#include "EmotionAnalyzer.h"

EmotionAnalyzer::EmotionAnalyzer()
{
    // 规则顺序即优先级：越靠前越先被检测。
    // 关键词列表故意保持精简且通用，不绑定具体角色人设的台词风格，
    // 角色的"说话方式"由 PromptManager 的人格 Prompt 控制，
    // 这里只识别情绪倾向，两者职责不重叠。

    m_rules.append({ PetAnimationState::Angry, {
        QStringLiteral("生气"), QStringLiteral("讨厌"), QStringLiteral("烦"),
        QStringLiteral("哼"), QStringLiteral("讨厌死了"), QStringLiteral("气死")
    }});

    m_rules.append({ PetAnimationState::Shy, {
        QStringLiteral("害羞"), QStringLiteral("脸红"), QStringLiteral("不好意思"),
        QStringLiteral("羞"), QStringLiteral("讨厌啦") // “讨厌啦”更偏嗔怪/害羞，优先级低于纯粹的“讨厌”
    }});

    m_rules.append({ PetAnimationState::Sleepy, {
        QStringLiteral("困"), QStringLiteral("好累"), QStringLiteral("想睡"),
        QStringLiteral("打哈欠"), QStringLiteral("瞌睡")
    }});

    m_rules.append({ PetAnimationState::Happy, {
        QStringLiteral("开心"), QStringLiteral("高兴"), QStringLiteral("哈哈"),
        QStringLiteral("耶"), QStringLiteral("喜欢"), QStringLiteral("好棒"),
        QStringLiteral("嘻嘻")
    }});
}

EmotionResult EmotionAnalyzer::analyze(const QString &text) const
{
    for (const EmotionRule &rule : m_rules) {
        for (const QString &keyword : rule.keywords) {
            if (text.contains(keyword)) {
                return EmotionResult{ rule.state, keyword };
            }
        }
    }
    // 没命中任何关键词，默认回到 Idle
    return EmotionResult{ PetAnimationState::Idle, QString() };
}
