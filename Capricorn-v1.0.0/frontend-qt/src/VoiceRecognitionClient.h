#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

class VoiceRecognitionClient final : public QObject {
    Q_OBJECT
public:
    explicit VoiceRecognitionClient(QObject *parent = nullptr);
    ~VoiceRecognitionClient() override;

    bool isRunning() const noexcept { return m_running.load(); }
    void start(const QString &appId, const QString &apiKey);
    void stop();

signals:
    void started();
    void interimText(const QString &text);
    void finalText(const QString &text);
    void errorOccurred(const QString &message);
    void stopped();

private:
    void runSession(QString appId, QString apiKey);

    std::thread m_thread;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_running{false};
    std::atomic<void *> m_captureWakeEvent{nullptr};
};
