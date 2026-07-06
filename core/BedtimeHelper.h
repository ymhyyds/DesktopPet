#pragma once

#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

/**
 * @brief BedtimeHelper — 深夜就寝提醒的纯逻辑函数集合。
 *
 * 所有函数均为纯逻辑（无副作用、不依赖 App 内部状态），
 * 便于单元测试。
 */
namespace BedtimeHelper {

/// 默认提醒文本（也是 messages()[0]）
inline QString defaultMessage()
{
    return QStringLiteral("主人，已经很晚啦，早点休息吧喵～");
}

/// 16 条内置本地提醒文案
inline const QStringList &messages()
{
    static const QStringList msgs = {
        QStringLiteral("主人，已经很晚啦，早点休息吧喵～"),
        QStringLiteral("今天辛苦啦，剩下的事情明天再做吧喵。"),
        QStringLiteral("夜深了，先去睡一觉，明天会更有精神喵～"),
        QStringLiteral("该让眼睛和身体休息一下啦，主人晚安喵。"),
        QStringLiteral("再忙也要照顾好自己，今晚先到这里吧喵。"),
        QStringLiteral("已经很晚了，保存好进度就去休息吧喵～"),
        QStringLiteral("主人，别熬得太晚，明天还要元气满满喵！"),
        QStringLiteral("休息不是偷懒，是给明天充电喵～"),
        QStringLiteral("看了这么久屏幕，去洗漱准备睡觉吧喵。"),
        QStringLiteral("夜已经深啦，关掉电脑，钻进被窝吧喵～"),
        QStringLiteral("今天已经做得很好了，可以安心休息啦喵。"),
        QStringLiteral("主人，身体也需要关机维护，早点睡吧喵～"),
        QStringLiteral("先和今天说晚安，明天再继续努力吧喵。"),
        QStringLiteral("该暂停一下啦，伸个懒腰，然后去睡觉喵～"),
        QStringLiteral("熬夜会偷走明天的精神，早点休息吧喵。"),
        QStringLiteral("现在去睡，还来得及做个好梦喵～"),
    };
    return msgs;
}

/// 确定性映射：给定候选索引和 previousIndex，返回符合去重约束的最终索引。
///
/// 将"去重决策"与"随机数生成"分离。selectRandomMessageIndex 生成一次随机
/// 候选值后调用本函数，测试也可直接调用本函数做确定性验证。
///
/// @param candidate     候选索引值（会被安全规范化到 [0, count)）
/// @param previousIndex 上一次选中的索引。-1 或越界表示从未选择过，不排除。
/// @param count         文案池大小
/// @return 符合去重约束的索引；count≤0 返回 -1，count=1 返回 0
inline int resolveMessageIndex(int candidate, int previousIndex, int count)
{
    if (count <= 0)
        return -1;
    if (count == 1)
        return 0;
    // 安全规范化 candidate 到 [0, count)
    int c = candidate % count;
    if (c < 0) c += count;
    // previousIndex 无效（-1 或越界）→ 不排除任何索引
    if (previousIndex < 0 || previousIndex >= count)
        return c;
    // candidate != previousIndex → 直接返回
    if (c != previousIndex)
        return c;
    // candidate == previousIndex → 映射到另一个合法索引，不循环重抽
    return (previousIndex + 1) % count;
}

/// 随机选择一个文案索引（不与 previousIndex 连续重复）
///
/// 内部只调用一次 QRandomGenerator，委托 resolveMessageIndex 做去重决策。
///
/// @param previousIndex  上一次选中的索引。传入 -1 表示从未选择过，
///                        允许从全部文案中随机；越界值同样安全处理。
/// @return [0, count) 范围内的索引
inline int selectRandomMessageIndex(int previousIndex)
{
    const int count = messages().size();
    if (count <= 0)
        return -1;
    // 只生成一次随机候选值，去重逻辑委托给 resolveMessageIndex
    const int candidate = static_cast<int>(
        QRandomGenerator::global()->bounded(count));
    return resolveMessageIndex(candidate, previousIndex, count);
}

/// 判断当前小时是否在就寝时段（00:00 ≤ hour < 06:00）
inline bool isBedtimeHour(int hour)
{
    return hour >= 0 && hour < 6;
}

/// 判断冷却是否已过（lastReminderUtc ≤ 0 表示从未提醒过）
inline bool isPastCooldown(qint64 lastReminderUtc, qint64 nowUtc, int cooldownSecs = 3600)
{
    if (lastReminderUtc <= 0)
        return true;
    return (nowUtc - lastReminderUtc) >= cooldownSecs;
}

#ifdef Q_OS_WIN
/**
 * @brief 判断用户是否处于空闲状态。
 *
 * @param lastInputTick  LASTINPUTINFO::dwTime（32 位 tick，每 ~49.7 天回绕）
 * @param nowTick        当前 tick（GetTickCount64() 的低 32 位）
 * @param idleThresholdMs 空闲阈值（默认 10 分钟）
 *
 * DWORD 无符号减法天然处理 32 位回绕，仅适用于"最近一次输入"间隔。
 */
inline bool isUserIdle(DWORD lastInputTick, DWORD nowTick,
                       DWORD idleThresholdMs = 10 * 60 * 1000)
{
    const DWORD idleMs = nowTick - lastInputTick;  // DWORD 模减法
    return idleMs > idleThresholdMs;
}
#endif

/// 清洗自定义消息文本
inline QString sanitizeBedtimeMessage(const QString &msg)
{
    if (msg.isEmpty())
        return defaultMessage();

    QString result = msg;
    // 换行和制表符替换为空格
    result.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t]")),
                   QStringLiteral(" "));
    // 去除控制字符（保留空格 0x20 及以上可见字符）
    result.remove(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F\\x7F]")));
    // 截断至 200 字符
    if (result.length() > 200) {
        result = result.left(200);
    }
    // 清洗后若为空 → 恢复默认
    if (result.trimmed().isEmpty()) {
        return defaultMessage();
    }

    return result;
}

/// 根据模式选择提醒文案的结果
struct BedtimeSelection {
    QString message;
    int     messageIndex = -1;  ///< -1 表示从未选择过
};

/// 根据模式选择提醒文案（纯函数，App 层可在整个提醒流程中使用）
inline BedtimeSelection selectBedtimeMessage(bool useRandom, int previousIndex,
                                              const QString &customMessage)
{
    if (useRandom) {
        const int idx = selectRandomMessageIndex(previousIndex);
        return { messages().at(idx), idx };
    }
    // 固定模式：使用自定义文案（空则恢复默认），索引不变
    return { sanitizeBedtimeMessage(customMessage), previousIndex };
}

/// 判断当前是否可提醒（全屏策略 / AI 忙碌 / TTS 忙碌）
inline bool canRemindNow(bool fullscreenActive, bool fullscreenDisplayEnabled,
                         bool aiBusy, bool ttsBusy)
{
    // 全屏策略
    if (fullscreenActive && !fullscreenDisplayEnabled)
        return false;

    // AI 请求进行中
    if (aiBusy)
        return false;

    // TTS 统一繁忙检查
    if (ttsBusy)
        return false;

    return true;
}

} // namespace BedtimeHelper
