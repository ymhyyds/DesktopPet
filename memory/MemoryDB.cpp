#include "MemoryDB.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QDebug>
#include <QUuid>

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
