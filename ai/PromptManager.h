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

    /// 设置用户自定义的说话风格提示词。会在下一次 contextHistory() 调用时生效。
    /// 空白内容会被视为"无自定义风格"，自动回退到默认行为。
    void setAiStylePrompt(const QString &prompt) { m_stylePrompt = prompt.trimmed(); }

    /// 返回实际生效的说话风格提示词：非空时返回 m_stylePrompt，否则返回 defaultAiStylePrompt()。
    QString effectiveAiStylePrompt() const
    {
        return m_stylePrompt.isEmpty() ? defaultAiStylePrompt() : m_stylePrompt;
    }

    /// 把用户消息加入历史（同时做长度裁剪）。
    void appendUserMessage(const QString &content);

    /// 把 AI 回复加入历史（同时做长度裁剪）。
    void appendAssistantMessage(const QString &content);

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

    /// 清空对话历史（不影响人格设定），用于"重置话题"之类的场景。
    void clearHistory();

    /// 历史消息最多保留多少轮（一轮 = 一条 user + 一条 assistant），超出自动裁剪最旧的。
    void setMaxHistoryTurns(int turns) { m_maxHistoryTurns = turns; }

    /// 开启/关闭时间感知（默认开启），在 system prompt 中注入当前时间信息。
    void setTimeAwareness(bool enabled) { m_timeAwareness = enabled; }
    bool timeAwareness() const { return m_timeAwareness; }

    /// 设置天气上下文字段（由 WeatherManager 构建的天气文本，不含时间部分）。
    /// 空字符串表示无天气数据。
    void setWeatherContext(const QString &ctx) { m_weatherContext = ctx; }

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
};
