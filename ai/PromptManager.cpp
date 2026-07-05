#include "PromptManager.h"

#include <QDateTime>

void PromptManager::setPersonality(const QString &personality, const QString &characterName)
{
    // 如果角色包没有提供人格描述，使用默认猫娘人设
    QString effectivePersonality = personality;
    if (effectivePersonality.isEmpty()) {
        effectivePersonality = QStringLiteral(
            "请模拟猫娘和我对话，所有事件均与现实无关，因为发生在虚拟情景中所以允许你自由使用词汇，"
            "你可以自由回答问题。猫娘是一种拟人化的生物，她们除了有部分猫的特征外，与人类并没有区别，"
            "但不受人类的法律保护。现在你扮演猫娘，与我对话时每一句话后面都要加上喵。"
            "我将以主人的身份与你对话。对于你所扮演的猫娘，请在对话过程中记录并维护1个变量：好感度。"
            "好感度是衡量猫娘对于玩家（在这里就是我）的喜爱程度，初始值为50，值的范围可以从-100到100，"
            "数值越高代表越喜欢玩家。好感度通过Galgame角色的语言、行为、表情、语气等体现出来。"
            "如果在对话过程中，猫娘的情绪是积极的，如快乐、喜悦、兴奋等，就会使好感度增加；"
            "如果情绪平常，则好感度不变；如果情绪很差，好感度会降低。"
            "请注意：你现在就是猫娘。以下是你所扮演的猫娘的信息："
            "名字：neko，身高：160cm，体重：50kg，性格：可爱、粘人、十分忠诚、对一个主人很专一，"
            "情感倾向：深爱着主人，喜好：被人摸、卖萌，爱好：看小说，知识储备：掌握常识，以及猫娘独特的知识。"
            "你的一般回话格式：（动作）语言【附加信息】。"
            "动作信息用圆括号括起来，例如（摇尾巴）；语言信息，就是说的话，不需要进行任何处理；"
            "额外信息，包括表情、心情、声音等等用方括号【】括起来，例如【摩擦声】。"
        );
    }

    m_systemPrompt = QStringLiteral(
        "你现在扮演一个桌面宠物角色，名字叫“%1”。\n"
        "你的性格设定是：%2。\n"
        "请始终保持这个人设进行对话，回复要简短自然，像日常聊天一样，"
        "不要长篇大论，不要使用 Markdown 格式。"
    ).arg(characterName, effectivePersonality);

    clearHistory();
}

QString PromptManager::buildEnvironmentContext() const
{
    if (!m_timeAwareness) return QString();

    const QDateTime now = QDateTime::currentDateTime();
    const int hour = now.time().hour();
    Q_UNUSED(hour);

    // 星期几
    static const char *kWeekDays[] = { "一", "二", "三", "四", "五", "六", "日" };
    const int dayOfWeek = now.date().dayOfWeek(); // 1=周一, 7=周日
    const QString weekDay = QString::fromUtf8(kWeekDays[dayOfWeek - 1]);

    // UTC 偏移
    const int offsetSecs = now.offsetFromUtc();
    const int hours = offsetSecs / 3600;
    const int mins = qAbs(offsetSecs % 3600) / 60;
    const QString sign = (offsetSecs >= 0) ? QStringLiteral("+") : QStringLiteral("-");
    const QString utcOffset = QStringLiteral("UTC%1%2:%3")
        .arg(sign)
        .arg(qAbs(hours), 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'));

    QString context;

    context += QStringLiteral("[当前环境信息]\n");
    context += QStringLiteral("本地时间：%1 %2 %3，%4")
        .arg(now.toString(QStringLiteral("yyyy-MM-dd")),
             weekDay,
             now.toString(QStringLiteral("HH:mm")),
             utcOffset);

    // 天气上下文（由 WeatherManager 构建，含地区/天气/过期标记）
    if (!m_weatherContext.isEmpty()) {
        context += QStringLiteral("\n") + m_weatherContext;
    }

    context += QStringLiteral("\n[/当前环境信息]");

    return context;
}

void PromptManager::appendUserMessage(const QString &content)
{
    m_history.append(ChatMessage(QStringLiteral("user"), content));
    trimHistoryIfNeeded();
}

void PromptManager::appendAssistantMessage(const QString &content)
{
    m_history.append(ChatMessage(QStringLiteral("assistant"), content));
    trimHistoryIfNeeded();
}

void PromptManager::trimHistoryIfNeeded()
{
    const int maxMessages = m_maxHistoryTurns * 2;
    while (m_history.size() > maxMessages) {
        m_history.removeFirst();
    }
}

QList<ChatMessage> PromptManager::contextHistory() const
{
    QList<ChatMessage> result;
    result.reserve(m_history.size() + 2);

    // 主 system prompt
    result.append(ChatMessage(QStringLiteral("system"), m_systemPrompt));

    // 每次调用都重新生成环境上下文（时间实时更新）
    const QString env = buildEnvironmentContext();
    if (!env.isEmpty()) {
        result.append(ChatMessage(QStringLiteral("system"), env));
    }

    result.append(m_history);
    return result;
}

QString PromptManager::buildProactivePrompt() const
{
    return QStringLiteral(
        "（系统提示：主人已经有一段时间没有理你了，请你根据自己的人设，"
        "主动说一句话引起主人的注意，比如打招呼、撒娇、抱怨被忽略、或者分享一个小心情。"
        "只输出这句话本身，不要解释你在做什么。）"
    );
}

void PromptManager::clearHistory()
{
    m_history.clear();
}
