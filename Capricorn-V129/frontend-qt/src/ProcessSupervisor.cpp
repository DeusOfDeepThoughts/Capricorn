#include "ProcessSupervisor.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

ProcessSupervisor::ProcessSupervisor(QObject *parent)
    : QObject(parent), m_token(QUuid::createUuid().toString(QUuid::WithoutBraces)) {
    connect(&m_core, &QProcess::readyReadStandardOutput, &m_core,
            [this] { m_core.readAllStandardOutput(); });
    connect(&m_core, &QProcess::readyReadStandardError, &m_core,
            [this] { m_core.readAllStandardError(); });
    connect(&m_core, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) { maybeFinishAsyncStop(); });
    connect(&m_core, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit processError(QStringLiteral("Go Core：") + m_core.errorString());
    });
#ifdef Q_OS_WIN
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            m_lifetimeGroup = job;
        else
            CloseHandle(job);
    }
#endif
}

ProcessSupervisor::~ProcessSupervisor() {
    stop();
#ifdef Q_OS_WIN
    if (m_lifetimeGroup) CloseHandle(static_cast<HANDLE>(m_lifetimeGroup));
#endif
}

void ProcessSupervisor::configureHidden(QProcess &process) {
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
        args->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        args->startupInfo->wShowWindow = SW_HIDE;
    });
#else
    Q_UNUSED(process)
#endif
}

void ProcessSupervisor::attachToLifetimeGroup(QProcess &process) {
#ifdef Q_OS_WIN
    if (!m_lifetimeGroup || process.processId() == 0) return;
    HANDLE child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process.processId()));
    if (!child) return;
    AssignProcessToJobObject(static_cast<HANDLE>(m_lifetimeGroup), child);
    CloseHandle(child);
#else
    Q_UNUSED(process)
#endif
}

bool ProcessSupervisor::startCore(QString *error) {
    const QString appDir = QCoreApplication::applicationDirPath();
    QString program = appDir + QStringLiteral("/core/CapricornCore-V129.exe");
#ifndef Q_OS_WIN
    if (!QFileInfo::exists(program)) program = appDir + QStringLiteral("/core/CapricornCore-V129");
#endif
    if (!QFileInfo::exists(program)) {
        if (error) *error = QStringLiteral("找不到 Go Core：") + program;
        return false;
    }
    configureHidden(m_core);
    m_core.setProgram(program);
    m_core.setArguments({QStringLiteral("--port"), QStringLiteral("4235"),
                         QStringLiteral("--token"), m_token});
    m_core.setProcessChannelMode(QProcess::SeparateChannels);
    m_core.start();
    if (!m_core.waitForStarted(5000)) {
        if (error) *error = QStringLiteral("Go Core 启动失败：") + m_core.errorString();
        return false;
    }
    attachToLifetimeGroup(m_core);
    return true;
}

bool ProcessSupervisor::start(QString *error) {
    return startCore(error);
}

void ProcessSupervisor::maybeFinishAsyncStop() {
    if (!m_stopping || m_core.state() != QProcess::NotRunning) return;
    m_stopping = false;
    emit stopped();
}

void ProcessSupervisor::stopAsync(int gracefulTimeoutMs, int killTimeoutMs) {
    if (m_stopping) return;
    if (m_core.state() == QProcess::NotRunning) {
        QTimer::singleShot(0, this, [this] { emit stopped(); });
        return;
    }

    m_stopping = true;
    const int generation = ++m_stopGeneration;
    m_core.terminate();

    QTimer::singleShot(qMax(0, gracefulTimeoutMs), this, [this, generation, killTimeoutMs] {
        if (!m_stopping || generation != m_stopGeneration) return;
        if (m_core.state() != QProcess::NotRunning) m_core.kill();
        maybeFinishAsyncStop();

        QTimer::singleShot(qMax(0, killTimeoutMs), this, [this, generation] {
            if (!m_stopping || generation != m_stopGeneration) return;
            if (m_core.state() != QProcess::NotRunning) m_core.kill();
            // The Windows Job Object remains the final lifetime safety net if a
            // damaged child process fails to emit finished promptly.
            m_stopping = false;
            emit stopped();
        });
    });
}

void ProcessSupervisor::stop() {
    ++m_stopGeneration;
    m_stopping = false;
    if (m_core.state() == QProcess::NotRunning) return;

    m_core.terminate();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 180 && m_core.state() != QProcess::NotRunning)
        m_core.waitForFinished(15);

    if (m_core.state() != QProcess::NotRunning) m_core.kill();
    if (m_core.state() != QProcess::NotRunning) m_core.waitForFinished(120);
}
