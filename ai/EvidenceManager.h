#pragma once

#include <QString>
#include <QList>
#include <QPair>

/**
 * @brief 单条原始对话证据。
 *
 * originalText 必须直接从原始 ChatMessage 的 second 字段复制，
 * 不经过 LLM 重写、不转换币种、不修正措辞、不添加派生信息。
 */
struct PreservedEvidence {
    QString id;             ///< 形如 "evt-1", "evt-2" 的稳定标识
    QString role;           ///< "user" 或 "assistant"
    QString originalText;   ///< 字节级不变的原始消息文本
    quint64 sequence = 0;   ///< 单调递增序号，用于时间顺序判断
};

/**
 * @brief EvidenceManager 管理需要逐字保真的原始对话片段。
 *
 * 职责：
 * - 在风格切换时将完整 snapshot 加入证据
 * - 按容量限制裁剪（最旧优先）
 * - 生成 [原始对话证据] 块供 contextHistory 注入
 * - 角色切换或用户清除时清空
 *
 * 不负责：语义解析、确认状态、正则匹配、QSettings 持久化。
 */
class EvidenceManager
{
public:
    EvidenceManager() = default;

    /// 将一条 snapshot（role, content 列表）全部追加为证据。
    /// 自动去重（相同 role + originalText）、自动裁剪到容量限制。
    void addSnapshot(const QList<QPair<QString, QString>> &snapshot);

    /// 清空所有证据（角色切换或用户主动清除）。
    void clear();

    /// 返回所有证据（按 sequence 升序，即时间正序）。
    const QList<PreservedEvidence> &allEvidence() const { return m_evidence; }

    /// 是否有任何证据。
    bool isEmpty() const { return m_evidence.isEmpty(); }

    /// 当前证据条数。
    int evidenceCount() const { return m_evidence.size(); }

    /// 所有 originalText 字符数总和。
    int totalTextLength() const;

    /// 构建 [原始对话证据] 块文本，供 PromptManager::contextHistory() 注入。
    QString buildEvidenceBlock() const;

    /// 最大证据条数（默认 16）。
    void setMaxCount(int max) { m_maxCount = qMax(1, max); }
    int maxCount() const { return m_maxCount; }

    /// 所有 originalText 总长度上限（默认 6000 字符）。
    void setMaxTotalLength(int max) { m_maxTotalLength = qMax(1, max); }
    int maxTotalLength() const { return m_maxTotalLength; }

private:
    /// 按容量限制裁剪：删除最旧的完整证据直到满足限制。
    /// 不截断单条消息。若一条已超出总限制则不加入。
    void trimToFit();

    /// 将字符串转为 JSON 转义形式（含包围引号），
    /// 防止 evidence 块中的标签注入。
    static QString jsonEscape(const QString &s);

    /// 生成唯一 ID。
    QString generateId();

    QList<PreservedEvidence> m_evidence;
    quint64 m_nextSequence = 1;
    int m_maxCount = 16;
    int m_maxTotalLength = 6000;
};
