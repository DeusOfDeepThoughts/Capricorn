#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

struct ChatStoreSnapshot {
    QJsonArray messages;
    QJsonArray structuredFacts;
    QString globalMemoryMarkdown;
    QString relationshipMemoryMarkdown;
    int globalMemoryRevision{};
    int relationshipMemoryRevision{};
    int structuredFactsRevision{};
    bool storeAvailable{false};
    bool exists{false};
};

class ChatStore final {
public:
    static ChatStore &instance();

    ChatStoreSnapshot load(const QString &personaId);
    bool replaceSnapshot(const QString &personaId, const QString &personaName,
                         const QJsonArray &messages, const QString &relationshipMemoryMarkdown,
                         int relationshipMemoryRevision);
    bool appendMessage(const QString &personaId, const QString &personaName,
                       const QJsonObject &message);
    bool importMessages(const QString &personaId, const QString &personaName,
                        const QJsonArray &messages, int *importedCount = nullptr,
                        QString *error = nullptr);
    bool updateRelationshipMemory(const QString &personaId, const QString &personaName,
                                  const QString &relationshipMemoryMarkdown,
                                  int relationshipMemoryRevision);
    bool compareAndSwapRelationshipMemory(const QString &personaId, const QString &personaName,
                                          const QString &relationshipMemoryMarkdown,
                                          int expectedRevision, int newRevision);
    bool updateGlobalMemory(const QString &globalMemoryMarkdown, int globalMemoryRevision);
    bool compareAndSwapGlobalMemory(const QString &globalMemoryMarkdown,
                                    int expectedRevision, int newRevision);
    bool applyStructuredFactUpdates(const QJsonArray &updates, int expectedRevision,
                                    int newRevision, int retriesRemaining = 3);
    bool deletePersonaData(const QString &personaId, QString *error = nullptr);
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
