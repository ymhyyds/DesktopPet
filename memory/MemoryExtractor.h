#pragma once

#include <QObject>
#include <QList>
#include <QString>

#include "MemoryDB.h"
#include "../ai/PromptManager.h" // ChatMessage

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @brief MemoryExtractor — 长期记忆的写入通道。
 *
 * 复用 DeepSeek 的非流式接口（与聊天流式通道完全独立）：
 *  - extractFromRecentMessages(): 每 6 轮 / 闲置 3 分钟 / 退出前触发，
 *    把最近若干条消息发给模型，要求返回 JSON 卡片数组。
 *  - startBackfill(): 历史回填模式，按自然日分块串行抽取。
 *  - rememberManually() / forgetMatching(): /记住 /忘掉 不发网络请求，直接落库。
 *
 * 设计约束（见交接方案 §6）：
 *  - 同一时刻只允许一个在途请求（单飞行），忙时调用直接返回 false。
 *  - 只抽取用户明确说过/确认过的事实；输入里的 assistant-only 片段要过滤。
 *  - profile/preference 且 subject 是主人 → character_name='*'（跨角色共享）；
 *    其余按当前角色隔离。
 *  - 同 (character, kind, subject) 且内容 bigram Jaccard > 0.7 → 合并更新。
 *  - 网络/解析失败静默返回信号，不阻塞聊天，下个周期重试。
 */
class MemoryExtractor : public QObject
{
    Q_OBJECT

public:
    explicit MemoryExtractor(MemoryDB *db, QObject *parent = nullptr);
    ~MemoryExtractor() override;

    /// API 配置与 DeepSeekClient 同源，由 App 注入。
    void setApiConfig(const QString &apiKey, const QString &endpoint, const QString &model);
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }

    /// 是否有在途请求（自动抽取 / 回填共用）。
    bool isBusy() const { return m_busy; }
    bool isBackfilling() const { return m_backfillRunning; }

    /**
     * @brief 抽取最近 limit 条消息并写入 memories。
     * @return 请求是否成功发出（忙 / 无 Key / 无 user 消息返回 false）。
     */
    bool extractFromRecentMessages(const QString &characterName, int limit = 20);

    /// 回填 [afterIso, beforeIso) 区间的历史（手动触发，串行分块）；afterIso 为空表示不限下界。
    bool startBackfill(const QString &characterName,
                       const QString &afterIso,
                       const QString &beforeIso);

    /// 取消回填（中止在途请求，已完成的块保留）。
    void cancelBackfill();

    /// /记住：立即写入一张卡片（走与自动抽取相同的去重规则）。
    /// @return 卡片 id；失败返回 -1。
    int rememberManually(const QString &characterName, const QString &text);

    /// /忘掉：删除 content/keywords/subject 包含该关键词的卡片。
    /// @return 删除条数。
    int forgetMatching(const QString &characterName, const QString &keyword);

    // ──── 静态工具（供测试） ────

    /// 容错解析模型返回的 JSON（允许 ```json 包裹 / 前后多余文字）。
    static QList<MemoryRecord> parseExtractionJson(const QString &raw);

    /// 字符 bigram Jaccard 相似度（用于合并去重）。
    static double similarity(const QString &a, const QString &b);

    /// 从一句话里推导检索关键词（空格分隔）。
    static QString deriveKeywords(const QString &text);

    /// 归因校验：evidence 必须是 userText 里真出现过的原话（忽略空白/标点/大小写）。
    /// 没有 evidence 时：promise 直接判定无依据；event 退化为 content 与 userText
    /// 的最长连续重合 ≥3 字。userText 为空（本请求没有主人发言）时一律 false。
    static bool hasUserEvidence(const MemoryRecord &m, const QString &userText);

signals:
    void extractionFinished(int added, int updated, int deleted);
    void extractionFailed(const QString &error);

    void backfillProgress(int done, int total);
    void backfillFinished(int added, int updated, int failed);
    void backfillCancelled(int done, int total);

private:
    struct BackfillChunk
    {
        QString text;       // 已经拼好的对话记录
        int     userCount = 0;
    };

    bool sendExtractionRequest(const QString &transcript, bool backfill);
    void sendNextBackfillChunk();
    void finishBackfill();

    void handleReply(QNetworkReply *reply, bool backfill);
    void applyExtraction(const QList<MemoryRecord> &items, const QString &currentCharacter,
                         int &added, int &updated, int &deleted);

    QString buildSystemPrompt(bool backfill) const;
    QString buildUserPrompt(const QString &transcript, const QString &characterName,
                            bool backfill) const;
    QString characterFor(const MemoryRecord &m, const QString &currentCharacter) const;

private:
    MemoryDB *m_db = nullptr;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    QTimer *m_timeoutTimer = nullptr;

    QString m_apiKey;
    QString m_endpoint = QStringLiteral("https://api.deepseek.com/chat/completions");
    QString m_model = QStringLiteral("deepseek-v4-flash");

    bool m_busy = false;
    bool m_backfillRunning = false;

    // 回填计划
    QString m_backfillCharacter;
    QList<BackfillChunk> m_backfillChunks;
    int m_backfillIndex = 0;
    int m_backfillAdded = 0;
    int m_backfillUpdated = 0;
    int m_backfillFailed = 0;

    /// 当前请求归属（回填时用于把卡片记到被回填的角色名下）。
    QString m_requestCharacter;

    /// 当前请求里“主人：”开头的原话，用于 promise/event 的归因校验。
    QString m_activeUserText;
};
