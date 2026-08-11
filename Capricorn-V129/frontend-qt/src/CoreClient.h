#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

class CoreClient final : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(bool, const QJsonObject &, const QString &)>;

    explicit CoreClient(QObject *parent = nullptr);
    void setEndpoint(const QString &baseUrl, const QString &token);
    void verifyText(const QJsonObject &config, Callback callback);
    void createPersonaSession(const QJsonObject &config, const QString &profile, int strength,
                              const QString &personaId, const QString &personaName,
                              const QString &longMemoryMarkdown, const QJsonArray &recentMessages,
                              Callback callback);
    void chat(const QString &sessionId, const QString &message, Callback callback);
    void rebuildMemory(const QString &sessionId, const QJsonArray &messages, Callback callback);
    void shutdown();

private:
    void request(const QByteArray &method, const QString &path, const QJsonObject &body,
                 Callback callback, int timeoutMs = 120000);

    QNetworkAccessManager m_network;
    QString m_baseUrl{QStringLiteral("http://127.0.0.1:4235")};
    QString m_token;
};
