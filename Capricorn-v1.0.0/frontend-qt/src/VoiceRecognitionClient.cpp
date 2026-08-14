#include "VoiceRecognitionClient.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
#include <mmsystem.h>
#include <winhttp.h>
#endif

namespace {
#ifdef Q_OS_WIN
QString systemErrorText(const QString &prefix, DWORD code) {
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    QString detail;
    if (length && buffer) detail = QString::fromWCharArray(buffer, int(length)).trimmed();
    if (buffer) LocalFree(buffer);
    return detail.isEmpty() ? QStringLiteral("%1（错误码 %2）").arg(prefix).arg(code)
                            : QStringLiteral("%1：%2").arg(prefix, detail);
}

QString mmErrorText(const QString &prefix, MMRESULT code) {
    wchar_t buffer[MAXERRORLENGTH]{};
    if (waveInGetErrorTextW(code, buffer, MAXERRORLENGTH) == MMSYSERR_NOERROR)
        return QStringLiteral("%1：%2").arg(prefix, QString::fromWCharArray(buffer));
    return QStringLiteral("%1（错误码 %2）").arg(prefix).arg(code);
}

bool sendWebSocketText(HINTERNET socket, const QByteArray &payload) {
    if (!socket) return false;
    return WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                const_cast<char *>(payload.constData()), DWORD(payload.size())) == NO_ERROR;
}

bool sendWebSocketBinary(HINTERNET socket, const void *data, DWORD size) {
    if (!socket || !data || size == 0) return false;
    return WinHttpWebSocketSend(socket, WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE,
                                const_cast<void *>(data), size) == NO_ERROR;
}
#endif
} // namespace

VoiceRecognitionClient::VoiceRecognitionClient(QObject *parent) : QObject(parent) {}

VoiceRecognitionClient::~VoiceRecognitionClient() {
    stop();
    if (m_thread.joinable()) m_thread.join();
}

void VoiceRecognitionClient::start(const QString &appId, const QString &apiKey) {
    if (m_running.load()) return;
    if (m_thread.joinable()) m_thread.join();
    m_stopRequested.store(false);
    m_running.store(true);
    m_thread = std::thread([this, appId, apiKey] { runSession(appId, apiKey); });
}

void VoiceRecognitionClient::stop() {
    m_stopRequested.store(true);
#ifdef Q_OS_WIN
    if (void *raw = m_captureWakeEvent.load()) SetEvent(static_cast<HANDLE>(raw));
#endif
}

void VoiceRecognitionClient::runSession(QString appId, QString apiKey) {
#ifdef Q_OS_WIN
    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    HINTERNET socket = nullptr;
    HWAVEIN waveIn = nullptr;
    HANDLE captureEvent = nullptr;
    bool wavePrepared = false;
    bool receiverStarted = false;
    std::thread receiver;
    std::mutex receiverMutex;
    std::condition_variable receiverCondition;
    bool receiverDone = false;

    auto finish = [this, &session, &connection, &request, &socket, &waveIn, &captureEvent,
                   &wavePrepared, &receiver, &receiverStarted]() {
        m_captureWakeEvent.store(nullptr);
        if (waveIn) {
            waveInStop(waveIn);
            waveInReset(waveIn);
        }
        if (receiverStarted && receiver.joinable()) receiver.join();
        if (socket) WinHttpCloseHandle(socket);
        if (request) WinHttpCloseHandle(request);
        if (connection) WinHttpCloseHandle(connection);
        if (session) WinHttpCloseHandle(session);
        if (waveIn) waveInClose(waveIn);
        if (captureEvent) CloseHandle(captureEvent);
        wavePrepared = false;
        m_running.store(false);
        emit stopped();
    };

    const QString trimmedAppId = appId.trimmed();
    const QString trimmedKey = apiKey.trimmed();
    if (trimmedAppId.isEmpty() || trimmedKey.isEmpty()) {
        emit errorOccurred(QStringLiteral("语音配置不完整，请先保存 AppID 和 API Key。"));
        finish();
        return;
    }

    session = WinHttpOpen(L"Capricorn/109", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        emit errorOccurred(systemErrorText(QStringLiteral("无法初始化语音网络连接"), GetLastError()));
        finish();
        return;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);

    connection = WinHttpConnect(session, L"vop.baidu.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) {
        emit errorOccurred(systemErrorText(QStringLiteral("无法连接百度实时语音服务"), GetLastError()));
        finish();
        return;
    }

    const QString sn = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const std::wstring path = QStringLiteral("/realtime_asr?sn=%1").arg(sn).toStdWString();
    request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        emit errorOccurred(systemErrorText(QStringLiteral("无法创建实时语音请求"), GetLastError()));
        finish();
        return;
    }
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) ||
        !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        emit errorOccurred(systemErrorText(QStringLiteral("无法建立实时语音 WebSocket"), GetLastError()));
        finish();
        return;
    }

    socket = WinHttpWebSocketCompleteUpgrade(request, 0);
    if (!socket) {
        emit errorOccurred(systemErrorText(QStringLiteral("实时语音 WebSocket 握手失败"), GetLastError()));
        finish();
        return;
    }
    WinHttpCloseHandle(request);
    request = nullptr;

    QJsonObject startData{
        {QStringLiteral("appid"), trimmedAppId.toLongLong()},
        {QStringLiteral("appkey"), trimmedKey},
        {QStringLiteral("dev_pid"), 15372},
        {QStringLiteral("cuid"), QStringLiteral("capricorn-") + QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("format"), QStringLiteral("pcm")},
        {QStringLiteral("sample"), 16000}
    };
    const QByteArray startFrame = QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("START")},
                                                             {QStringLiteral("data"), startData}})
                                      .toJson(QJsonDocument::Compact);
    if (!sendWebSocketText(socket, startFrame)) {
        emit errorOccurred(QStringLiteral("无法发送实时语音开始参数。"));
        finish();
        return;
    }

    // Receive MID_TEXT/FIN_TEXT on a dedicated thread. Audio capture and upload
    // remain independent so incoming recognition results never block microphone IO.
    receiverStarted = true;
    receiver = std::thread([this, socket, &receiverMutex, &receiverCondition, &receiverDone] {
        std::array<char, 65536> buffer{};
        QByteArray textMessage;
        while (true) {
            DWORD bytesRead = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
            const DWORD status = WinHttpWebSocketReceive(socket, buffer.data(), DWORD(buffer.size()), &bytesRead, &type);
            if (status != NO_ERROR) {
                if (!m_stopRequested.load()) {
                    emit errorOccurred(systemErrorText(QStringLiteral("实时语音连接已中断"), status));
                    stop();
                }
                break;
            }
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                if (!m_stopRequested.load()) stop();
                break;
            }
            if (type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE &&
                type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) continue;
            if (bytesRead) textMessage.append(buffer.data(), qsizetype(bytesRead));
            if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;

            QJsonParseError parseError{};
            const QJsonDocument document = QJsonDocument::fromJson(textMessage, &parseError);
            textMessage.clear();
            if (parseError.error != QJsonParseError::NoError || !document.isObject()) continue;
            const QJsonObject payload = document.object();
            const int errorNumber = payload.value(QStringLiteral("err_no")).toInt(0);
            if (errorNumber != 0) {
                const QString serverMessage = payload.value(QStringLiteral("err_msg")).toString();
                emit errorOccurred(serverMessage.isEmpty()
                                       ? QStringLiteral("百度实时语音识别失败（错误码 %1）。").arg(errorNumber)
                                       : QStringLiteral("百度实时语音识别失败：%1").arg(serverMessage));
                stop();
                break;
            }
            const QString typeName = payload.value(QStringLiteral("type")).toString();
            const QString result = payload.value(QStringLiteral("result")).toString();
            if (typeName == QStringLiteral("MID_TEXT")) emit interimText(result);
            else if (typeName == QStringLiteral("FIN_TEXT") && !result.isEmpty()) emit finalText(result);
        }
        {
            std::lock_guard<std::mutex> lock(receiverMutex);
            receiverDone = true;
        }
        receiverCondition.notify_all();
    });

    captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureEvent) {
        emit errorOccurred(systemErrorText(QStringLiteral("无法初始化麦克风事件"), GetLastError()));
        m_stopRequested.store(true);
    } else {
        m_captureWakeEvent.store(captureEvent);
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = 16000;
    format.wBitsPerSample = 16;
    format.nBlockAlign = WORD(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    format.cbSize = 0;

    constexpr int kBufferCount = 4;
    constexpr int kFrameBytes = 5120; // 160 ms @ 16 kHz, mono, PCM16.
    std::array<std::array<char, kFrameBytes>, kBufferCount> audioBuffers{};
    std::array<WAVEHDR, kBufferCount> headers{};

    if (!m_stopRequested.load()) {
        const MMRESULT openResult = waveInOpen(&waveIn, WAVE_MAPPER, &format,
                                               reinterpret_cast<DWORD_PTR>(captureEvent), 0, CALLBACK_EVENT);
        if (openResult != MMSYSERR_NOERROR) {
            emit errorOccurred(mmErrorText(QStringLiteral("无法打开麦克风"), openResult));
            m_stopRequested.store(true);
        }
    }

    if (waveIn && !m_stopRequested.load()) {
        bool prepareOk = true;
        for (int i = 0; i < kBufferCount; ++i) {
            headers[i] = {};
            headers[i].lpData = audioBuffers[i].data();
            headers[i].dwBufferLength = kFrameBytes;
            const MMRESULT prepareResult = waveInPrepareHeader(waveIn, &headers[i], sizeof(WAVEHDR));
            if (prepareResult != MMSYSERR_NOERROR) {
                emit errorOccurred(mmErrorText(QStringLiteral("无法准备麦克风缓冲区"), prepareResult));
                prepareOk = false;
                break;
            }
            const MMRESULT addResult = waveInAddBuffer(waveIn, &headers[i], sizeof(WAVEHDR));
            if (addResult != MMSYSERR_NOERROR) {
                emit errorOccurred(mmErrorText(QStringLiteral("无法提交麦克风缓冲区"), addResult));
                prepareOk = false;
                break;
            }
        }
        wavePrepared = prepareOk;
        if (prepareOk) {
            const MMRESULT startResult = waveInStart(waveIn);
            if (startResult != MMSYSERR_NOERROR) {
                emit errorOccurred(mmErrorText(QStringLiteral("无法开始录音"), startResult));
                m_stopRequested.store(true);
            } else {
                emit started();
            }
        } else {
            m_stopRequested.store(true);
        }
    }

    auto uploadReadyBuffers = [&](bool requeue) {
        if (!waveIn || !socket) return;
        for (WAVEHDR &header : headers) {
            if (!(header.dwFlags & WHDR_DONE)) continue;
            const DWORD recorded = header.dwBytesRecorded;
            if (recorded > 0 && !sendWebSocketBinary(socket, header.lpData, recorded)) {
                emit errorOccurred(QStringLiteral("实时语音音频上传失败。"));
                m_stopRequested.store(true);
                requeue = false;
            }
            if (requeue && !m_stopRequested.load()) {
                header.dwBytesRecorded = 0;
                const MMRESULT addResult = waveInAddBuffer(waveIn, &header, sizeof(WAVEHDR));
                if (addResult != MMSYSERR_NOERROR) {
                    emit errorOccurred(mmErrorText(QStringLiteral("麦克风缓冲区恢复失败"), addResult));
                    m_stopRequested.store(true);
                }
            }
        }
    };

    while (waveIn && !m_stopRequested.load()) {
        const DWORD waitResult = WaitForSingleObject(captureEvent, 500);
        if (waitResult == WAIT_OBJECT_0) uploadReadyBuffers(true);
        else if (waitResult != WAIT_TIMEOUT) {
            emit errorOccurred(systemErrorText(QStringLiteral("等待麦克风数据失败"), GetLastError()));
            m_stopRequested.store(true);
        }
    }

    if (waveIn) {
        waveInStop(waveIn);
        waveInReset(waveIn);
        uploadReadyBuffers(false); // flush the partial final frame before FINISH.
        for (WAVEHDR &header : headers) {
            if (header.dwFlags & WHDR_PREPARED) waveInUnprepareHeader(waveIn, &header, sizeof(WAVEHDR));
        }
    }
    m_captureWakeEvent.store(nullptr);

    if (socket) {
        const QByteArray finishFrame = QByteArrayLiteral("{\"type\":\"FINISH\"}");
        sendWebSocketText(socket, finishFrame);
        std::unique_lock<std::mutex> lock(receiverMutex);
        const bool serverClosed = receiverCondition.wait_for(lock, std::chrono::milliseconds(2500),
                                                              [&receiverDone] { return receiverDone; });
        lock.unlock();
        if (!serverClosed) {
            // A dead network must never leave the UI stuck in the recording state.
            WinHttpCloseHandle(socket);
            socket = nullptr;
        }
    }

    finish();
#else
    Q_UNUSED(appId)
    Q_UNUSED(apiKey)
    emit errorOccurred(QStringLiteral("当前平台暂不支持实时语音录制。"));
    m_running.store(false);
    emit stopped();
#endif
}
