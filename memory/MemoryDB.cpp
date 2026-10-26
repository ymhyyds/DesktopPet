#include "MemoryDB.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QUuid>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <algorithm>

namespace {

/// NOT NULL 文本列兜底：QString 默认构造是 null，直接 bind 会变成 SQL NULL 触发约束失败。
QString orEmpty(const QString &s)
{
    return s.isNull() ? QStringLiteral("") : s;
}

/// 从自然语言里挑出检索词：CJK 连续段取 bigram（两字），拉丁/数字段整体保留。
QStringList searchTerms(const QString &query)
{
    QStringList terms;
    QString current;
    const auto flush = [&]() {
        if (current.isEmpty()) return;
        if (current.size() <= 2) {
            terms.append(current);
        } else {
            for (int i = 0; i + 2 <= current.size(); ++i) {
                terms.append(current.mid(i, 2));
            }
        }
        current.clear();
    };

    for (const QChar &ch : query) {
        if (ch.isLetterOrNumber()) {
            current.append(ch);
        } else {
            flush();
        }
    }
    flush();

    QStringList unique;
    for (const QString &t : terms) {
        if (t.size() >= 2 && !unique.contains(t)) {
            unique.append(t);
        }
        if (unique.size() >= 16) break;
    }
    return unique;
}

MemoryRecord recordFromQuery(const QSqlQuery &q)
{
    MemoryRecord r;
    r.id              = q.value(0).toInt();
    r.characterName   = q.value(1).toString();
    r.kind            = q.value(2).toString();
    r.subject         = q.value(3).toString();
    r.content         = q.value(4).toString();
    r.keywords        = q.value(5).toString();
    r.importance      = q.value(6).toInt();
    r.confidence      = q.value(7).toDouble();
    r.sourceMessageId = q.value(8).toInt();
    r.createdAt       = q.value(9).toString();
    r.lastHitAt       = q.value(10).toString();
    r.hitCount        = q.value(11).toInt();
    r.status          = q.value(12).toString();
    return r;
}

const char *kMemorySelectColumns =
    "id, character_name, kind, subject, content, keywords, importance, confidence, "
    "source_message_id, created_at, last_hit_at, hit_count, status";

} // namespace

QString MemoryDB::connectionName() const
{
    // 用随机后缀避免多个 MemoryDB 实例（比如单测里）互相抢同一个默认连接名。
    static const QString suffix = QUuid::createUuid().toString(QUuid::Id128);
    return QStringLiteral("DesktopPetMemoryConnection_") + suffix;
}

bool MemoryDB::open(const QString &dbFilePath)
{
    // 确保数据库文件所在目录存在（比如 database/ 目录可能还没创建）
    const QFileInfo fileInfo(dbFilePath);
    QDir dir = fileInfo.dir();
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName());
    m_db.setDatabaseName(dbFilePath);

    if (!m_db.open()) {
        qWarning() << "[MemoryDB] 数据库打开失败：" << m_db.lastError().text();
        m_opened = false;
        return false;
    }

    m_opened = ensureSchema();
    return m_opened;
}

bool MemoryDB::ensureSchema()
{
    QSqlQuery query(m_db);

    const bool usersOk = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY,"
        "  name TEXT,"
        "  created_at TEXT NOT NULL"
        ")"
    ));
    if (!usersOk) {
        qWarning() << "[MemoryDB] 创建 users 表失败：" << query.lastError().text();
        return false;
    }

    const bool messagesOk = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  character_name TEXT NOT NULL,"
        "  role TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at TEXT NOT NULL"
        ")"
    ));
    if (!messagesOk) {
        qWarning() << "[MemoryDB] 创建 messages 表失败：" << query.lastError().text();
        return false;
    }

    // 给常用查询字段建索引，避免历史记录变多之后 loadHistory 变慢
    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_messages_character ON messages(character_name, id)"
    ));

    const bool preferencesOk = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS preferences ("
        "  key TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL,"
        "  updated_at TEXT NOT NULL"
        ")"
    ));
    if (!preferencesOk) {
        qWarning() << "[MemoryDB] 创建 preferences 表失败：" << query.lastError().text();
        return false;
    }

    // ──── 长期记忆：user_version 0 → 1 迁移 ────
    const int version = schemaVersion();
    if (version < 1 && !migrateToV1()) {
        return false;
    }

    // 迁移后确认 FTS 表是否真的可用（旧 qsqlite 可能没有编译 fts5）
    QSqlQuery ftsCheck(m_db);
    if (ftsCheck.exec(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type='table' AND name='memories_fts'"))
        && ftsCheck.next()) {
        m_ftsAvailable = true;
    } else {
        m_ftsAvailable = false;
        qWarning() << "[MemoryDB] memories_fts 不可用，检索回退到 LIKE";
    }

    return true;
}

int MemoryDB::schemaVersion() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("PRAGMA user_version")) || !query.next()) {
        qWarning() << "[MemoryDB] 读取 user_version 失败：" << query.lastError().text();
        return -1;
    }
    return query.value(0).toInt();
}

bool MemoryDB::migrateToV1()
{
    if (!m_db.transaction()) {
        qWarning() << "[MemoryDB] 记忆迁移开启事务失败：" << m_db.lastError().text();
        return false;
    }

    QSqlQuery query(m_db);

    const bool tableOk = query.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS memories ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  character_name TEXT NOT NULL,"
        "  kind TEXT NOT NULL,"
        "  subject TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  keywords TEXT NOT NULL,"
        "  importance INTEGER NOT NULL DEFAULT 3,"
        "  confidence REAL NOT NULL DEFAULT 1.0,"
        "  source_message_id INTEGER,"
        "  created_at TEXT NOT NULL,"
        "  last_hit_at TEXT,"
        "  hit_count INTEGER NOT NULL DEFAULT 0,"
        "  status TEXT NOT NULL DEFAULT 'active'"
        ")"
    ));
    if (!tableOk) {
        qWarning() << "[MemoryDB] 创建 memories 表失败：" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_mem_char ON memories(character_name, status, importance)"));
    query.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_mem_kind ON memories(character_name, kind)"));

    // FTS5 trigram 外部内容表：查询失败不致命，检索会回退到 LIKE
    const bool ftsOk = query.exec(QStringLiteral(
        "CREATE VIRTUAL TABLE IF NOT EXISTS memories_fts USING fts5("
        "  content, keywords,"
        "  content='memories', content_rowid='id',"
        "  tokenize='trigram'"
        ")"
    ));
    if (!ftsOk) {
        qWarning() << "[MemoryDB] 创建 memories_fts 失败（将只用 LIKE 检索）："
                   << query.lastError().text();
    } else {
        // 外部内容表三件套：缺了触发器会让 FTS 与主表不一致
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS memories_ai AFTER INSERT ON memories BEGIN "
            "  INSERT INTO memories_fts(rowid, content, keywords) "
            "  VALUES (new.id, new.content, new.keywords);"
            "END"));
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS memories_ad AFTER DELETE ON memories BEGIN "
            "  INSERT INTO memories_fts(memories_fts, rowid, content, keywords) "
            "  VALUES('delete', old.id, old.content, old.keywords);"
            "END"));
        query.exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS memories_au AFTER UPDATE ON memories BEGIN "
            "  INSERT INTO memories_fts(memories_fts, rowid, content, keywords) "
            "  VALUES('delete', old.id, old.content, old.keywords);"
            "  INSERT INTO memories_fts(rowid, content, keywords) "
            "  VALUES (new.id, new.content, new.keywords);"
            "END"));
        // 若 memories 里已有数据（理论上没有），把索引重建一遍
        query.exec(QStringLiteral(
            "INSERT INTO memories_fts(memories_fts) VALUES('rebuild')"));
    }

    if (!query.exec(QStringLiteral("PRAGMA user_version = 1"))) {
        qWarning() << "[MemoryDB] 写入 user_version 失败：" << query.lastError().text();
        m_db.rollback();
        return false;
    }

    if (!m_db.commit()) {
        qWarning() << "[MemoryDB] 记忆迁移提交失败：" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    qInfo() << "[MemoryDB] 数据库已迁移至 user_version=1（memories + FTS5）";
    return true;
}

MemoryDB::~MemoryDB()
{
    close();
}

void MemoryDB::close()
{
    if (m_opened && m_db.isOpen()) {
        m_db.close();
    }
    m_opened = false;
}

bool MemoryDB::isOpen() const
{
    return m_opened && m_db.isOpen();
}

bool MemoryDB::saveMessage(const QString &characterName, const QString &role, const QString &content)
{
    if (!isOpen()) {
        qWarning() << "[MemoryDB] 数据库未打开，saveMessage 被忽略";
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO messages (character_name, role, content, created_at) "
        "VALUES (:character_name, :role, :content, :created_at)"
    ));
    query.bindValue(QStringLiteral(":character_name"), characterName);
    query.bindValue(QStringLiteral(":role"), role);
    query.bindValue(QStringLiteral(":content"), content);
    query.bindValue(QStringLiteral(":created_at"), QDateTime::currentDateTime().toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] saveMessage 失败：" << query.lastError().text();
        return false;
    }
    return true;
}

QList<MessageRecord> MemoryDB::loadHistory(const QString &characterName, int limit) const
{
    QList<MessageRecord> result;
    if (!isOpen()) {
        return result;
    }

    // 先按 id 倒序取最近 limit 条，再在 C++ 侧反转成时间正序，
    // 这样聊天窗口可以直接按返回顺序从上到下渲染。
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT role, content, created_at FROM messages "
        "WHERE character_name = :character_name "
        "ORDER BY id DESC LIMIT :limit"
    ));
    query.bindValue(QStringLiteral(":character_name"), characterName);
    query.bindValue(QStringLiteral(":limit"), limit);

    if (!query.exec()) {
        qWarning() << "[MemoryDB] loadHistory 失败：" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        MessageRecord record;
        record.role      = query.value(0).toString();
        record.content   = query.value(1).toString();
        record.timestamp = query.value(2).toString();
        result.prepend(record); // prepend 实现反转，最终是时间正序
    }

    return result;
}

QList<MessageRecord> MemoryDB::searchMessages(const QString &keyword, const QString &characterName) const
{
    QList<MessageRecord> result;
    if (!isOpen() || keyword.isEmpty()) return result;

    QSqlQuery query(m_db);
    const QString likePattern = QStringLiteral("%%1%").arg(keyword);

    if (characterName.isEmpty()) {
        query.prepare(QStringLiteral(
            "SELECT role, content, created_at FROM messages "
            "WHERE content LIKE :keyword "
            "ORDER BY id DESC"
        ));
    } else {
        query.prepare(QStringLiteral(
            "SELECT role, content, created_at FROM messages "
            "WHERE character_name = :char AND content LIKE :keyword "
            "ORDER BY id DESC"
        ));
        query.bindValue(QStringLiteral(":char"), characterName);
    }
    query.bindValue(QStringLiteral(":keyword"), likePattern);

    if (!query.exec()) {
        qWarning() << "[MemoryDB] searchMessages 失败：" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        MessageRecord rec;
        rec.role      = query.value(0).toString();
        rec.content   = query.value(1).toString();
        rec.timestamp = query.value(2).toString();
        result.append(rec);
    }

    qInfo() << "[MemoryDB] 搜索\"" << keyword << "\" 找到" << result.size() << "条结果";
    return result;
}

QList<MessageRecord> MemoryDB::loadAllMessages(const QString &characterName) const
{
    QList<MessageRecord> result;
    if (!isOpen()) return result;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT role, content, created_at FROM messages "
        "WHERE character_name = :char "
        "ORDER BY id ASC"
    ));
    query.bindValue(QStringLiteral(":char"), characterName);

    if (!query.exec()) {
        qWarning() << "[MemoryDB] loadAllMessages 失败：" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        MessageRecord rec;
        rec.role      = query.value(0).toString();
        rec.content   = query.value(1).toString();
        rec.timestamp = query.value(2).toString();
        result.append(rec);
    }
    return result;
}

bool MemoryDB::clearMessages(const QString &characterName)
{
    if (!isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM messages WHERE character_name = :char"
    ));
    query.bindValue(QStringLiteral(":char"), characterName);

    if (!query.exec()) {
        qWarning() << "[MemoryDB] clearMessages 失败：" << query.lastError().text();
        return false;
    }
    qInfo() << "[MemoryDB] 已清除" << characterName << "的全部聊天记录";
    return true;
}

int MemoryDB::messageCount(const QString &characterName) const
{
    if (!isOpen()) return 0;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM messages WHERE character_name = :char"
    ));
    query.bindValue(QStringLiteral(":char"), characterName);

    if (!query.exec() || !query.next()) {
        qWarning() << "[MemoryDB] messageCount 失败：" << query.lastError().text();
        return 0;
    }
    return query.value(0).toInt();
}

int MemoryDB::mergeCharacterHistory(const QString &from, const QString &to)
{
    if (!isOpen()) {
        qWarning() << "[MemoryDB] 数据库未打开，mergeCharacterHistory 被忽略";
        return -1;
    }
    if (from.isEmpty() || to.isEmpty() || from == to) {
        return -1;
    }

    if (!m_db.transaction()) {
        qWarning() << "[MemoryDB] mergeCharacterHistory 开启事务失败："
                   << m_db.lastError().text();
        return -1;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE messages SET character_name = :to WHERE character_name = :from"
    ));
    query.bindValue(QStringLiteral(":to"), to);
    query.bindValue(QStringLiteral(":from"), from);

    if (!query.exec()) {
        qWarning() << "[MemoryDB] mergeCharacterHistory 失败：" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    const int affected = query.numRowsAffected();

    if (!m_db.commit()) {
        qWarning() << "[MemoryDB] mergeCharacterHistory 提交失败：" << m_db.lastError().text();
        m_db.rollback();
        return -1;
    }

    qInfo() << "[MemoryDB] 历史合并：" << from << "→" << to << "共" << affected << "条";
    return affected;
}

// ═══════════════════════════════════════════════════════════════════════════
// 长期记忆（memories 表）
// ═══════════════════════════════════════════════════════════════════════════

namespace {

QString normalizedNow()
{
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

/// 角色可见范围：'*' 共享画像 + 指定角色。characterName 为空时不过滤。
QString characterScopeSql(const QString &characterName)
{
    return characterName.isEmpty()
        ? QString()
        : QStringLiteral(" AND character_name IN ('*', :char)");
}

} // namespace

int MemoryDB::addMemory(const MemoryRecord &m)
{
    if (!isOpen()) {
        qWarning() << "[MemoryDB] 数据库未打开，addMemory 被忽略";
        return -1;
    }
    const QString content = m.content.trimmed();
    if (content.isEmpty() || m.characterName.trimmed().isEmpty()) {
        return -1;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO memories (character_name, kind, subject, content, keywords, "
        "importance, confidence, source_message_id, created_at, status) "
        "VALUES (:character_name, :kind, :subject, :content, :keywords, "
        ":importance, :confidence, :source_message_id, :created_at, :status)"
    ));
    query.bindValue(QStringLiteral(":character_name"), m.characterName);
    query.bindValue(QStringLiteral(":kind"), orEmpty(m.kind));
    query.bindValue(QStringLiteral(":subject"), orEmpty(m.subject));
    query.bindValue(QStringLiteral(":content"), content);
    query.bindValue(QStringLiteral(":keywords"), orEmpty(m.keywords.trimmed()));
    query.bindValue(QStringLiteral(":importance"), qBound(1, m.importance, 5));
    query.bindValue(QStringLiteral(":confidence"), qBound(0.0, m.confidence, 1.0));
    query.bindValue(QStringLiteral(":source_message_id"),
                    m.sourceMessageId > 0 ? m.sourceMessageId : QVariant());
    query.bindValue(QStringLiteral(":created_at"),
                    m.createdAt.isEmpty() ? normalizedNow() : m.createdAt);
    query.bindValue(QStringLiteral(":status"),
                    m.status.isEmpty() ? QStringLiteral("active") : m.status);

    if (!query.exec()) {
        qWarning() << "[MemoryDB] addMemory 失败：" << query.lastError().text();
        return -1;
    }
    return query.lastInsertId().toInt();
}

bool MemoryDB::updateMemory(const MemoryRecord &m)
{
    if (!isOpen() || m.id <= 0 || m.content.trimmed().isEmpty()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE memories SET content = :content, keywords = :keywords, "
        "kind = :kind, subject = :subject, importance = :importance, "
        "confidence = :confidence, status = :status WHERE id = :id"
    ));
    query.bindValue(QStringLiteral(":content"), m.content.trimmed());
    query.bindValue(QStringLiteral(":keywords"), orEmpty(m.keywords.trimmed()));
    query.bindValue(QStringLiteral(":kind"), orEmpty(m.kind));
    query.bindValue(QStringLiteral(":subject"), orEmpty(m.subject));
    query.bindValue(QStringLiteral(":importance"), qBound(1, m.importance, 5));
    query.bindValue(QStringLiteral(":confidence"), qBound(0.0, m.confidence, 1.0));
    query.bindValue(QStringLiteral(":status"),
                    m.status.isEmpty() ? QStringLiteral("active") : m.status);
    query.bindValue(QStringLiteral(":id"), m.id);

    if (!query.exec()) {
        qWarning() << "[MemoryDB] updateMemory 失败：" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool MemoryDB::deleteMemory(int id)
{
    if (!isOpen() || id <= 0) return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM memories WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        qWarning() << "[MemoryDB] deleteMemory 失败：" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool MemoryDB::setMemoryStatus(int id, const QString &status)
{
    if (!isOpen() || id <= 0) return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE memories SET status = :status WHERE id = :id"));
    query.bindValue(QStringLiteral(":status"), status);
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec() && query.numRowsAffected() > 0;
}

bool MemoryDB::memoryById(int id, MemoryRecord *out) const
{
    if (!isOpen() || id <= 0 || !out) return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT %1 FROM memories WHERE id = :id")
                      .arg(QString::fromLatin1(kMemorySelectColumns)));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next()) {
        return false;
    }
    *out = recordFromQuery(query);
    return true;
}

QList<MemoryRecord> MemoryDB::listMemories(const QString &characterName,
                                           const QString &keyword, int limit) const
{
    QList<MemoryRecord> result;
    if (!isOpen()) return result;

    QString sql = QStringLiteral("SELECT %1 FROM memories WHERE 1=1")
                      .arg(QString::fromLatin1(kMemorySelectColumns));
    if (!characterName.isEmpty()) {
        sql += QStringLiteral(" AND character_name IN ('*', :char)");
    }
    if (!keyword.trimmed().isEmpty()) {
        sql += QStringLiteral(
            " AND (content LIKE :kw OR keywords LIKE :kw OR subject LIKE :kw)");
    }
    sql += QStringLiteral(
        " ORDER BY CASE WHEN status='pinned' THEN 0 ELSE 1 END,"
        " importance DESC, id DESC LIMIT :limit");

    QSqlQuery query(m_db);
    query.prepare(sql);
    if (!characterName.isEmpty()) {
        query.bindValue(QStringLiteral(":char"), characterName);
    }
    if (!keyword.trimmed().isEmpty()) {
        query.bindValue(QStringLiteral(":kw"), QStringLiteral("%%1%").arg(keyword.trimmed()));
    }
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] listMemories 失败：" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.append(recordFromQuery(query));
    }
    return result;
}

QList<MemoryRecord> MemoryDB::searchMemories(const QString &currentCharacter,
                                             const QString &query, int limit) const
{
    QList<MemoryRecord> result;
    if (!isOpen() || query.trimmed().isEmpty()) return result;

    const QStringList terms = searchTerms(query);
    if (terms.isEmpty()) return result;

    QHash<int, MemoryRecord> candidates;

    // 1) LIKE 兜底：中文两字关键词 FTS trigram 命中不了
    {
        QString sql = QStringLiteral("SELECT %1 FROM memories "
                                     "WHERE status IN ('active','pinned')")
                          .arg(QString::fromLatin1(kMemorySelectColumns));
        if (!currentCharacter.isEmpty()) {
            sql += QStringLiteral(" AND character_name IN ('*', :char)");
        }
        sql += QStringLiteral(" AND (");
        for (int i = 0; i < terms.size(); ++i) {
            if (i > 0) sql += QStringLiteral(" OR ");
            sql += QStringLiteral("content LIKE :t%1 OR keywords LIKE :t%1 "
                                  "OR subject LIKE :t%1").arg(i);
        }
        sql += QStringLiteral(") LIMIT 300");

        QSqlQuery q(m_db);
        q.prepare(sql);
        if (!currentCharacter.isEmpty()) q.bindValue(QStringLiteral(":char"), currentCharacter);
        for (int i = 0; i < terms.size(); ++i) {
            q.bindValue(QStringLiteral(":t%1").arg(i),
                        QStringLiteral("%%1%").arg(terms.at(i)));
        }
        if (q.exec()) {
            while (q.next()) {
                const MemoryRecord r = recordFromQuery(q);
                candidates.insert(r.id, r);
            }
        } else {
            qWarning() << "[MemoryDB] searchMemories LIKE 查询失败：" << q.lastError().text();
        }
    }

    // 2) FTS5 trigram：>=3 字符的词走索引（两字词上面已经覆盖）
    if (m_ftsAvailable) {
        for (const QString &term : terms) {
            if (term.size() < 3) continue;
            QString escaped = term;
            escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            const QString matchExpr = QStringLiteral("\"%1\"").arg(escaped);

            QString sql = QStringLiteral(
                "SELECT m.id, m.character_name, m.kind, m.subject, m.content, "
                "m.keywords, m.importance, m.confidence, m.source_message_id, "
                "m.created_at, m.last_hit_at, m.hit_count, m.status "
                "FROM memories_fts f JOIN memories m ON m.id = f.rowid "
                "WHERE memories_fts MATCH :match "
                "AND m.status IN ('active','pinned')");
            if (!currentCharacter.isEmpty()) {
                sql += QStringLiteral(" AND m.character_name IN ('*', :char)");
            }
            sql += QStringLiteral(" LIMIT 100");

            QSqlQuery q(m_db);
            q.prepare(sql);
            q.bindValue(QStringLiteral(":match"), matchExpr);
            if (!currentCharacter.isEmpty()) q.bindValue(QStringLiteral(":char"), currentCharacter);
            if (q.exec()) {
                while (q.next()) {
                    const MemoryRecord r = recordFromQuery(q);
                    candidates.insert(r.id, r);
                }
            }
            // FTS 语法/索引问题不致命，LIKE 候选仍在
        }
    }

    if (candidates.isEmpty()) return result;

    // 3) 打分：命中词数 + importance + 30 天半衰期 + 命中次数
    const QDateTime now = QDateTime::currentDateTime();
    const QString lowerQuery = query.toLower();
    QList<QPair<double, MemoryRecord>> scored;
    scored.reserve(candidates.size());

    for (const MemoryRecord &r : candidates) {
        double score = r.importance * 1.0;
        if (r.status == QStringLiteral("pinned")) score += 1.5;

        const QString haystack = (r.content + QLatin1Char(' ') + r.keywords + QLatin1Char(' ')
                                  + r.subject).toLower();
        int matched = 0;
        for (const QString &t : terms) {
            if (haystack.contains(t.toLower())) ++matched;
        }
        score += matched * 1.2;

        // 记忆里的关键词出现在当前消息里 → 强信号
        const QStringList kws = r.keywords.split(QRegularExpression(QStringLiteral("\\s+")),
                                                 Qt::SkipEmptyParts);
        for (const QString &kw : kws) {
            if (kw.size() >= 2 && lowerQuery.contains(kw.toLower())) {
                score += 2.0;
                break;
            }
        }

        const QDateTime created = QDateTime::fromString(r.createdAt, Qt::ISODate);
        if (created.isValid()) {
            const qint64 days = created.daysTo(now);
            score += 30.0 / (30.0 + qMax<qint64>(0, days));
        }
        score += qMin(r.hitCount, 10) * 0.1;
        score *= qBound(0.0, r.confidence, 1.0);

        scored.append(qMakePair(score, r));
    }

    std::sort(scored.begin(), scored.end(),
              [](const QPair<double, MemoryRecord> &a,
                 const QPair<double, MemoryRecord> &b) {
                  return a.first > b.first;
              });

    const int take = qMax(1, limit);
    for (int i = 0; i < scored.size() && i < take; ++i) {
        result.append(scored.at(i).second);
    }
    return result;
}

QList<MemoryRecord> MemoryDB::memoriesByKind(const QString &currentCharacter,
                                             const QString &kind, int limit) const
{
    QList<MemoryRecord> result;
    if (!isOpen() || kind.trimmed().isEmpty()) return result;

    QString sql = QStringLiteral(
        "SELECT %1 FROM memories "
        "WHERE status IN ('active','pinned') AND kind = :kind")
                      .arg(QString::fromLatin1(kMemorySelectColumns));
    if (!currentCharacter.isEmpty()) {
        sql += QStringLiteral(" AND (character_name = '*' OR character_name = :char)");
    }
    sql += QStringLiteral(
        " ORDER BY CASE WHEN status='pinned' THEN 0 ELSE 1 END,"
        " importance DESC, last_hit_at IS NULL, hit_count DESC, id DESC LIMIT :limit");

    QSqlQuery query(m_db);
    query.prepare(sql);
    query.bindValue(QStringLiteral(":kind"), kind.trimmed());
    if (!currentCharacter.isEmpty()) {
        query.bindValue(QStringLiteral(":char"), currentCharacter);
    }
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] memoriesByKind 失败：" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.append(recordFromQuery(query));
    }
    return result;
}

QList<MemoryRecord> MemoryDB::fixedMemories(const QString &currentCharacter, int limit) const
{
    QList<MemoryRecord> result;
    if (!isOpen()) return result;

    QString sql = QStringLiteral(
        "SELECT %1 FROM memories "
        "WHERE status IN ('active','pinned') AND (importance >= 4 OR status = 'pinned')")
                      .arg(QString::fromLatin1(kMemorySelectColumns));
    if (!currentCharacter.isEmpty()) {
        sql += QStringLiteral(" AND (character_name = '*' OR character_name = :char)");
    }
    sql += QStringLiteral(
        " ORDER BY CASE WHEN status='pinned' THEN 0 ELSE 1 END,"
        " importance DESC, id ASC LIMIT :limit");

    QSqlQuery query(m_db);
    query.prepare(sql);
    if (!currentCharacter.isEmpty()) {
        query.bindValue(QStringLiteral(":char"), currentCharacter);
    }
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] fixedMemories 失败：" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.append(recordFromQuery(query));
    }
    return result;
}

void MemoryDB::touchMemories(const QList<int> &ids)
{
    if (!isOpen() || ids.isEmpty()) return;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "UPDATE memories SET hit_count = hit_count + 1, last_hit_at = :now WHERE id = :id"));
    const QString now = normalizedNow();
    for (int id : ids) {
        if (id <= 0) continue;
        query.bindValue(QStringLiteral(":now"), now);
        query.bindValue(QStringLiteral(":id"), id);
        if (!query.exec()) {
            qWarning() << "[MemoryDB] touchMemories 失败：" << query.lastError().text();
            return;
        }
    }
}

int MemoryDB::clearMemories(const QString &characterName)
{
    if (!isOpen()) return -1;

    QSqlQuery query(m_db);
    if (characterName.isEmpty()) {
        query.prepare(QStringLiteral("DELETE FROM memories"));
    } else {
        query.prepare(QStringLiteral("DELETE FROM memories WHERE character_name = :char"));
        query.bindValue(QStringLiteral(":char"), characterName);
    }
    if (!query.exec()) {
        qWarning() << "[MemoryDB] clearMemories 失败：" << query.lastError().text();
        return -1;
    }
    const int removed = query.numRowsAffected();
    qInfo() << "[MemoryDB] 已清除长期记忆：" << (characterName.isEmpty() ? "全部" : characterName)
            << "共" << removed << "条";
    return removed;
}

int MemoryDB::memoryCount(const QString &characterName) const
{
    if (!isOpen()) return 0;

    QString sql = QStringLiteral(
        "SELECT COUNT(*) FROM memories WHERE status IN ('active','pinned')");
    if (!characterName.isEmpty()) {
        sql += QStringLiteral(" AND character_name IN ('*', :char)");
    }
    QSqlQuery query(m_db);
    query.prepare(sql);
    if (!characterName.isEmpty()) {
        query.bindValue(QStringLiteral(":char"), characterName);
    }
    if (!query.exec() || !query.next()) {
        return 0;
    }
    return query.value(0).toInt();
}

QList<MemoryRecord> MemoryDB::findSimilarMemories(const QString &characterName,
                                                  const QString &kind,
                                                  const QString &subject, int limit) const
{
    QList<MemoryRecord> result;
    if (!isOpen()) return result;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT %1 FROM memories "
        "WHERE character_name = :char AND kind = :kind AND subject = :subject "
        "AND status != 'archived' ORDER BY id ASC LIMIT :limit")
                      .arg(QString::fromLatin1(kMemorySelectColumns)));
    query.bindValue(QStringLiteral(":char"), characterName);
    query.bindValue(QStringLiteral(":kind"), kind);
    query.bindValue(QStringLiteral(":subject"), subject);
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] findSimilarMemories 失败：" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.append(recordFromQuery(query));
    }
    return result;
}

QList<MemoryRecord> MemoryDB::matchMemories(const QString &characterName,
                                            const QString &substring, int limit) const
{
    QList<MemoryRecord> result;
    if (!isOpen() || substring.trimmed().isEmpty()) return result;

    QString sql = QStringLiteral(
        "SELECT %1 FROM memories WHERE status != 'archived' "
        "AND (content LIKE :kw OR keywords LIKE :kw OR subject LIKE :kw)")
                      .arg(QString::fromLatin1(kMemorySelectColumns));
    if (!characterName.isEmpty()) {
        sql += QStringLiteral(" AND character_name IN ('*', :char)");
    }
    sql += QStringLiteral(" ORDER BY importance DESC, id DESC LIMIT :limit");

    QSqlQuery query(m_db);
    query.prepare(sql);
    query.bindValue(QStringLiteral(":kw"), QStringLiteral("%%1%").arg(substring.trimmed()));
    if (!characterName.isEmpty()) {
        query.bindValue(QStringLiteral(":char"), characterName);
    }
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] matchMemories 失败：" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        result.append(recordFromQuery(query));
    }
    return result;
}

QList<StoredMessage> MemoryDB::loadRecentMessages(const QString &characterName, int limit) const
{
    QList<StoredMessage> result;
    if (!isOpen()) return result;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, role, content, created_at FROM messages "
        "WHERE character_name = :char ORDER BY id DESC LIMIT :limit"));
    query.bindValue(QStringLiteral(":char"), characterName);
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));
    if (!query.exec()) {
        qWarning() << "[MemoryDB] loadRecentMessages 失败：" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        StoredMessage m;
        m.id        = query.value(0).toLongLong();
        m.role      = query.value(1).toString();
        m.content   = query.value(2).toString();
        m.createdAt = query.value(3).toString();
        result.prepend(m);
    }
    return result;
}

QList<StoredMessage> MemoryDB::loadMessagesBefore(const QString &characterName,
                                                  const QString &beforeIso, int limit) const
{
    return loadMessagesRange(characterName, QString(), beforeIso, limit);
}

QList<StoredMessage> MemoryDB::loadMessagesRange(const QString &characterName,
                                                 const QString &afterIso,
                                                 const QString &beforeIso,
                                                 int limit) const
{
    QList<StoredMessage> result;
    if (!isOpen()) return result;

    QSqlQuery query(m_db);
    QString sql = QStringLiteral(
        "SELECT id, role, content, created_at FROM messages "
        "WHERE character_name = :char AND created_at < :before ");
    if (!afterIso.isEmpty()) {
        sql += QStringLiteral("AND created_at >= :after ");
    }
    sql += QStringLiteral("ORDER BY id ASC LIMIT :limit");
    query.prepare(sql);
    query.bindValue(QStringLiteral(":char"), characterName);
    query.bindValue(QStringLiteral(":before"), beforeIso);
    if (!afterIso.isEmpty()) {
        query.bindValue(QStringLiteral(":after"), afterIso);
    }
    query.bindValue(QStringLiteral(":limit"), qMax(1, limit));
    if (!query.exec()) {
        qWarning() << "[MemoryDB] loadMessagesRange 失败：" << query.lastError().text();
        return result;
    }
    while (query.next()) {
        StoredMessage m;
        m.id        = query.value(0).toLongLong();
        m.role      = query.value(1).toString();
        m.content   = query.value(2).toString();
        m.createdAt = query.value(3).toString();
        result.append(m);
    }
    return result;
}

bool MemoryDB::savePreference(const QString &key, const QString &value)
{
    if (!isOpen()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO preferences (key, value, updated_at) VALUES (:key, :value, :updated_at) "
        "ON CONFLICT(key) DO UPDATE SET value = :value, updated_at = :updated_at"
    ));
    query.bindValue(QStringLiteral(":key"), key);
    query.bindValue(QStringLiteral(":value"), value);
    query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentDateTime().toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] savePreference 失败：" << query.lastError().text();
        return false;
    }
    return true;
}

QString MemoryDB::loadPreference(const QString &key, const QString &defaultValue) const
{
    if (!isOpen()) {
        return defaultValue;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT value FROM preferences WHERE key = :key"));
    query.bindValue(QStringLiteral(":key"), key);

    if (!query.exec() || !query.next()) {
        return defaultValue;
    }
    return query.value(0).toString();
}

bool MemoryDB::saveUserName(const QString &name, int userId)
{
    if (!isOpen()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO users (id, name, created_at) VALUES (:id, :name, :created_at) "
        "ON CONFLICT(id) DO UPDATE SET name = :name"
    ));
    query.bindValue(QStringLiteral(":id"), userId);
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":created_at"), QDateTime::currentDateTime().toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "[MemoryDB] saveUserName 失败：" << query.lastError().text();
        return false;
    }
    return true;
}

QString MemoryDB::loadUserName(int userId) const
{
    if (!isOpen()) {
        return QString();
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT name FROM users WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), userId);

    if (!query.exec() || !query.next()) {
        return QString();
    }
    return query.value(0).toString();
}
