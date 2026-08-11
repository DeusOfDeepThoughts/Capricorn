#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class ProcessSupervisor final : public QObject {
    Q_OBJECT
public:
    explicit ProcessSupervisor(QObject *parent = nullptr);
    ~ProcessSupervisor() override;

    bool start(QString *error = nullptr);
    void stop();
    void stopAsync(int gracefulTimeoutMs = 180, int killTimeoutMs = 120);
    QString token() const { return m_token; }
    QString coreUrl() const { return QStringLiteral("http://127.0.0.1:4235"); }

signals:
    void processError(const QString &message);
    void stopped();

private:
    bool startCore(QString *error);
    void configureHidden(QProcess &process);
    void attachToLifetimeGroup(QProcess &process);
    void maybeFinishAsyncStop();

    QProcess m_core;
    QString m_token;
    void *m_lifetimeGroup{};
    bool m_stopping{false};
    int m_stopGeneration{0};
};
