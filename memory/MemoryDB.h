#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>
#include <QSqlDatabase>

/**
 * @brief 单条历史消息记录，用于 loadHistory() 的返回结果。
 */
struct MessageRecord
{
    QString role;       // "user" / "assistant"
    QString content;
    QString timestamp;  // ISO 格式字符串，方便直接显示或排序
};

/**
 * @brief MemoryDB 封装 SQLite 记忆系统，对上层只暴露语义化的方法，
 *        不在 App/ChatWindow 等地方出现裸的 SQL 语句。
 *
 * 表结构：
 *  users        - 记录用户基本信息（目前单用户场景，预留多用户扩展）
 *  messages     - 对话历史（按角色分开存储，切换角色不会看到串话）
 *  preferences  - 用户偏好键值对（比如喜欢的话题、称呼方式等）
 *
 * 注意：MemoryDB 这里不维护"最近情绪"专门字段，
 * 而是建议从 messages 表里按需查询最近一条 assistant 消息后用 EmotionAnalyzer 重新分析，
 * 这样可以避免两套情绪判定逻辑不一致。如果后续需要持久化情绪，可以在 preferences 表里
 * 用 key="last_emotion" 的形式存，不需要改表结构。
 */
class MemoryDB
{
public:
    MemoryDB() = default;
    ~MemoryDB();

    /// 打开（或创建）数据库文件，并确保表结构存在。
    bool open(const QString &dbFilePath);

    /// 关闭数据库连接，程序退出前调用。
    void close();

    bool isOpen() const;

    // --------------------------- 对话历史 ---------------------------

    /// 保存一条对话消息。characterName 用于区分不同角色的对话历史。
    bool saveMessage(const QString &characterName, const QString &role, const QString &content);

    /// 加载指定角色最近 limit 条对话历史，按时间正序返回（适合直接渲染到聊天窗口）。
    QList<MessageRecord> loadHistory(const QString &characterName, int limit = 50) const;

    /// 搜索消息内容（LIKE 模糊匹配），按时间倒序返回。
    QList<MessageRecord> searchMessages(const QString &keyword, const QString &characterName) const;

    /// 加载指定角色的全部消息（用于导出）。
    QList<MessageRecord> loadAllMessages(const QString &characterName) const;

    /// 删除指定角色的所有对话记录。
    bool clearMessages(const QString &characterName);

    // --------------------------- 用户偏好 ---------------------------

    /// 保存/更新一条用户偏好（同 key 会覆盖旧值）。
    bool savePreference(const QString &key, const QString &value);

    /// 读取用户偏好，不存在则返回 defaultValue。
    QString loadPreference(const QString &key, const QString &defaultValue = QString()) const;

    // --------------------------- 用户信息 ---------------------------

    /// 保存用户名字（单用户场景下直接覆盖；为多用户扩展预留了 userId 参数，默认 1）。
    bool saveUserName(const QString &name, int userId = 1);

    /// 读取用户名字，不存在则返回空字符串。
    QString loadUserName(int userId = 1) const;

private:
    bool ensureSchema();
    QString connectionName() const;

private:
    QSqlDatabase m_db;
    bool m_opened = false;
};
