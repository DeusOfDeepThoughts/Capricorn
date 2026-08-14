#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <functional>

class QNetworkReply;

class CoreClient final : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(bool, const QJsonObject &, const QString &)>;

    explicit CoreClient(QObject *parent = nullptr);
    void setEndpoint(const QString &baseUrl, const QString &token);
    void verifyText(const QJsonObject &config, Callback callback);
    void generateProfileSummary(const QJsonObject &config, const QJsonObject &profileData, Callback callback);
    void generateProfileInsights(const QJsonObject &config, const QJsonObject &profileData, Callback callback);
    void generateProfileTopics(const QJsonObject &config, const QJsonObject &profileData, Callback callback);
    void createPersonaSession(const QJsonObject &config, const QString &profile, int strength,
                              const QString &personaId, const QString &personaName,
                              const QString &globalMemoryMarkdown,
                              const QString &relationshipMemoryMarkdown,
                              int globalMemoryRevision, int relationshipMemoryRevision,
                              const QJsonArray &structuredFacts, int structuredFactsRevision,
                              const QJsonArray &recentMessages, Callback callback);
    QNetworkReply *chat(const QString &sessionId, const QString &message,
                        const QJsonArray &structuredFacts, int structuredFactsRevision,
                        Callback callback);
    void acknowledgeChat(const QString &transactionId, Callback callback);
    QNetworkReply *rebuildMemory(const QString &sessionId, const QJsonArray &messages, Callback callback);
    QNetworkReply *acknowledgeMemoryRebuild(const QString &transactionId, Callback callback);
    void shutdown();

private:
    QNetworkReply *request(const QByteArray &method, const QString &path, const QJsonObject &body,
                           Callback callback, int timeoutMs = 120000);

    QNetworkAccessManager m_network;
    QString m_baseUrl{QStringLiteral("http://127.0.0.1:4235")};
    QString m_token;
};
