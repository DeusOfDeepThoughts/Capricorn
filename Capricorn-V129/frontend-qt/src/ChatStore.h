#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

struct ChatStoreSnapshot {
    QJsonArray messages;
    QString longMemoryMarkdown;
    int memoryRevision{};
    bool storeAvailable{false};
    bool exists{false};
};

class ChatStore final {
public:
    static ChatStore &instance();

    ChatStoreSnapshot load(const QString &personaId);
    bool replaceSnapshot(const QString &personaId, const QString &personaName,
                         const QJsonArray &messages, const QString &longMemoryMarkdown,
                         int memoryRevision);
    bool appendMessage(const QString &personaId, const QString &personaName,
                       const QJsonObject &message);
    bool updateMemory(const QString &personaId, const QString &personaName,
                      const QString &longMemoryMarkdown, int memoryRevision);
    void close();

private:
    ChatStore() = default;
    bool ensureOpen();
    bool ensurePersona(const QString &personaId, const QString &personaName);
    bool insertMessages(const QString &personaId, const QJsonArray &messages);
    bool pruneMessages(const QString &personaId);

    QString m_connectionName;
    QString m_databasePath;
};
