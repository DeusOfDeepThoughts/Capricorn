#include "CoreClient.h"

#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QtGlobal>

CoreClient::CoreClient(QObject *parent) : QObject(parent) {}

void CoreClient::setEndpoint(const QString &baseUrl, const QString &token) {
    m_baseUrl = baseUrl.endsWith(u'/') ? baseUrl.left(baseUrl.size() - 1) : baseUrl;
    m_token = token;
}

QNetworkReply *CoreClient::request(const QByteArray &method, const QString &path, const QJsonObject &body,
                                   Callback callback, int timeoutMs) {
    QNetworkRequest request(QUrl(m_baseUrl + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json; charset=utf-8"));
    request.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json"));
    if (!m_token.isEmpty()) request.setRawHeader(QByteArrayLiteral("X-Capricorn-Token"), m_token.toUtf8());
    QNetworkReply *reply = nullptr;
    if (method == QByteArrayLiteral("GET")) reply = m_network.get(request);
    else reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->start(qMax(1000, timeoutMs));
    connect(timer, &QTimer::timeout, reply, [reply] {
        reply->setProperty("capricornTimedOut", true);
        reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() mutable {
        const QByteArray raw = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject object = QJsonDocument::fromJson(raw).object();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300 && object.value(QStringLiteral("ok")).toBool(true);
        QString error = object.value(QStringLiteral("error")).toString();
        if (error.isEmpty() && !ok) {
            if (reply->property("capricornTimedOut").toBool())
                error = QStringLiteral("请求超时，请检查当前模型响应速度或网络连接后重试。");
            else
                error = reply->errorString();
        }
        reply->deleteLater();
        callback(ok, object, error);
    });
    return reply;
}

void CoreClient::verifyText(const QJsonObject &config, Callback callback) {
    request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/verify-text"),
            QJsonObject{{QStringLiteral("config"), config}}, std::move(callback), 180000);
}
void CoreClient::generateProfileSummary(const QJsonObject &config, const QJsonObject &profileData, Callback callback) {
    request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/user-profile/summary"), QJsonObject{
        {QStringLiteral("config"), config},
        {QStringLiteral("profile"), profileData}
    }, std::move(callback), 180000);
}
void CoreClient::generateProfileInsights(const QJsonObject &config, const QJsonObject &profileData, Callback callback) {
    request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/user-profile/insights"), QJsonObject{
        {QStringLiteral("config"), config},
        {QStringLiteral("profile"), profileData}
    }, std::move(callback), 90000);
}
void CoreClient::generateProfileTopics(const QJsonObject &config, const QJsonObject &profileData, Callback callback) {
    request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/user-profile/topics"), QJsonObject{
        {QStringLiteral("config"), config},
        {QStringLiteral("profile"), profileData}
    }, std::move(callback), 90000);
}

void CoreClient::createPersonaSession(const QJsonObject &config, const QString &profile, int strength,
                                      const QString &personaId, const QString &personaName,
                                      const QString &globalMemoryMarkdown,
                                      const QString &relationshipMemoryMarkdown,
                                      int globalMemoryRevision, int relationshipMemoryRevision,
                                      const QJsonArray &structuredFacts, int structuredFactsRevision,
                                      const QJsonArray &recentMessages, Callback callback) {
    request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/persona-session"), QJsonObject{
        {QStringLiteral("config"), config},
        {QStringLiteral("profileMarkdown"), profile},
        {QStringLiteral("personaStrength"), strength},
        {QStringLiteral("personaId"), personaId},
        {QStringLiteral("personaName"), personaName},
        {QStringLiteral("globalMemoryMarkdown"), globalMemoryMarkdown},
        {QStringLiteral("relationshipMemoryMarkdown"), relationshipMemoryMarkdown},
        {QStringLiteral("longMemoryMarkdown"), relationshipMemoryMarkdown},
        {QStringLiteral("globalMemoryRevision"), globalMemoryRevision},
        {QStringLiteral("relationshipMemoryRevision"), relationshipMemoryRevision},
        {QStringLiteral("structuredFacts"), structuredFacts},
        {QStringLiteral("structuredFactsRevision"), structuredFactsRevision},
        {QStringLiteral("recentMessages"), recentMessages}
    }, std::move(callback), 360000);
}
QNetworkReply *CoreClient::chat(const QString &sessionId, const QString &message,
                      const QJsonArray &structuredFacts, int structuredFactsRevision,
                      Callback callback) {
    // A normal reply performs personality audit and durable memory refresh before
    // returning, so allow enough time for several bounded upstream calls.
    return request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/chat"), QJsonObject{
        {QStringLiteral("sessionId"), sessionId},
        {QStringLiteral("prompt"), message},
        {QStringLiteral("structuredFacts"), structuredFacts},
        {QStringLiteral("structuredFactsRevision"), structuredFactsRevision}
    }, std::move(callback), 360000);
}
void CoreClient::acknowledgeChat(const QString &transactionId, Callback callback) {
    request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/chat/ack"),
            QJsonObject{{QStringLiteral("transactionId"), transactionId}},
            std::move(callback), 30000);
}
QNetworkReply *CoreClient::rebuildMemory(const QString &sessionId, const QJsonArray &messages, Callback callback) {
    return request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/memory/rebuild"),
                   QJsonObject{{QStringLiteral("sessionId"), sessionId}, {QStringLiteral("messages"), messages}},
                   std::move(callback), 360000);
}
QNetworkReply *CoreClient::acknowledgeMemoryRebuild(const QString &transactionId, Callback callback) {
    return request(QByteArrayLiteral("POST"), QStringLiteral("/v1/model/memory/rebuild/ack"),
                   QJsonObject{{QStringLiteral("transactionId"), transactionId}},
                   std::move(callback), 30000);
}
void CoreClient::shutdown() { request(QByteArrayLiteral("POST"), QStringLiteral("/v1/shutdown"), {}, [](bool, const QJsonObject &, const QString &) {}, 10000); }
