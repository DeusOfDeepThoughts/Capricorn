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
#include <QtMath>
#include <limits>

namespace {
constexpr int kMaximumStoredMessagesPerPersona = 1000;
constexpr int kMaximumStoredMessageCharacters = 64000;
constexpr int kMaximumStoredCharactersPerPersona = 4 * 1024 * 1024;
constexpr int kMaximumStoredMemoryCharacters = 24000;
constexpr int kMaximumStructuredFacts = 512;
constexpr int kMaximumFactFieldCharacters = 4000;
constexpr qint64 kMinimumPlausibleUnixMilliseconds = 100000000000LL;
constexpr qint64 kMinimumPlausibleUnixSeconds = 946684800LL;
const QString kGlobalMemoryScope = QStringLiteral("local-user");

qint64 normalizedMessageTimestamp(const QJsonValue &value, qint64 fallback) {
    const double numeric = value.toDouble(double(fallback));
    if (!qIsFinite(numeric) || numeric < 0.0) return fallback;
    qint64 timestamp = qint64(numeric);
    if (timestamp >= kMinimumPlausibleUnixSeconds
        && timestamp < kMinimumPlausibleUnixMilliseconds) {
        timestamp *= 1000;
    }
    return timestamp >= kMinimumPlausibleUnixMilliseconds ? timestamp : fallback;
}

QString normalizedPersonaId(const QString &personaId) {
    const QString value = personaId.trimmed();
    return value.isEmpty() ? QStringLiteral("Capricorn") : value.left(240);
}

bool validPersonaIdForDeletion(const QString &personaId) {
    if (personaId.isEmpty() || personaId.size() > 240 || personaId != personaId.trimmed()) return false;
    for (const QChar character : personaId) {
        if (character.isNull() || character.isSpace() || character.category() == QChar::Other_Control
            || character == u'/' || character == u'\\') return false;
    }
    return true;
}

QString normalizedMessageText(const QString &text) {
    return text.left(kMaximumStoredMessageCharacters);
}

void logSqlFailure(const char *operation, const QSqlError &error) {
    qWarning().noquote() << "ChatStore" << operation << error.text();
}

bool migrateStructuredFactsSchema(QSqlDatabase &database) {
    QSqlQuery inspect(database);
    if (!inspect.exec(QStringLiteral("SELECT sql FROM sqlite_master WHERE type='table' AND name='global_user_facts'"))) {
        logSqlFailure("inspect global user facts schema", inspect.lastError());
        return false;
    }
    const bool legacySchema = inspect.next()
        && inspect.value(0).toString().contains(QStringLiteral("PRIMARY KEY(scope, fact_key)"));
    inspect.finish();
    if (!legacySchema) {
        QSqlQuery columns(database);
        if (!columns.exec(QStringLiteral("PRAGMA table_info(global_user_facts)"))) return false;
        bool hasUpdateRevision = false;
        bool hasUpdateOrder = false;
        while (columns.next()) {
            const QString name = columns.value(1).toString();
            hasUpdateRevision = hasUpdateRevision || name == QStringLiteral("update_revision");
            hasUpdateOrder = hasUpdateOrder || name == QStringLiteral("update_order");
        }
        QSqlQuery alter(database);
        if (!hasUpdateRevision && !alter.exec(QStringLiteral(
                "ALTER TABLE global_user_facts ADD COLUMN update_revision INTEGER NOT NULL DEFAULT 0")))
            return false;
        if (!hasUpdateOrder && !alter.exec(QStringLiteral(
                "ALTER TABLE global_user_facts ADD COLUMN update_order INTEGER NOT NULL DEFAULT 0")))
            return false;
        return true;
    }
    if (!database.transaction()) return false;
    QSqlQuery query(database);
    bool ok = query.exec(QStringLiteral("ALTER TABLE global_user_facts RENAME TO global_user_facts_legacy"));
    ok = ok && query.exec(QStringLiteral(
        "CREATE TABLE global_user_facts (scope TEXT NOT NULL, fact_key TEXT NOT NULL, stable_id TEXT NOT NULL, "
        "category TEXT NOT NULL, value TEXT NOT NULL, confidence REAL NOT NULL DEFAULT 0, "
        "source_message_id TEXT NOT NULL DEFAULT '', first_confirmed_at INTEGER NOT NULL, "
        "last_confirmed_at INTEGER NOT NULL, status TEXT NOT NULL DEFAULT 'active' "
        "CHECK(status IN ('active','superseded')), superseded_by TEXT NOT NULL DEFAULT '', "
        "update_revision INTEGER NOT NULL DEFAULT 0, update_order INTEGER NOT NULL DEFAULT 0, "
        "PRIMARY KEY(scope, stable_id))"));
    ok = ok && query.exec(QStringLiteral(
        "INSERT INTO global_user_facts(scope, fact_key, stable_id, category, value, confidence, source_message_id, "
        "first_confirmed_at, last_confirmed_at, status, superseded_by) SELECT scope, fact_key, stable_id, category, value, confidence, "
        "source_message_id, first_confirmed_at, last_confirmed_at, status, superseded_by FROM global_user_facts_legacy"));
    ok = ok && query.exec(QStringLiteral("DROP TABLE global_user_facts_legacy"));
    ok = ok && query.exec(QStringLiteral("CREATE UNIQUE INDEX IF NOT EXISTS global_user_facts_active_key ON global_user_facts(scope, fact_key) WHERE status='active'"));
    if (!ok || !database.commit()) {
        logSqlFailure("migrate global user facts schema", query.lastError());
        database.rollback();
        return false;
    }
    return true;
}

bool repairLegacyMessageTimestamps(QSqlDatabase &database) {
    QSqlQuery marker(database);
    marker.prepare(QStringLiteral("SELECT value FROM storage_meta WHERE key=?"));
    marker.addBindValue(QStringLiteral("v129_message_timestamp_repair"));
    if (!marker.exec()) {
        logSqlFailure("read message timestamp repair marker", marker.lastError());
        return false;
    }
    if (marker.next()) return true;
    marker.finish();

    if (!database.transaction()) return false;

    QSqlQuery seconds(database);
    seconds.prepare(QStringLiteral(
        "UPDATE messages SET created_at=created_at*1000 "
        "WHERE created_at>=? AND created_at<?"));
    seconds.addBindValue(kMinimumPlausibleUnixSeconds);
    seconds.addBindValue(kMinimumPlausibleUnixMilliseconds);
    if (!seconds.exec()) {
        logSqlFailure("normalize second message timestamps", seconds.lastError());
        database.rollback();
        return false;
    }

    QSqlQuery personas(database);
    personas.prepare(QStringLiteral(
        "SELECT DISTINCT persona_id FROM messages WHERE created_at<?"));
    personas.addBindValue(kMinimumPlausibleUnixSeconds);
    if (!personas.exec()) {
        logSqlFailure("find invalid message timestamps", personas.lastError());
        database.rollback();
        return false;
    }
    QStringList personaIds;
    while (personas.next()) personaIds.append(personas.value(0).toString());
    personas.finish();

    for (const QString &personaId : personaIds) {
        QSqlQuery invalid(database);
        invalid.prepare(QStringLiteral(
            "SELECT rowid FROM messages WHERE persona_id=? AND created_at<? "
            "ORDER BY created_at ASC, rowid ASC"));
        invalid.addBindValue(personaId);
        invalid.addBindValue(kMinimumPlausibleUnixSeconds);
        if (!invalid.exec()) {
            logSqlFailure("read invalid message timestamps", invalid.lastError());
            database.rollback();
            return false;
        }
        QList<qint64> rowIds;
        while (invalid.next()) rowIds.append(invalid.value(0).toLongLong());
        invalid.finish();

        QSqlQuery latest(database);
        latest.prepare(QStringLiteral(
            "SELECT COALESCE(MAX(created_at), -1) FROM messages "
            "WHERE persona_id=? AND created_at>=?"));
        latest.addBindValue(personaId);
        latest.addBindValue(kMinimumPlausibleUnixMilliseconds);
        if (!latest.exec() || !latest.next()) {
            logSqlFailure("read latest valid message timestamp", latest.lastError());
            database.rollback();
            return false;
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 timestamp = qMax(
            latest.value(0).toLongLong(),
            now - qint64(rowIds.size()));
        latest.finish();

        QSqlQuery update(database);
        update.prepare(QStringLiteral("UPDATE messages SET created_at=? WHERE rowid=?"));
        for (const qint64 rowId : rowIds) {
            if (timestamp == std::numeric_limits<qint64>::max()) {
                database.rollback();
                return false;
            }
            ++timestamp;
            update.bindValue(0, timestamp);
            update.bindValue(1, rowId);
            if (!update.exec()) {
                logSqlFailure("repair invalid message timestamp", update.lastError());
                database.rollback();
                return false;
            }
        }
    }

    QSqlQuery writeMarker(database);
    writeMarker.prepare(QStringLiteral(
        "INSERT INTO storage_meta(key, value) VALUES(?, ?)"));
    writeMarker.addBindValue(QStringLiteral("v129_message_timestamp_repair"));
    writeMarker.addBindValue(QStringLiteral("complete"));
    if (!writeMarker.exec() || !database.commit()) {
        logSqlFailure("write message timestamp repair marker", writeMarker.lastError());
        database.rollback();
        return false;
    }
    return true;
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
            "CREATE TABLE IF NOT EXISTS global_memory ("
            "scope TEXT PRIMARY KEY NOT NULL, memory TEXT NOT NULL DEFAULT '', "
            "memory_revision INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL)"))) {
        logSqlFailure("create global_memory", schema.lastError());
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
            "CREATE TABLE IF NOT EXISTS global_user_facts ("
            "scope TEXT NOT NULL, fact_key TEXT NOT NULL, stable_id TEXT NOT NULL, "
            "category TEXT NOT NULL, value TEXT NOT NULL, confidence REAL NOT NULL DEFAULT 0, "
            "source_message_id TEXT NOT NULL DEFAULT '', first_confirmed_at INTEGER NOT NULL, "
            "last_confirmed_at INTEGER NOT NULL, status TEXT NOT NULL DEFAULT 'active' "
            "CHECK(status IN ('active','superseded')), superseded_by TEXT NOT NULL DEFAULT '', "
            "update_revision INTEGER NOT NULL DEFAULT 0, update_order INTEGER NOT NULL DEFAULT 0, "
            "PRIMARY KEY(scope, stable_id))"))) {
        logSqlFailure("create global user facts", schema.lastError());
        return false;
    }
    if (!schema.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS structured_memory_meta ("
            "scope TEXT PRIMARY KEY NOT NULL, revision INTEGER NOT NULL DEFAULT 0, "
            "updated_at INTEGER NOT NULL)"))) {
        logSqlFailure("create structured memory meta", schema.lastError());
        return false;
    }
    if (!migrateStructuredFactsSchema(database)) return false;
    if (!schema.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS global_user_facts_active_key "
            "ON global_user_facts(scope, fact_key) WHERE status='active'"))
        || !schema.exec(QStringLiteral(
            "CREATE INDEX IF NOT EXISTS global_user_facts_scope_status "
            "ON global_user_facts(scope, status, last_confirmed_at)"))) {
        logSqlFailure("create global user facts index", schema.lastError());
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
    if (!repairLegacyMessageTimestamps(database)) {
        qWarning() << "ChatStore will retry message timestamp repair on the next launch";
        return false;
    }
    return true;
}

bool ChatStore::updateGlobalMemory(const QString &globalMemoryMarkdown, int globalMemoryRevision) {
    if (!ensureOpen()) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO global_memory(scope, memory, memory_revision, updated_at) "
        "VALUES(?, ?, ?, ?) ON CONFLICT(scope) DO UPDATE SET "
        "memory=excluded.memory, memory_revision=excluded.memory_revision, updated_at=excluded.updated_at "
        "WHERE excluded.memory_revision > global_memory.memory_revision OR "
        "(excluded.memory_revision=global_memory.memory_revision AND excluded.memory=global_memory.memory)"));
    query.addBindValue(kGlobalMemoryScope);
    query.addBindValue(globalMemoryMarkdown.left(kMaximumStoredMemoryCharacters));
    query.addBindValue(qMax(0, globalMemoryRevision));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        logSqlFailure("update global memory", query.lastError());
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool ChatStore::compareAndSwapGlobalMemory(const QString &globalMemoryMarkdown,
                                           int expectedRevision, int newRevision) {
    if (!ensureOpen() || newRevision <= expectedRevision) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO global_memory(scope, memory, memory_revision, updated_at) "
        "SELECT ?, ?, ?, ? WHERE ?=0 OR EXISTS(" 
        "SELECT 1 FROM global_memory WHERE scope=? AND memory_revision=?) "
        "ON CONFLICT(scope) DO UPDATE SET memory=excluded.memory, "
        "memory_revision=excluded.memory_revision, updated_at=excluded.updated_at "
        "WHERE global_memory.memory_revision=? AND excluded.memory_revision>global_memory.memory_revision"));
    query.addBindValue(kGlobalMemoryScope);
    query.addBindValue(globalMemoryMarkdown.left(kMaximumStoredMemoryCharacters));
    query.addBindValue(newRevision);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(expectedRevision);
    query.addBindValue(kGlobalMemoryScope);
    query.addBindValue(expectedRevision);
    query.addBindValue(expectedRevision);
    if (!query.exec()) {
        logSqlFailure("compare and swap global memory", query.lastError());
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool ChatStore::applyStructuredFactUpdates(const QJsonArray &updates, int expectedRevision,
                                           int newRevision, int retriesRemaining) {
    if (!ensureOpen() || newRevision <= expectedRevision || updates.size() > kMaximumStructuredFacts)
        return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    int currentExpectedRevision = expectedRevision;
    int currentNewRevision = newRevision;
    bool revisionAdvanced = false;
    for (int attempt = 0; attempt <= retriesRemaining; ++attempt) {
        if (!database.transaction()) {
            logSqlFailure("begin structured fact update", database.lastError());
            return false;
        }
        {
            QSqlQuery revision(database);
            revision.prepare(QStringLiteral(
                "INSERT INTO structured_memory_meta(scope, revision, updated_at) "
                "SELECT ?, ?, ? WHERE ?=0 OR EXISTS(" 
                "SELECT 1 FROM structured_memory_meta WHERE scope=? AND revision=?) "
                "ON CONFLICT(scope) DO UPDATE SET revision=excluded.revision, updated_at=excluded.updated_at "
                "WHERE structured_memory_meta.revision=? AND excluded.revision>structured_memory_meta.revision"));
            revision.addBindValue(kGlobalMemoryScope);
            revision.addBindValue(currentNewRevision);
            revision.addBindValue(QDateTime::currentMSecsSinceEpoch());
            revision.addBindValue(currentExpectedRevision);
            revision.addBindValue(kGlobalMemoryScope);
            revision.addBindValue(currentExpectedRevision);
            revision.addBindValue(currentExpectedRevision);
            if (!revision.exec()) {
                logSqlFailure("advance structured facts revision", revision.lastError());
                database.rollback();
                return false;
            }
            revisionAdvanced = revision.numRowsAffected() == 1;
        }
        if (revisionAdvanced) break;
        database.rollback();
        if (attempt == retriesRemaining) return false;
        QSqlQuery latest(database);
        latest.prepare(QStringLiteral("SELECT revision FROM structured_memory_meta WHERE scope=?"));
        latest.addBindValue(kGlobalMemoryScope);
        if (!latest.exec()) {
            logSqlFailure("reload structured facts revision", latest.lastError());
            return false;
        }
        currentExpectedRevision = latest.next() ? latest.value(0).toInt() : 0;
        currentNewRevision = currentExpectedRevision + 1;
    }

    const int operationRevision = newRevision;
    QSqlQuery upsert(database);
    upsert.prepare(QStringLiteral(
        "INSERT INTO global_user_facts(scope, fact_key, stable_id, category, value, confidence, "
        "source_message_id, first_confirmed_at, last_confirmed_at, status, superseded_by, "
        "update_revision, update_order) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(scope, stable_id) DO UPDATE SET category=excluded.category, value=excluded.value, "
        "confidence=excluded.confidence, source_message_id=excluded.source_message_id, "
        "last_confirmed_at=excluded.last_confirmed_at, status=excluded.status, "
        "superseded_by=excluded.superseded_by, update_revision=excluded.update_revision, "
        "update_order=excluded.update_order WHERE excluded.last_confirmed_at>global_user_facts.last_confirmed_at "
        "OR (excluded.last_confirmed_at=global_user_facts.last_confirmed_at AND "
        "(excluded.update_revision>global_user_facts.update_revision OR "
        "(excluded.update_revision=global_user_facts.update_revision AND "
        "excluded.update_order>=global_user_facts.update_order)))"));
    QSqlQuery statusUpdate(database);
    statusUpdate.prepare(QStringLiteral(
        "UPDATE global_user_facts SET status=?, superseded_by=?, last_confirmed_at=?, "
        "update_revision=?, update_order=? WHERE scope=? AND stable_id=? AND "
        "(? > last_confirmed_at OR (?=last_confirmed_at AND "
        "(? > update_revision OR (?=update_revision AND ?>=update_order))))"));
    QSqlQuery activeForKey(database);
    activeForKey.prepare(QStringLiteral(
        "SELECT stable_id, last_confirmed_at, update_revision, update_order "
        "FROM global_user_facts WHERE scope=? AND fact_key=? AND status='active'"));
    QSqlQuery supersedeActive(database);
    supersedeActive.prepare(QStringLiteral(
        "UPDATE global_user_facts SET status='superseded', superseded_by=?, last_confirmed_at=?, "
        "update_revision=?, update_order=? WHERE scope=? AND fact_key=? AND status='active' "
        "AND stable_id<>? AND (? > last_confirmed_at OR (?=last_confirmed_at AND "
        "(? > update_revision OR (?=update_revision AND ?>=update_order))))"));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int operationOrder = 0; operationOrder < updates.size(); ++operationOrder) {
        const QJsonObject update = updates.at(operationOrder).toObject();
        const QString operation = update.value(QStringLiteral("operation")).toString(
            update.value(QStringLiteral("op")).toString(QStringLiteral("upsert"))).trimmed().toLower();
        const QString key = update.value(QStringLiteral("key")).toString().trimmed().left(240);
        const QString stableId = update.value(QStringLiteral("id")).toString().trimmed().left(240);
        if (key.isEmpty() && stableId.isEmpty()) {
            database.rollback();
            return false;
        }
        QString status = update.value(QStringLiteral("status")).toString(
            operation == QStringLiteral("supersede") ? QStringLiteral("superseded")
                                                     : QStringLiteral("active"));
        if (status != QStringLiteral("active") && status != QStringLiteral("superseded")) {
            database.rollback();
            return false;
        }
        const qint64 confirmedAt = qint64(update.value(QStringLiteral("lastConfirmedAt")).toDouble(now));
        const QString supersededBy = update.value(QStringLiteral("supersededBy")).toString(
            QStringLiteral("")).left(240);
        if (operation == QStringLiteral("supersede") || operation == QStringLiteral("status")) {
            if (stableId.isEmpty()) {
                database.rollback();
                return false;
            }
            statusUpdate.bindValue(0, status);
            statusUpdate.bindValue(1, supersededBy);
            statusUpdate.bindValue(2, confirmedAt);
            statusUpdate.bindValue(3, operationRevision);
            statusUpdate.bindValue(4, operationOrder);
            statusUpdate.bindValue(5, kGlobalMemoryScope);
            statusUpdate.bindValue(6, stableId);
            statusUpdate.bindValue(7, confirmedAt);
            statusUpdate.bindValue(8, confirmedAt);
            statusUpdate.bindValue(9, operationRevision);
            statusUpdate.bindValue(10, operationRevision);
            statusUpdate.bindValue(11, operationOrder);
            if (!statusUpdate.exec()) {
                logSqlFailure("update structured fact status", statusUpdate.lastError());
                database.rollback();
                return false;
            }
            // A stale replay is an idempotent success when a newer state for the
            // same stable identity already exists.
            if (statusUpdate.numRowsAffected() != 1) {
                QSqlQuery exists(database);
                exists.prepare(QStringLiteral(
                    "SELECT 1 FROM global_user_facts WHERE scope=? AND stable_id=? AND "
                    "(last_confirmed_at>? OR (last_confirmed_at=? AND "
                    "(update_revision>? OR (update_revision=? AND update_order>=?))))"));
                exists.addBindValue(kGlobalMemoryScope);
                exists.addBindValue(stableId);
                exists.addBindValue(confirmedAt);
                exists.addBindValue(confirmedAt);
                exists.addBindValue(operationRevision);
                exists.addBindValue(operationRevision);
                exists.addBindValue(operationOrder);
                if (!exists.exec() || !exists.next()) {
                    database.rollback();
                    return false;
                }
            }
            continue;
        }
        if (operation != QStringLiteral("upsert") || key.isEmpty()) {
            database.rollback();
            return false;
        }
        const QString id = stableId.isEmpty() ? key : stableId;
        activeForKey.bindValue(0, kGlobalMemoryScope);
        activeForKey.bindValue(1, key);
        if (!activeForKey.exec()) {
            logSqlFailure("load active structured fact", activeForKey.lastError());
            database.rollback();
            return false;
        }
        QString activeId;
        qint64 activeConfirmedAt = -1;
        int activeRevision = -1;
        int activeOrder = -1;
        if (activeForKey.next()) {
            activeId = activeForKey.value(0).toString();
            activeConfirmedAt = activeForKey.value(1).toLongLong();
            activeRevision = activeForKey.value(2).toInt();
            activeOrder = activeForKey.value(3).toInt();
        }
        const bool newerThanActive = confirmedAt > activeConfirmedAt
            || (confirmedAt == activeConfirmedAt
                && (operationRevision > activeRevision
                    || (operationRevision == activeRevision && operationOrder >= activeOrder)));
        if (!activeId.isEmpty() && activeId != id) {
            if (!newerThanActive) {
                status = QStringLiteral("superseded");
            } else {
                supersedeActive.bindValue(0, id);
                supersedeActive.bindValue(1, confirmedAt);
                supersedeActive.bindValue(2, operationRevision);
                supersedeActive.bindValue(3, operationOrder);
                supersedeActive.bindValue(4, kGlobalMemoryScope);
                supersedeActive.bindValue(5, key);
                supersedeActive.bindValue(6, id);
                supersedeActive.bindValue(7, confirmedAt);
                supersedeActive.bindValue(8, confirmedAt);
                supersedeActive.bindValue(9, operationRevision);
                supersedeActive.bindValue(10, operationRevision);
                supersedeActive.bindValue(11, operationOrder);
                if (!supersedeActive.exec() || supersedeActive.numRowsAffected() != 1) {
                    if (supersedeActive.lastError().isValid())
                        logSqlFailure("supersede active structured fact", supersedeActive.lastError());
                    database.rollback();
                    return false;
                }
            }
        }
        upsert.bindValue(0, kGlobalMemoryScope);
        upsert.bindValue(1, key);
        upsert.bindValue(2, id);
        upsert.bindValue(3, update.value(QStringLiteral("category")).toString().trimmed().left(240));
        upsert.bindValue(4, update.value(QStringLiteral("value")).toVariant().toString().left(kMaximumFactFieldCharacters));
        upsert.bindValue(5, qBound(0.0, update.value(QStringLiteral("confidence")).toDouble(), 1.0));
        upsert.bindValue(6, update.value(QStringLiteral("sourceMessageId")).toString().left(240));
        upsert.bindValue(7, qint64(update.value(QStringLiteral("firstConfirmedAt")).toDouble(confirmedAt)));
        upsert.bindValue(8, confirmedAt);
        upsert.bindValue(9, status);
        upsert.bindValue(10, supersededBy);
        upsert.bindValue(11, operationRevision);
        upsert.bindValue(12, operationOrder);
        if (!upsert.exec()) {
            logSqlFailure("upsert structured fact", upsert.lastError());
            database.rollback();
            return false;
        }
        if (upsert.numRowsAffected() != 1) {
            QSqlQuery newer(database);
            newer.prepare(QStringLiteral(
                "SELECT 1 FROM global_user_facts WHERE scope=? AND stable_id=? AND "
                "(last_confirmed_at>? OR (last_confirmed_at=? AND "
                "(update_revision>? OR (update_revision=? AND update_order>=?))))"));
            newer.addBindValue(kGlobalMemoryScope);
            newer.addBindValue(id);
            newer.addBindValue(confirmedAt);
            newer.addBindValue(confirmedAt);
            newer.addBindValue(operationRevision);
            newer.addBindValue(operationRevision);
            newer.addBindValue(operationOrder);
            if (!newer.exec() || !newer.next()) {
                database.rollback();
                return false;
            }
        }
    }
    if (!database.commit()) {
        logSqlFailure("commit structured fact updates", database.lastError());
        database.rollback();
        return false;
    }
    return true;
}

bool ChatStore::deletePersonaData(const QString &personaId, QString *error) {
    if (!validPersonaIdForDeletion(personaId)) {
        if (error) *error = QStringLiteral("人格 ID 为空、过长或包含非法字符。");
        return false;
    }
    if (!ensureOpen()) {
        if (error) *error = QStringLiteral("聊天数据库不可用，无法确认对话数据已删除。");
        return false;
    }
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) {
        if (error) *error = database.lastError().text();
        return false;
    }
    QSqlQuery remove(database);
    remove.prepare(QStringLiteral("DELETE FROM persona_memory WHERE persona_id=?"));
    remove.addBindValue(personaId);
    if (!remove.exec()) {
        logSqlFailure("delete persona data", remove.lastError());
        if (error) *error = remove.lastError().text();
        database.rollback();
        return false;
    }
    QSqlQuery verify(database);
    verify.prepare(QStringLiteral(
        "SELECT (SELECT COUNT(*) FROM persona_memory WHERE persona_id=?) + "
        "(SELECT COUNT(*) FROM messages WHERE persona_id=?)"));
    verify.addBindValue(personaId);
    verify.addBindValue(personaId);
    if (!verify.exec() || !verify.next() || verify.value(0).toInt() != 0) {
        if (error) *error = verify.lastError().isValid()
            ? verify.lastError().text() : QStringLiteral("删除校验未通过。");
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        if (error) *error = database.lastError().text();
        database.rollback();
        return false;
    }
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

    QSqlQuery global(database);
    global.prepare(QStringLiteral(
        "SELECT memory, memory_revision FROM global_memory WHERE scope=?"));
    global.addBindValue(kGlobalMemoryScope);
    if (!global.exec()) {
        logSqlFailure("load global memory", global.lastError());
        snapshot.storeAvailable = false;
        return snapshot;
    }
    if (global.next()) {
        snapshot.globalMemoryMarkdown = global.value(0).toString().left(kMaximumStoredMemoryCharacters);
        snapshot.globalMemoryRevision = global.value(1).toInt();
    }

    QSqlQuery factsRevision(database);
    factsRevision.prepare(QStringLiteral(
        "SELECT revision FROM structured_memory_meta WHERE scope=?"));
    factsRevision.addBindValue(kGlobalMemoryScope);
    if (!factsRevision.exec()) {
        logSqlFailure("load structured facts revision", factsRevision.lastError());
        snapshot.storeAvailable = false;
        return snapshot;
    }
    if (factsRevision.next()) snapshot.structuredFactsRevision = factsRevision.value(0).toInt();

    QSqlQuery facts(database);
    facts.prepare(QStringLiteral(
        "SELECT stable_id, fact_key, category, value, confidence, source_message_id, "
        "first_confirmed_at, last_confirmed_at, status, superseded_by "
        "FROM global_user_facts WHERE scope=? "
        "ORDER BY status ASC, last_confirmed_at DESC LIMIT ?"));
    facts.addBindValue(kGlobalMemoryScope);
    facts.addBindValue(kMaximumStructuredFacts);
    if (!facts.exec()) {
        logSqlFailure("load structured facts", facts.lastError());
        snapshot.storeAvailable = false;
        return snapshot;
    }
    while (facts.next()) {
        snapshot.structuredFacts.append(QJsonObject{
            {QStringLiteral("id"), facts.value(0).toString()},
            {QStringLiteral("key"), facts.value(1).toString()},
            {QStringLiteral("category"), facts.value(2).toString()},
            {QStringLiteral("value"), facts.value(3).toString()},
            {QStringLiteral("confidence"), facts.value(4).toDouble()},
            {QStringLiteral("sourceMessageId"), facts.value(5).toString()},
            {QStringLiteral("firstConfirmedAt"), facts.value(6).toLongLong()},
            {QStringLiteral("lastConfirmedAt"), facts.value(7).toLongLong()},
            {QStringLiteral("status"), facts.value(8).toString()},
            {QStringLiteral("supersededBy"), facts.value(9).toString()}
        });
    }

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
    snapshot.relationshipMemoryMarkdown = persona.value(0).toString().left(kMaximumStoredMemoryCharacters);
    snapshot.relationshipMemoryRevision = persona.value(1).toInt();

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
        "INSERT INTO messages(id, persona_id, role, text, created_at) VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET role=excluded.role, text=excluded.text, "
        "created_at=excluded.created_at WHERE messages.persona_id=excluded.persona_id"));
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
        insert.bindValue(4, normalizedMessageTimestamp(
                                message.value(QStringLiteral("at")),
                                QDateTime::currentMSecsSinceEpoch()));
        if (!insert.exec() || insert.numRowsAffected() != 1) {
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
                                const QJsonArray &messages, const QString &relationshipMemoryMarkdown,
                                int relationshipMemoryRevision) {
    if (!ensureOpen()) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) return false;
    QSqlQuery persona(database);
    persona.prepare(QStringLiteral(
        "INSERT INTO persona_memory(persona_id, persona_name, long_memory, memory_revision, updated_at) "
        "VALUES(?, ?, ?, ?, ?) ON CONFLICT(persona_id) DO UPDATE SET "
        "persona_name=excluded.persona_name, long_memory=excluded.long_memory, "
        "memory_revision=excluded.memory_revision, updated_at=excluded.updated_at "
        "WHERE excluded.memory_revision > persona_memory.memory_revision OR "
        "(excluded.memory_revision=persona_memory.memory_revision "
        "AND excluded.long_memory=persona_memory.long_memory)"));
    persona.addBindValue(normalizedPersonaId(personaId));
    persona.addBindValue(personaName.trimmed().left(240));
    persona.addBindValue(relationshipMemoryMarkdown.left(kMaximumStoredMemoryCharacters));
    persona.addBindValue(qMax(0, relationshipMemoryRevision));
    persona.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!persona.exec() || persona.numRowsAffected() != 1) {
        if (persona.lastError().isValid())
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

bool ChatStore::importMessages(const QString &personaId, const QString &personaName,
                               const QJsonArray &messages, int *importedCount, QString *error) {
    if (importedCount) *importedCount = 0;
    if (!ensureOpen()) {
        if (error) *error = QStringLiteral("聊天数据库不可用。");
        return false;
    }
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    if (!database.transaction()) {
        if (error) *error = database.lastError().text();
        return false;
    }

    const QString id = normalizedPersonaId(personaId);
    QJsonArray pendingMessages;
    QSet<QString> pendingIds;
    qint64 importCharacters = 0;
    QSqlQuery existing(database);
    existing.prepare(QStringLiteral("SELECT persona_id, role, text FROM messages WHERE id=?"));
    for (const QJsonValue &value : messages) {
        const QJsonObject message = value.toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        const QString text = normalizedMessageText(message.value(QStringLiteral("text")).toString());
        if ((role != QStringLiteral("user") && role != QStringLiteral("assistant")) || text.isEmpty()) continue;

        QString messageId = message.value(QStringLiteral("id")).toString().trimmed().left(240);
        bool occupied = false;
        if (!messageId.isEmpty()) {
            existing.bindValue(0, messageId);
            if (!existing.exec()) {
                if (error) *error = existing.lastError().text();
                database.rollback();
                return false;
            }
            if (existing.next()) {
                if (existing.value(0).toString() == id
                    && existing.value(1).toString() == role
                    && existing.value(2).toString() == text) {
                    existing.finish();
                    continue;
                }
                occupied = true;
            }
            existing.finish();
            occupied = occupied || pendingIds.contains(messageId);
        }
        if (messageId.isEmpty() || occupied) {
            do {
                messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                existing.bindValue(0, messageId);
                if (!existing.exec()) {
                    if (error) *error = existing.lastError().text();
                    database.rollback();
                    return false;
                }
                occupied = existing.next() || pendingIds.contains(messageId);
                existing.finish();
            } while (occupied);
        }
        pendingMessages.append(QJsonObject{
            {QStringLiteral("id"), messageId},
            {QStringLiteral("role"), role},
            {QStringLiteral("text"), text}
        });
        pendingIds.insert(messageId);
        importCharacters += text.size();
    }

    QSqlQuery capacity(database);
    capacity.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(length(text)), 0) FROM messages WHERE persona_id=?"));
    capacity.addBindValue(id);
    if (!capacity.exec() || !capacity.next()) {
        if (error) *error = capacity.lastError().text();
        database.rollback();
        return false;
    }
    if (capacity.value(0).toInt() + pendingMessages.size() > kMaximumStoredMessagesPerPersona
        || capacity.value(1).toLongLong() + importCharacters > kMaximumStoredCharactersPerPersona) {
        if (error) *error = QStringLiteral("现有聊天记录与本次导入合计超过容量上限，请先删除部分聊天后再导入。");
        database.rollback();
        return false;
    }
    if (pendingMessages.isEmpty()) {
        if (!database.commit()) {
            if (error) *error = database.lastError().text();
            database.rollback();
            return false;
        }
        return true;
    }

    QSqlQuery persona(database);
    persona.prepare(QStringLiteral(
        "INSERT INTO persona_memory(persona_id, persona_name, long_memory, memory_revision, updated_at) "
        "VALUES(?, ?, '', 0, ?) ON CONFLICT(persona_id) DO UPDATE SET "
        "persona_name=excluded.persona_name, updated_at=excluded.updated_at"));
    persona.addBindValue(id);
    persona.addBindValue(personaName.trimmed().left(240));
    persona.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!persona.exec()) {
        logSqlFailure("ensure import persona", persona.lastError());
        if (error) *error = persona.lastError().text();
        database.rollback();
        return false;
    }

    QSqlQuery latest(database);
    latest.prepare(QStringLiteral("SELECT COALESCE(MAX(created_at), -1) FROM messages WHERE persona_id=?"));
    latest.addBindValue(id);
    if (!latest.exec() || !latest.next()) {
        if (error) *error = latest.lastError().text();
        database.rollback();
        return false;
    }
    qint64 latestTimestamp = latest.value(0).toLongLong();
    if (latestTimestamp < kMinimumPlausibleUnixMilliseconds) {
        latestTimestamp = QDateTime::currentMSecsSinceEpoch()
            - qint64(pendingMessages.size());
    }

    QSqlQuery insert(database);
    insert.prepare(QStringLiteral(
        "INSERT INTO messages(id, persona_id, role, text, created_at) VALUES(?, ?, ?, ?, ?)"));
    int count = 0;
    for (const QJsonValue &value : pendingMessages) {
        const QJsonObject message = value.toObject();
        if (latestTimestamp == std::numeric_limits<qint64>::max()) {
            if (error) *error = QStringLiteral("当前聊天记录时间戳已达到上限，无法继续严格追加。");
            database.rollback();
            return false;
        }
        ++latestTimestamp;
        insert.bindValue(0, message.value(QStringLiteral("id")).toString());
        insert.bindValue(1, id);
        insert.bindValue(2, message.value(QStringLiteral("role")).toString());
        insert.bindValue(3, message.value(QStringLiteral("text")).toString());
        insert.bindValue(4, latestTimestamp);
        if (!insert.exec()) {
            logSqlFailure("import message", insert.lastError());
            if (error) *error = insert.lastError().text();
            database.rollback();
            return false;
        }
        ++count;
    }
    if (!pruneMessages(id) || !database.commit()) {
        if (error) *error = database.lastError().text();
        database.rollback();
        return false;
    }
    if (importedCount) *importedCount = count;
    return true;
}

bool ChatStore::compareAndSwapRelationshipMemory(const QString &personaId, const QString &personaName,
                                                 const QString &relationshipMemoryMarkdown,
                                                 int expectedRevision, int newRevision) {
    if (!ensureOpen() || newRevision <= expectedRevision) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO persona_memory(persona_id, persona_name, long_memory, memory_revision, updated_at) "
        "SELECT ?, ?, ?, ?, ? WHERE ?=0 OR EXISTS(" 
        "SELECT 1 FROM persona_memory WHERE persona_id=? AND memory_revision=?) "
        "ON CONFLICT(persona_id) DO UPDATE SET persona_name=excluded.persona_name, "
        "long_memory=excluded.long_memory, memory_revision=excluded.memory_revision, "
        "updated_at=excluded.updated_at WHERE persona_memory.memory_revision=? "
        "AND excluded.memory_revision>persona_memory.memory_revision"));
    const QString id = normalizedPersonaId(personaId);
    query.addBindValue(id);
    query.addBindValue(personaName.trimmed().left(240));
    query.addBindValue(relationshipMemoryMarkdown.left(kMaximumStoredMemoryCharacters));
    query.addBindValue(newRevision);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(expectedRevision);
    query.addBindValue(id);
    query.addBindValue(expectedRevision);
    query.addBindValue(expectedRevision);
    if (!query.exec()) {
        logSqlFailure("compare and swap relationship memory", query.lastError());
        return false;
    }
    return query.numRowsAffected() == 1;
}

bool ChatStore::updateRelationshipMemory(const QString &personaId, const QString &personaName,
                                         const QString &relationshipMemoryMarkdown,
                                         int relationshipMemoryRevision) {
    if (!ensureOpen()) return false;
    QSqlDatabase database = QSqlDatabase::database(m_connectionName);
    QSqlQuery query(database);
    query.prepare(QStringLiteral(
        "INSERT INTO persona_memory(persona_id, persona_name, long_memory, memory_revision, updated_at) "
        "VALUES(?, ?, ?, ?, ?) ON CONFLICT(persona_id) DO UPDATE SET "
        "persona_name=excluded.persona_name, long_memory=excluded.long_memory, "
        "memory_revision=excluded.memory_revision, updated_at=excluded.updated_at "
        "WHERE excluded.memory_revision > persona_memory.memory_revision OR "
        "(excluded.memory_revision=persona_memory.memory_revision "
        "AND excluded.long_memory=persona_memory.long_memory)"));
    query.addBindValue(normalizedPersonaId(personaId));
    query.addBindValue(personaName.trimmed().left(240));
    query.addBindValue(relationshipMemoryMarkdown.left(kMaximumStoredMemoryCharacters));
    query.addBindValue(qMax(0, relationshipMemoryRevision));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        logSqlFailure("update memory", query.lastError());
        return false;
    }
    return query.numRowsAffected() == 1;
}
