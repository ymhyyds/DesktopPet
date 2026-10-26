#pragma once

#include <QString>
#include <QList>
#include <QPair>
#include <QDateTime>

/**
 * @brief 单条对话消息，role 取值约定为 "system" / "user" / "assistant"
 *        （与 DeepSeek/OpenAI 风格的 chat completion 接口保持一致）。
 */
using ChatMessage = QPair<QString, QString>; // first: role, second: content

/**
 * @brief PromptManager 负责"组装要发给 AI 的内容"，包括：
 *  1. 系统人格 Prompt（来自角色 config.json 的 personality 字段，不写死在代码里）
 *  2. 维护一份滚动的对话历史（保持上下文，同时控制长度避免 token 爆炸）
 *  3. 给"主动找话题"场景生成专门的 Prompt
 *
 * 不负责：实际发 HTTP 请求（那是 DeepSeekClient 的事），
 * 也不负责：从回复里识别情绪（那是 EmotionAnalyzer 的事）。
 */
class EvidenceManager;

class PromptManager
{
public:
    PromptManager() = default;

    /**
     * @brief 设置/切换当前角色人格。会重置历史对话（换了人格继续掺杂旧人格的上下文没有意义），
     *        但不会清空数据库里的长期记忆——那部分由 MemoryDB 单独管理。
     * @param personality 来自 config.json 的 personality 字段，例如 "可爱、傲娇、粘人"
     * @param characterName 角色显示名，用于系统 Prompt 里自我称呼
     */
    void setPersonality(const QString &personality, const QString &characterName);

    /// 返回默认的 AI 说话风格（空字符串 = 无自定义风格，保持默认行为）。
    static QString defaultAiStylePrompt();

    /// 获取当前用户自定义的说话风格提示词。
    QString aiStylePrompt() const { return m_stylePrompt; }

    /// 设置用户自定义的说话风格提示词。
    /// 当有效风格实际发生变化时，自动清除 API 对话历史（UI 聊天记录不受影响），
    /// 确保下一次 API 请求从干净的上下文开始。
    /// 空白内容会被视为"无自定义风格"，自动回退到默认行为。
    /// 对于需要异步摘要的风格切换，使用 beginStyleTransition() / completeStyleTransition() 代替。
    void setAiStylePrompt(const QString &prompt);

    /// 返回实际生效的说话风格提示词：非空时返回 m_stylePrompt，否则返回 defaultAiStylePrompt()。
    QString effectiveAiStylePrompt() const
    {
        return m_stylePrompt.isEmpty() ? defaultAiStylePrompt() : m_stylePrompt;
    }

    /// 归一化风格提示词（trim + 截断 2000 字）。
    static QString normalizeStylePrompt(const QString &prompt);

    /// 把用户消息加入历史（同时做长度裁剪）。
    void appendUserMessage(const QString &content);

    /// 把 AI 回复加入历史（同时做长度裁剪）。
    void appendAssistantMessage(const QString &content);

    /**
     * @brief 用数据库里的历史消息重建 API 短期上下文（启动、切换角色时使用）。
     *
     * 与 appendXxx 不同，这里会先清空现有历史再整体写入，且**不递增**
     * conversationGeneration——调用点都发生在没有在途请求的干净时刻
     * （App 启动、setPersonality 之后），不需要让旧回复失效。
     *
     * 传入的列表按时间正序，只保留 user/assistant；会做以下清洗，
     * 避免把数据库里的脏数据原样喂给接口：
     *  - 丢弃空内容、未知 role
     *  - 丢弃开头连续出现的 assistant（chat completions 接口要求以 user 开始）
     *  - 连续同角色的消息合并成一条（错误中断容易留下连续 user）
     * 最后按 maxHistoryTurns 裁剪，保留最新的若干轮。
     */
    void restoreHistory(const QList<ChatMessage> &history);

    /**
     * @brief 返回当前应该发给 AI 的完整上下文（包含 system 人格消息 + 最近若干轮对话）。
     *        DeepSeekClient::sendMessage() 会直接把这个列表序列化进请求体。
     */
    QList<ChatMessage> contextHistory() const;

    /**
     * @brief 挂机超时触发"主动聊天"时使用的特殊 Prompt——
     *        不是普通用户消息，而是一条指示 AI"主动找主人聊点什么"的隐藏指令。
     *        这条指令本身不会被存入历史（避免污染上下文），由调用方决定是否记录其回复。
     */
    QString buildProactivePrompt() const;

    /// 返回当前对话代数。每次 clearHistory() 或有效风格变化导致的清空时递增。
    /// 用于异步请求路径中判断回复是否仍属于当前上下文。
    int conversationGeneration() const { return m_conversationGeneration; }

    /// 清空对话历史和中立摘要（不影响人格设定），用于"重置话题"、角色切换等场景。
    /// 同时递增 conversationGeneration。
    void clearHistory();

    // ──── 异步风格切换接口（替代 setAiStylePrompt 的直接清空） ────

    /**
     * @brief 开始风格切换事务。
     * - 创建当前普通 user/assistant history 的不可变快照
     * - conversationGeneration 立即递增（使在途旧回复不写入新 history）
     * - 清空 m_history
     * @return 切换前 user/assistant history 的快照（用于异步摘要请求）
     */
    QList<ChatMessage> beginStyleTransition();

    /**
     * @brief 完成风格切换：设置新风格并保存中立摘要。
     * 不重复清空 m_history（已在 beginStyleTransition 中完成），
     * 不重复递增 generation。
     * @param stylePrompt 归一化后的目标风格
     * @param neutralSummary 解析后的中立摘要文本（空 = 无摘要）
     */
    void completeStyleTransition(const QString &stylePrompt, const QString &neutralSummary);

    /**
     * @brief 摘要失败时的安全 fallback：仅保留最近的 user 消息完成切换。
     * @param stylePrompt 归一化后的目标风格
     * @param userMessages 要保留的 user 消息列表（按时间正序）
     */
    void completeStyleTransitionWithFallback(const QString &stylePrompt, const QStringList &userMessages);

    /// 返回当前存储的中立对话摘要（空字符串表示无摘要）。
    QString conversationSummary() const { return m_conversationSummary; }

    /// 设置 EvidenceManager 指针（非拥有），用于 contextHistory() 中注入原始证据块。
    void setEvidenceManager(EvidenceManager *mgr) { m_evidenceManager = mgr; }

    /// 返回当前绑定的 EvidenceManager 指针（可能为 nullptr）。
    const EvidenceManager *evidenceManager() const { return m_evidenceManager; }

    /// 当前是否有普通对话历史（用于判断是否需要摘要）。
    bool hasConversationHistory() const { return !m_history.isEmpty(); }

    /// 历史消息最多保留多少轮（一轮 = 一条 user + 一条 assistant），超出自动裁剪最旧的。
    void setMaxHistoryTurns(int turns) { m_maxHistoryTurns = turns; }
    int maxHistoryTurns() const { return m_maxHistoryTurns; }

    /// 开启/关闭时间感知（默认开启），在 system prompt 中注入当前时间信息。
    void setTimeAwareness(bool enabled) { m_timeAwareness = enabled; }
    bool timeAwareness() const { return m_timeAwareness; }

    /// 设置天气上下文字段（由 WeatherManager 构建的天气文本，不含时间部分）。
    /// 空字符串表示无天气数据。
    void setWeatherContext(const QString &ctx) { m_weatherContext = ctx; }

    /**
     * @brief 设置长期记忆块（App 每条消息发出前检索生成）。
     * 空字符串表示不注入；注入位置在 [对话摘要] 之后、[原始对话证据] 之前。
     */
    void setMemoryContext(const QString &block) { m_memoryContext = block; }
    QString memoryContext() const { return m_memoryContext; }

    /// 构建时间/天气感知文本，追加到 system prompt 尾部。
    QString buildEnvironmentContext() const;

private:
    void trimHistoryIfNeeded();

private:
    QString m_systemPrompt;
    QString m_stylePrompt;        // 用户自定义说话风格（空 = 无自定义，保持默认行为），最大 2000 字
    QList<ChatMessage> m_history; // 不包含 system 消息，role 只会是 "user"/"assistant"
    int m_maxHistoryTurns = 10;   // 默认保留最近 10 轮，平衡上下文连贯性和请求体大小
    bool m_timeAwareness = true;
    QString m_weatherContext;     // 由 WeatherManager 构建，仅含天气部分
    int m_conversationGeneration = 0;

    /// 风格切换时生成的中立摘要，通过 contextHistory() 注入为 system 消息。
    QString m_conversationSummary;

    /// 长期记忆注入块（App 每条消息发出前重算）。
    QString m_memoryContext;

    /// 原始对话证据管理器（非拥有指针，可为 nullptr）。
    EvidenceManager *m_evidenceManager = nullptr;
};
