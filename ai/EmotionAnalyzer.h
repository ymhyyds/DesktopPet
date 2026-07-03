#pragma once

#include <QString>
#include <QStringList>
#include "../pet/PetAnimationState.h"

/**
 * @brief 情绪分析结果。
 */
struct EmotionResult
{
    PetAnimationState state = PetAnimationState::Idle;
    QString matchedKeyword;   // 命中的关键词（调试/日志用，没命中则为空）
};

/**
 * @brief EmotionAnalyzer 通过关键词匹配，从 AI 回复文本里粗略判断情绪倾向，
 *        然后映射为对应的动画状态。
 *
 * 这是一个关键词级别的简单实现（不是真正的 NLP 情感分析模型），
 * 但接口设计上做了隔离：未来想换成调用情感分类模型/再让 AI 自己返回结构化情绪标签，
 * 只需要重写 analyze() 的内部实现，外部调用方式不变。
 */
class EmotionAnalyzer
{
public:
    EmotionAnalyzer();

    /// 分析一段文本，返回判定出的情绪状态。命中多个关键词时按优先级（构造函数里定义的顺序）取第一个。
    EmotionResult analyze(const QString &text) const;

private:
    struct EmotionRule
    {
        PetAnimationState state;
        QStringList keywords;
    };

    QList<EmotionRule> m_rules;
};
