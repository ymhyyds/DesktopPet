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
 * @brief 数据库里的一条原始消息（带主键），用于长期记忆抽取 / 回填。
 */
struct StoredMessage
{
    qint64  id = -1;
    QString role;
    QString content;
    QString createdAt;  // ISO 格式
};

/**
 * @brief 长期记忆事实卡片。
 *
 * characterName 为角色目录名；'*' 表示跨角色共享的主人画像。
 * kind 取值：profile / preference / event / promise / relationship。
 * status 取值：active / archived / pinned。
 */
struct MemoryRecord
{
    int     id = -1;
    QString characterName;
    QString kind = QStringLiteral("event");
    QString subject;
    QString content;
    QString keywords;
    int     importance = 3;
    double  confidence = 1.0;
    int     sourceMessageId = -1;
    QString createdAt;
    QString lastHitAt;
    int     hitCount = 0;
    QString status = QStringLiteral("active");

    /// 仅抽取/解析时使用（add / update / delete），不落库。
    QString action = QStringLiteral("add");

    /// 仅抽取/解析时使用：promise/event 需引用主人原话作为依据，不落库。
    QString evidence;

    bool isValid() const { return id > 0; }
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

    /// 返回指定角色名下的消息条数。
    int messageCount(const QString &characterName) const;

    /**
     * @brief 把 from 角色名下的全部消息改挂到 to 名下（角色目录改名后的一次性历史合并）。
     *
     * 只做 UPDATE，不删除任何消息，失败时整笔回滚；天然幂等——
     * 第一次执行后 from 名下就没有消息了，再执行返回 0。
     * @return 受影响的消息条数；数据库未打开或参数非法返回 -1。
     */
    int mergeCharacterHistory(const QString &from, const QString &to);

    // --------------------------- 长期记忆（memories 表） ---------------------------

    /**
     * @brief 插入一条记忆卡片。
     * @return 新卡片 id；数据库未打开或内容为空返回 -1。
     */
    int addMemory(const MemoryRecord &m);

    /// 按 id 更新可编辑字段（content/keywords/importance/confidence/status）。
    bool updateMemory(const MemoryRecord &m);

    /// 硬删除一条记忆。
    bool deleteMemory(int id);

    /// 修改状态（active / archived / pinned）。
    bool setMemoryStatus(int id, const QString &status);

    /// 按 id 读取一条记忆；不存在返回 false。
    bool memoryById(int id, MemoryRecord *out) const;

    /**
     * @brief 列出记忆卡片（设置页使用）。
     *
     * characterName 非空时返回 `character_name IN ('*', characterName)`——
     * 共享画像也会出现在每个角色的列表里；搜索 keyword 非空时对
     * content/keywords/subject 做 LIKE 过滤。
     */
    QList<MemoryRecord> listMemories(const QString &characterName,
                                     const QString &keyword = QString(),
                                     int limit = 200) const;

    /**
     * @brief 为一轮对话检索最相关的记忆（L3 动态块）。
     *
     * 候选来源：FTS5 trigram（长度 >= 3 的词）+ LIKE 模糊匹配
     * （中文两字关键词 FTS 命中不了，必须兜底），然后在 C++ 侧按
     * 匹配度 + importance + 时间衰减打分排序。
     */
    QList<MemoryRecord> searchMemories(const QString &currentCharacter,
                                       const QString &query, int limit = 6) const;

    /**
     * @brief 固定块：'*' 画像 + pinned + importance >= 4，按重要度排序。
     */
    QList<MemoryRecord> fixedMemories(const QString &currentCharacter, int limit = 12) const;

    /// 按 kind 取卡（'*' + 当前角色），供意图感知检索使用（如问“答应过什么”取 promise）。
    QList<MemoryRecord> memoriesByKind(const QString &currentCharacter,
                                       const QString &kind,
                                       int limit = 20) const;

    /// 命中回写：hit_count++、last_hit_at=now。
    void touchMemories(const QList<int> &ids);

    /// 删除某角色的记忆（不删 '*' 共享画像）。characterName 为空 = 全部。
    int clearMemories(const QString &characterName);

    /// 统计角色可见卡片数（'*' + 该角色，active/pinned）。
    int memoryCount(const QString &characterName) const;

    /// 取出同 (character, kind, subject) 的候选卡片，供去重合并使用。
    QList<MemoryRecord> findSimilarMemories(const QString &characterName,
                                            const QString &kind,
                                            const QString &subject,
                                            int limit = 100) const;

    /// 对 content/keywords 做子串匹配（/忘掉 用），不参与相似度打分。
    QList<MemoryRecord> matchMemories(const QString &characterName,
                                      const QString &substring,
                                      int limit = 100) const;

    // --------------------------- 抽取/回填辅助 ---------------------------

    /// 最近 limit 条消息，按时间正序返回（供自动抽取使用）。
    QList<StoredMessage> loadRecentMessages(const QString &characterName, int limit) const;

    /// beforeIso 之前的全部消息，按时间正序返回（供历史回填使用）。
    QList<StoredMessage> loadMessagesBefore(const QString &characterName,
                                            const QString &beforeIso,
                                            int limit = 100000) const;

    /// [afterIso, beforeIso) 区间的消息，按时间正序返回；afterIso 为空表示不限下界。
    QList<StoredMessage> loadMessagesRange(const QString &characterName,
                                           const QString &afterIso,
                                           const QString &beforeIso,
                                           int limit = 100000) const;

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
    bool migrateToV1();
    int  schemaVersion() const;
    QString connectionName() const;

private:
    QSqlDatabase m_db;
    bool m_opened = false;
    bool m_ftsAvailable = false;  // 旧环境不支持 FTS5 时检索自动回退到 LIKE
};
