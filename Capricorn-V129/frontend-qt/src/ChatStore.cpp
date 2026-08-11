#include "ChatStore.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QSet>
#include <QStringList>
#include <QUuid>
#include <QVariant>
#include <QDebug>

namespace {
constexpr int kMaximumStoredMessagesPerPersona = 1000;
constexpr int kMaximumStoredMessageCharacters = 64000;
constexpr int kMaximumStoredCharactersPerPersona = 4 * 1024 * 1024;
constexpr int kMaximumStoredMemoryCharacters = 24000;

QString normalizedPersonaId(const QString &personaId) {
    const QString value = personaId.trimmed();
    return value.isEmpty() ? QStringLiteral("Capricorn") : value.left(240);
}

QString normalizedMessageText(const QString &text) {
    return text.left(kMaximumStoredMessageCharacters);
}

void logSqlFailure(const char *operation, const QSqlError &error) {
    qWarning().noquote() << "ChatStore" << operation << error.text();
}

bool migrateLegacyChatDatabase(QSqlDatabase &database, const QString &root) {
    QSqlQuery marker(database);
    if (!marker.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS storage_meta ("
            "key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL)"))) {
        logSqlFailure("create storage_meta", marker.lastError());
        return false;
    }
    marker.prepare(QStringLiteral("SELECT value FROM storage_meta WHERE key=?"));
    marker.addBindValue(QStringLiteral("v97_legacy_sqlite_migration"));
    if (!marker.exec()) {
        logSqlFailure("read migration marker", marker.lastError());
        return false;
    }
    if (marker.next()) return true;
    marker.finish();

    const QString parent = QFileInfo(root).dir().absolutePath();
    const QString target = QDir(root).filePath(QStringLiteral("chat-v97.sqlite3"));
    QStringList rawCandidates;
    for (int version = 96; version >= 73; --version) {
        const QString fileName = QStringLiteral("chat-v%1.sqlite3").arg(version);
        rawCandidates << QDir(root).filePath(fileName)
                      << QDir(parent).filePath(QStringLiteral("Capricorn-V%1/").arg(version) + fileName)
                      << QDir(parent).filePath(QStringLiteral("CapricornV%1/").arg(version) + fileName)
                      << QDir(parent).filePath(QStringLiteral("Capricorn/") + fileName);
    }

    QSet<QString> visited;
    bool foundLegacy = false;
    bool migrated = false;
    for (const QString &rawCandidate : rawCandidates) {
        const QString candidate = QDir::cleanPath(rawCandidate);
        if (visited.contains(candidate) || candidate == QDir::cleanPath(target)
            || !QFileInfo::exists(candidate)) continue;
        visited.insert(candidate);
        foundLegacy = true;

        {
            QSqlQuery attach(database);
            attach.prepare(QStringLiteral("ATTACH DATABASE ? AS legacy_chat"));
            attach.addBindValue(candidate);
            if (!attach.exec()) {
                logSqlFailure("attach legacy chat database", attach.lastError());
                continue;
            }
        }

        bool compatible = false;
        {
            QSqlQuery tables(database);
            compatible = tables.exec(QStringLiteral(
                "SELECT COUNT(*) FROM legacy_chat.sqlite_master "
                "WHERE type='table' AND name IN ('persona_memory','messages')"))
                && tables.next() && tables.value(0).toInt() == 2;
        }

        bool copied = false;
        if (compatible && database.transaction()) {
            QSqlQuery copy(database);
            copied = copy.exec(QStringLiteral(
                "INSERT OR IGNORE INTO persona_memory "
                "(persona_id, persona_name, long_memory, memory_revision, updated_at) "
                "SELECT persona_id, persona_name, long_memory, memory_revision, updated_at "
                "FROM legacy_chat.persona_memory"));
            if (copied) {
                copied = copy.exec(QStringLiteral(
                    "INSERT OR IGNORE INTO messages (id, persona_id, role, text, created_at) "
                    "SELECT m.id, m.persona_id, m.role, m.text, m.created_at "
                    "FROM legacy_chat.messages AS m "
                    "JOIN persona_memory AS p ON p.persona_id=m.persona_id"));
            }
            if (copied) {
                copied = database.commit();
                if (!copied) database.rollback();
            } else {
                database.rollback();
            }
        }

        QSqlQuery detach(database);
        if (!detach.exec(QStringLiteral("DETACH DATABASE legacy_chat"))) {
            logSqlFailure("detach legacy chat database", detach.lastError());
            copied = false;
        }
        if (copied) {
            migrated = true;
            break;
        }
    }

    // Mark a conclusive success or a genuinely clean installation. A transient
    // attach/copy failure remains unmarked so the next launch can retry safely.
    if (migrated || !foundLegacy) {
        QSqlQuery writeMarker(database);
        writeMarker.prepare(QStringLiteral(
            "INSERT OR REPLACE INTO storage_meta(key, value) VALUES(?, ?)"));
        writeMarker.addBindValue(QStringLiteral("v97_legacy_sqlite_migration"));
        writeMarker.addBindValue(migrated ? QStringLiteral("migrated") : QStringLiteral("not-found"));
        if (!writeMarker.exec()) {
            logSqlFailure("write migration marker", writeMarker.lastError());
            return false;
        }
    }
    return migrated || !foundLegacy;
}
}

ChatStore &ChatStore::instance() {
    // Process-lifetime ownership avoids QSqlDatabase teardown ordering problems
    // after QApplication has already destroyed Qt's plugin infrastructure.
    static ChatStore *store = new ChatStore;
    return *store;
}

void ChatStore::close() {
    if (m_connectionName.isEmpty()) return;
    const QString connectionName = m_connectionName;
    {
        QSqlDatabase database = QSqlDatabase::database(connectionName, false);
        if (database.isValid() && database.isOpen()) {
            QSqlQuery checkpoint(database);
            checkpoint.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
            database.close();
        }
    }
    m_connectionName.clear();
    QSqlDatabase::removeDatabase(connectionName);
}

bool ChatStore::ensureOpen() {
    if (!m_connectionName.isEmpty()) {
        QSqlDatabase existing = QSqlDatabase::database(m_connectionName, false);
        return existing.isValid() && (existing.isOpen() || existing.open());
    }

    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (root.isEmpty() || !QDir().mkpath(root)) return false;
    m_databasePath = QDir(root).filePath(QStringLiteral("chat-v97.sqlite3"));
    m_connectionName = QStringLiteral("capricorn-chat-v97-%1")
                           .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    database.setDatabaseName(m_databasePath);
    database.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=5000"));
    if (!database.open()) {
        logSqlFailure("open", database.lastError());
        return false;
    }

    QSqlQuery pragma(database);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    QSqlQuery schema(database);
    if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS persona_memory ("
            "persona_id TEXT PRIMARY KEY NOT NULL, persona_name TEXT NOT NULL, "
            "long_memory TEXT NOT NULL DEFAULT '', memory_revision INTEGER NOT NULL DEFAULT 0, "
            "updated_at INTEGER NOT NULL)"))) {
        logSqlFailure("create persona_memory", schema.lastError());
        return false;
    }
    if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS messages ("
            "id TEXT PRIMARY KEY NOT NULL, persona_id TEXT NOT NULL, role TEXT NOT NULL, "
            "text TEXT NOT NULL, created_at INTEGER NOT NULL, "
            "FOREIGN KEY(persona_id) REFERENCES persona_memory(persona_id) ON DELETE CASCADE)"))) {
        logSqlFailure("create messages", schema.lastError());
        return false;
    }
    if (!schema.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS messages_persona_time "
            "ON messages(persona_id, created_at)"))) {
        logSqlFailure("create messages index", schema.lastError());
        return false;
    }
    if (!migrateLegacyChatDatabase(database, root))
        qWarning() << "ChatStore will retry legacy SQLite migration on the next launch";
    return true;
}

bool ChatStore::ensurePersona(const QString &personaId, const QString &personaName) {
    if (!ensureOpen()) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO persona_memory(persona_id, persona_name, long_memory, memory_revision, updated_at) "
        "VALUES(?, ?, '', 0, ?) "
        "ON CONFLICT(persona_id) DO UPDATE SET persona_name=excluded.persona_name, updated_at=excluded.updated_at"));
    query.addBindValue(normalizedPersonaId(personaId));
    query.addBindValue(personaName.trimmed().left(240));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        logSqlFailure("ensure persona", query.lastError());
        return false;
    }
    return true;
}

ChatStoreSnapshot ChatStore::load(const QString &personaId) {
    ChatStoreSnapshot snapshot;
    if (!ensureOpen()) return snapshot;
    snapshot.storeAvailable = true;
    const QString id = normalizedPersonaId(personaId);
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);

    QSqlQuery persona(database);
    persona.prepare(QStringLiteral(
        "SELECT long_memory, memory_revision FROM persona_memory WHERE persona_id=?"));
    persona.addBindValue(id);
    if (!persona.exec()) {
        logSqlFailure("load persona", persona.lastError());
        snapshot.storeAvailable = false;
        return snapshot;
    }
    if (!persona.next()) return snapshot;
    snapshot.exists = true;
    snapshot.longMemoryMarkdown = persona.value(0).toString().left(kMaximumStoredMemoryCharacters);
    snapshot.memoryRevision = persona.value(1).toInt();

    QSqlQuery messages(database);
    messages.prepare(QStringLiteral(
        "SELECT id, role, text, created_at FROM ("
        "SELECT rowid AS stable_rowid, id, role, text, created_at FROM messages "
        "WHERE persona_id=? ORDER BY created_at DESC, rowid DESC LIMIT ?) "
        "ORDER BY created_at ASC, stable_rowid ASC"));
    messages.addBindValue(id);
    messages.addBindValue(kMaximumStoredMessagesPerPersona);
    if (!messages.exec()) {
        logSqlFailure("load messages", messages.lastError());
        snapshot.storeAvailable = false;
        return snapshot;
    }
    while (messages.next()) {
        snapshot.messages.append(QJsonObject{
            {QStringLiteral("id"), messages.value(0).toString()},
            {QStringLiteral("role"), messages.value(1).toString()},
            {QStringLiteral("text"), messages.value(2).toString()},
            {QStringLiteral("at"), messages.value(3).toLongLong()}
        });
    }
    return snapshot;
}

bool ChatStore::insertMessages(const QString &personaId, const QJsonArray &messages) {
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery insert(database);
    insert.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO messages(id, persona_id, role, text, created_at) VALUES(?, ?, ?, ?, ?)"));
    const QString id = normalizedPersonaId(personaId);
    const int start = qMax(0, messages.size() - kMaximumStoredMessagesPerPersona);
    for (int index = start; index < messages.size(); ++index) {
        const QJsonObject message = messages.at(index).toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        const QString text = normalizedMessageText(message.value(QStringLiteral("text")).toString());
        if ((role != QStringLiteral("user") && role != QStringLiteral("assistant")) || text.isEmpty()) continue;
        QString messageId = message.value(QStringLiteral("id")).toString();
        if (messageId.isEmpty()) messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        insert.bindValue(0, messageId);
        insert.bindValue(1, id);
        insert.bindValue(2, role);
        insert.bindValue(3, text);
        insert.bindValue(4, qint64(message.value(QStringLiteral("at")).toDouble(
                                QDateTime::currentMSecsSinceEpoch())));
        if (!insert.exec()) {
            logSqlFailure("insert message", insert.lastError());
            return false;
        }
    }
    return true;
}

bool ChatStore::pruneMessages(const QString &personaId) {
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    const QString id = normalizedPersonaId(personaId);
    QSqlQuery countPrune(database);
    countPrune.prepare(QStringLiteral(
        "DELETE FROM messages WHERE rowid IN (SELECT rowid FROM messages WHERE persona_id=? "
        "ORDER BY created_at DESC, rowid DESC LIMIT -1 OFFSET ?)"));
    countPrune.addBindValue(id);
    countPrune.addBindValue(kMaximumStoredMessagesPerPersona);
    if (!countPrune.exec()) {
        logSqlFailure("prune message count", countPrune.lastError());
        return false;
    }

    // Keep the newest messages whose cumulative text fits the per-persona disk
    // budget. SQLite remains compact and queryable even after years of chatting.
    QSqlQuery sizePrune(database);
    sizePrune.prepare(QStringLiteral(
        "DELETE FROM messages WHERE rowid IN (SELECT stable_rowid FROM ("
        "SELECT rowid AS stable_rowid, SUM(length(text)) OVER "
        "(ORDER BY created_at DESC, rowid DESC) AS running_chars "
        "FROM messages WHERE persona_id=?) WHERE running_chars > ?)"));
    sizePrune.addBindValue(id);
    sizePrune.addBindValue(kMaximumStoredCharactersPerPersona);
    if (!sizePrune.exec()) {
        logSqlFailure("prune message size", sizePrune.lastError());
        return false;
    }
    return true;
}

bool ChatStore::replaceSnapshot(const QString &personaId, const QString &personaName,
                                const QJsonArray &messages, const QString &longMemoryMarkdown,
                                int memoryRevision) {
    if (!ensureOpen()) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) return false;
    QSqlQuery persona(database);
    persona.prepare(QStringLiteral(
        "INSERT INTO persona_memory(persona_id, persona_name, long_memory, memory_revision, updated_at) "
        "VALUES(?, ?, ?, ?, ?) ON CONFLICT(persona_id) DO UPDATE SET "
        "persona_name=excluded.persona_name, long_memory=excluded.long_memory, "
        "memory_revision=excluded.memory_revision, updated_at=excluded.updated_at"));
    persona.addBindValue(normalizedPersonaId(personaId));
    persona.addBindValue(personaName.trimmed().left(240));
    persona.addBindValue(longMemoryMarkdown.left(kMaximumStoredMemoryCharacters));
    persona.addBindValue(qMax(0, memoryRevision));
    persona.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!persona.exec()) {
        logSqlFailure("replace snapshot persona", persona.lastError());
        database.rollback();
        return false;
    }
    QSqlQuery clear(database);
    clear.prepare(QStringLiteral("DELETE FROM messages WHERE persona_id=?"));
    clear.addBindValue(normalizedPersonaId(personaId));
    if (!clear.exec() || !insertMessages(personaId, messages) || !pruneMessages(personaId)
        || !database.commit()) {
        logSqlFailure("replace snapshot", clear.lastError());
        database.rollback();
        return false;
    }
    return true;
}

bool ChatStore::appendMessage(const QString &personaId, const QString &personaName,
                              const QJsonObject &message) {
    if (!ensurePersona(personaId, personaName)) return false;
    if (!insertMessages(personaId, QJsonArray{message})) return false;
    return pruneMessages(personaId);
}

bool ChatStore::updateMemory(const QString &personaId, const QString &personaName,
                             const QString &longMemoryMarkdown, int memoryRevision) {
    if (!ensureOpen()) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO persona_memory(persona_id, persona_name, long_memory, memory_revision, updated_at) "
        "VALUES(?, ?, ?, ?, ?) ON CONFLICT(persona_id) DO UPDATE SET "
        "persona_name=excluded.persona_name, long_memory=excluded.long_memory, "
        "memory_revision=excluded.memory_revision, updated_at=excluded.updated_at"));
    query.addBindValue(normalizedPersonaId(personaId));
    query.addBindValue(personaName.trimmed().left(240));
    query.addBindValue(longMemoryMarkdown.left(kMaximumStoredMemoryCharacters));
    query.addBindValue(qMax(0, memoryRevision));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        logSqlFailure("update memory", query.lastError());
        return false;
    }
    return true;
}
