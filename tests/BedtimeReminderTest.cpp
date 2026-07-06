#include <QtTest>
#include <QDateTime>

#include "../core/BedtimeHelper.h"

// ============================================================================
// BedtimeReminderTest — 深夜就寝提醒纯逻辑单元测试
//
// 测试 BedtimeHelper 中的所有纯函数，不依赖 App、TTS、PetWindow 等实例。
// ============================================================================

class BedtimeReminderTest : public QObject
{
    Q_OBJECT

private slots:
    // ── isBedtimeHour ──
    void test_isBedtimeHour_beforeMidnight();
    void test_isBedtimeHour_midnight();
    void test_isBedtimeHour_earlyMorning();
    void test_isBedtimeHour_sixAm();
    void test_isBedtimeHour_morning();

    // ── isPastCooldown ──
    void test_cooldown_neverReminded();
    void test_cooldown_59min59s();
    void test_cooldown_exactly60min();
    void test_cooldown_over60min();
    void test_cooldown_restartPreserves();

    // ── isUserIdle ──
    void test_idle_notIdle();
    void test_idle_exactly10min();
    void test_idle_over10min();
    void test_idle_dwordWraparound();

    // ── sanitizeBedtimeMessage ──
    void test_sanitize_empty_returnsDefault();
    void test_sanitize_normalText_unchanged();
    void test_sanitize_controlChars_removed();
    void test_sanitize_newlinesToSpaces();
    void test_sanitize_tabsToSpaces();
    void test_sanitize_over200chars_truncated();
    void test_sanitize_allWhitespace_returnsDefault();

    // ── canRemindNow ──
    void test_canRemindNow_normal();
    void test_canRemindNow_fullscreenActiveDisplayDisabled();
    void test_canRemindNow_fullscreenActiveDisplayEnabled();
    void test_canRemindNow_aiBusy();
    void test_canRemindNow_ttsBusy();
    void test_canRemindNow_allBlocking();

    // ── defaultMessage ──
    void test_defaultMessage_returnsExpected();

    // ── messages / selectRandomMessageIndex / selectBedtimeMessage ──
    void test_messages_poolSize();
    void test_random_validRange();
    void test_random_noConsecutiveRepeat();
    void test_random_previousIndexMinusOne_returnsValid();
    void test_resolveMessageIndex_deterministic();
    void test_random_previousIndexOutOfBounds();
    void test_selectMessage_randomMode();
    void test_selectMessage_fixedMode();
    void test_selectMessage_fixedModeEmpty();
    void test_selectMessage_fixedModeMinusOne();
};

// ============================================================================
// isBedtimeHour tests
// ============================================================================

void BedtimeReminderTest::test_isBedtimeHour_beforeMidnight()
{
    // 23:59 → 不在时段内
    QVERIFY(!BedtimeHelper::isBedtimeHour(23));
    QVERIFY(!BedtimeHelper::isBedtimeHour(-1));
}

void BedtimeReminderTest::test_isBedtimeHour_midnight()
{
    // 00:00 → 在时段内
    QVERIFY(BedtimeHelper::isBedtimeHour(0));
}

void BedtimeReminderTest::test_isBedtimeHour_earlyMorning()
{
    // 05:59 → 在时段内
    QVERIFY(BedtimeHelper::isBedtimeHour(5));
}

void BedtimeReminderTest::test_isBedtimeHour_sixAm()
{
    // 06:00 → 不在时段内
    QVERIFY(!BedtimeHelper::isBedtimeHour(6));
}

void BedtimeReminderTest::test_isBedtimeHour_morning()
{
    // 07:00 → 不在时段内
    QVERIFY(!BedtimeHelper::isBedtimeHour(7));
}

// ============================================================================
// isPastCooldown tests
// ============================================================================

void BedtimeReminderTest::test_cooldown_neverReminded()
{
    // lastReminderUtc ≤ 0 → 从未提醒，可以提醒
    QVERIFY(BedtimeHelper::isPastCooldown(0, QDateTime::currentSecsSinceEpoch(), 3600));
    QVERIFY(BedtimeHelper::isPastCooldown(-1, QDateTime::currentSecsSinceEpoch(), 3600));
}

void BedtimeReminderTest::test_cooldown_59min59s()
{
    const qint64 now = 1000000;
    const qint64 last = now - 3599;  // 59 分 59 秒前
    QVERIFY(!BedtimeHelper::isPastCooldown(last, now, 3600));
}

void BedtimeReminderTest::test_cooldown_exactly60min()
{
    const qint64 now = 1000000;
    const qint64 last = now - 3600;  // 正好 60 分钟前
    QVERIFY(BedtimeHelper::isPastCooldown(last, now, 3600));
}

void BedtimeReminderTest::test_cooldown_over60min()
{
    const qint64 now = 1000000;
    const qint64 last = now - 7200;  // 2 小时前
    QVERIFY(BedtimeHelper::isPastCooldown(last, now, 3600));
}

void BedtimeReminderTest::test_cooldown_restartPreserves()
{
    // 模拟程序重启后从 QSettings 读取 lastReminderUtc
    // 假设上次提醒是在 1800 秒前（30 分钟），重启后仍应遵守冷却
    const qint64 now = 2000000;
    const qint64 lastFromSettings = now - 1800;  // 30 分钟前
    QVERIFY(!BedtimeHelper::isPastCooldown(lastFromSettings, now, 3600));

    // 假设上次提醒是 7200 秒前（2 小时），重启后冷却已过
    const qint64 oldReminder = now - 7200;
    QVERIFY(BedtimeHelper::isPastCooldown(oldReminder, now, 3600));
}

// ============================================================================
// isUserIdle tests
// ============================================================================

void BedtimeReminderTest::test_idle_notIdle()
{
    // 1 秒前有输入 → 不空闲
    const DWORD now = 100000;
    QVERIFY(!BedtimeHelper::isUserIdle(now - 1000, now, 10 * 60 * 1000));
}

void BedtimeReminderTest::test_idle_exactly10min()
{
    // 正好 10 分钟（600,000 ms）→ 不空闲（等于阈值不算超过）
    const DWORD now = 10000000;
    const DWORD threshold = 10 * 60 * 1000;  // 600,000
    QVERIFY(!BedtimeHelper::isUserIdle(now - threshold, now, threshold));
}

void BedtimeReminderTest::test_idle_over10min()
{
    // 10 分钟 + 1 秒 → 空闲
    const DWORD now = 10000000;
    QVERIFY(BedtimeHelper::isUserIdle(now - 600001, now, 10 * 60 * 1000));
}

void BedtimeReminderTest::test_idle_dwordWraparound()
{
    // 模拟 dwTime 回绕：lastInput = 0xFFFFFF00, now = 0x00000100
    // DWORD 无符号减法：0x00000100 - 0xFFFFFF00 = 0x00000200 = 512 ms
    // 实际空闲时间 = (2^32 - 0xFFFFFF00) + 0x00000100 = 256 + 256 = 512ms
    const DWORD lastInputTick = 0xFFFFFF00u;
    const DWORD nowTick = 0x00000100u;
    // 512ms < 10min → 应判定为不空闲
    QVERIFY(!BedtimeHelper::isUserIdle(lastInputTick, nowTick, 10 * 60 * 1000));

    // 回绕后空闲超过 10 分钟：lastInput = 0xFFFF0000, now = 0x0000EA60
    // idleMs = 0x0000EA60 - 0xFFFF0000 = 0x0000EA60 (mod 2^32) ... wait
    // Actually: 0x0000EA60 - 0xFFFF0000 with DWORD wrap: 0x0000EA60 + (2^32 - 0xFFFF0000)
    // = 0x0000EA60 + 0x00010000 = 0x0001EA60 = 125536 ms ≈ 2 min
    // That's less than 10 min. Let me use a bigger difference.
    //
    // idleMs = 0x000F0000 → ~15 min, lastInput = 0xFFFF0000
    // nowTick = 0xFFFF0000 + 0x000F0000 = 0x000E0000 (wraps around)
    // DWORD: 0x000E0000 - 0xFFFF0000 = 0x000F0000 = 983040 ms ≈ 16 min > 10 min
    const DWORD last2 = 0xFFFF0000u;
    const DWORD now2 = 0x000E0000u;  // ~16 min later in DWORD arithmetic
    QVERIFY(BedtimeHelper::isUserIdle(last2, now2, 10 * 60 * 1000));
}

// ============================================================================
// sanitizeBedtimeMessage tests
// ============================================================================

void BedtimeReminderTest::test_sanitize_empty_returnsDefault()
{
    const QString result = BedtimeHelper::sanitizeBedtimeMessage(QString());
    QCOMPARE(result, BedtimeHelper::defaultMessage());
}

void BedtimeReminderTest::test_sanitize_normalText_unchanged()
{
    const QString input = QStringLiteral("主人，已经很晚啦，早点休息吧喵～");
    const QString result = BedtimeHelper::sanitizeBedtimeMessage(input);
    QCOMPARE(result, input);
}

void BedtimeReminderTest::test_sanitize_controlChars_removed()
{
    // 包含控制字符 \x01, \x02, \x7F
    // 使用字符串字面量拼接避免 \x7F 后的字符被误解析为十六进制
    const QString input = QStringLiteral("hello\x01world\x02test")
                        + QChar(0x7F) + QStringLiteral("end");
    const QString result = BedtimeHelper::sanitizeBedtimeMessage(input);
    QVERIFY(!result.contains(QChar(0x01)));
    QVERIFY(!result.contains(QChar(0x02)));
    QVERIFY(!result.contains(QChar(0x7F)));
    QCOMPARE(result, QStringLiteral("helloworldtestend"));
}

void BedtimeReminderTest::test_sanitize_newlinesToSpaces()
{
    const QString input = QStringLiteral("line1\nline2\r\nline3");
    const QString result = BedtimeHelper::sanitizeBedtimeMessage(input);
    QVERIFY(!result.contains('\n'));
    QVERIFY(!result.contains('\r'));
    QCOMPARE(result, QStringLiteral("line1 line2  line3"));
}

void BedtimeReminderTest::test_sanitize_tabsToSpaces()
{
    const QString input = QStringLiteral("col1\tcol2");
    const QString result = BedtimeHelper::sanitizeBedtimeMessage(input);
    QVERIFY(!result.contains('\t'));
    QCOMPARE(result, QStringLiteral("col1 col2"));
}

void BedtimeReminderTest::test_sanitize_over200chars_truncated()
{
    // 构造 250 个字符的字符串
    QString input;
    input.fill('A', 250);
    const QString result = BedtimeHelper::sanitizeBedtimeMessage(input);
    QCOMPARE(result.length(), 200);
    QVERIFY(result.count('A') == 200);
}

void BedtimeReminderTest::test_sanitize_allWhitespace_returnsDefault()
{
    const QString input = QStringLiteral("   \t\n\r   ");
    const QString result = BedtimeHelper::sanitizeBedtimeMessage(input);
    QCOMPARE(result, BedtimeHelper::defaultMessage());
}

// ============================================================================
// canRemindNow tests
// ============================================================================

void BedtimeReminderTest::test_canRemindNow_normal()
{
    QVERIFY(BedtimeHelper::canRemindNow(false, true, false, false));
}

void BedtimeReminderTest::test_canRemindNow_fullscreenActiveDisplayDisabled()
{
    QVERIFY(!BedtimeHelper::canRemindNow(true, false, false, false));
}

void BedtimeReminderTest::test_canRemindNow_fullscreenActiveDisplayEnabled()
{
    // 开启全屏显示桌宠，即使在全屏中也可提醒
    QVERIFY(BedtimeHelper::canRemindNow(true, true, false, false));
}

void BedtimeReminderTest::test_canRemindNow_aiBusy()
{
    QVERIFY(!BedtimeHelper::canRemindNow(false, true, true, false));
}

void BedtimeReminderTest::test_canRemindNow_ttsBusy()
{
    QVERIFY(!BedtimeHelper::canRemindNow(false, true, false, true));
}

void BedtimeReminderTest::test_canRemindNow_allBlocking()
{
    QVERIFY(!BedtimeHelper::canRemindNow(true, false, true, true));
}

// ============================================================================
// defaultMessage tests
// ============================================================================

void BedtimeReminderTest::test_defaultMessage_returnsExpected()
{
    QCOMPARE(BedtimeHelper::defaultMessage(),
             QStringLiteral("主人，已经很晚啦，早点休息吧喵～"));
}

// ============================================================================
// messages / selectRandomMessageIndex / selectBedtimeMessage tests
// ============================================================================

void BedtimeReminderTest::test_messages_poolSize()
{
    QCOMPARE(BedtimeHelper::messages().size(), 16);
    // 默认文案与池中第一条一致
    QCOMPARE(BedtimeHelper::defaultMessage(), BedtimeHelper::messages().at(0));
}

void BedtimeReminderTest::test_random_validRange()
{
    // 多次随机，所有索引均在 [0, 15] 内
    for (int i = 0; i < 500; ++i) {
        const int idx = BedtimeHelper::selectRandomMessageIndex(
            QRandomGenerator::global()->bounded(16));
        QVERIFY2(idx >= 0 && idx < 16,
                 qPrintable(QStringLiteral("索引 %1 超出范围").arg(idx)));
    }
}

void BedtimeReminderTest::test_random_noConsecutiveRepeat()
{
    // 以 5 为上一次索引，连续 200 次随机均不得再现 5
    const int previousIndex = 5;
    for (int i = 0; i < 200; ++i) {
        const int idx = BedtimeHelper::selectRandomMessageIndex(previousIndex);
        QVERIFY2(idx != previousIndex,
                 qPrintable(QStringLiteral("连续重复索引 %1").arg(idx)));
    }
}

void BedtimeReminderTest::test_selectMessage_randomMode()
{
    // 随机模式：返回内置文案，索引改变
    const int prev = 3;
    const auto result = BedtimeHelper::selectBedtimeMessage(true, prev, QStringLiteral("自定义"));
    QVERIFY(BedtimeHelper::messages().contains(result.message));
    QVERIFY(result.messageIndex >= 0 && result.messageIndex < 16);
    // 索引不应与 prev 连续重复
    QVERIFY(result.messageIndex != prev);
}

void BedtimeReminderTest::test_selectMessage_fixedMode()
{
    // 固定模式：返回清洗后的自定义文案，索引不变
    const auto result = BedtimeHelper::selectBedtimeMessage(
        false, 7, QStringLiteral("晚安喵～"));
    QCOMPARE(result.message, QStringLiteral("晚安喵～"));
    QCOMPARE(result.messageIndex, 7);
}

void BedtimeReminderTest::test_selectMessage_fixedModeEmpty()
{
    // 固定模式 + 空文案 → 返回默认，索引不变
    const auto result = BedtimeHelper::selectBedtimeMessage(false, 9, QString());
    QCOMPARE(result.message, BedtimeHelper::defaultMessage());
    QCOMPARE(result.messageIndex, 9);
}

void BedtimeReminderTest::test_random_previousIndexMinusOne_returnsValid()
{
    // previousIndex=-1 表示从未选择过 → 应返回有效索引
    for (int i = 0; i < 200; ++i) {
        const int idx = BedtimeHelper::selectRandomMessageIndex(-1);
        QVERIFY2(idx >= 0 && idx < 16,
                 qPrintable(QStringLiteral("-1 返回了越界索引 %1").arg(idx)));
    }
}

void BedtimeReminderTest::test_resolveMessageIndex_deterministic()
{
    constexpr int COUNT = 16;

    // ── count 边界 ──
    // 1. count=0 返回 -1
    QCOMPARE(BedtimeHelper::resolveMessageIndex(0, -1, 0), -1);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(5, 3, 0), -1);

    // 2. count=1 返回 0（无论 candidate、previousIndex 为何值）
    QCOMPARE(BedtimeHelper::resolveMessageIndex(0, -1, 1), 0);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(0, 0, 1), 0);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(5, 0, 1), 0);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(-3, -1, 1), 0);

    // ── previousIndex = -1：不排除任何索引 ──
    // 3. 候选值 0 可以直接返回 0
    QCOMPARE(BedtimeHelper::resolveMessageIndex(0, -1, COUNT), 0);

    // 4. 全部合法候选保持不变
    for (int c = 0; c < COUNT; ++c)
        QCOMPARE(BedtimeHelper::resolveMessageIndex(c, -1, COUNT), c);

    // ── previousIndex 越界：按无历史记录处理 ──
    // 5. 越界时全部合法候选保持不变
    for (int c = 0; c < COUNT; ++c)
        QCOMPARE(BedtimeHelper::resolveMessageIndex(c, 999, COUNT), c);
    for (int c = 0; c < COUNT; ++c)
        QCOMPARE(BedtimeHelper::resolveMessageIndex(c, -2, COUNT), c);

    // ── 有效 previousIndex，candidate ≠ previousIndex ──
    // 6. candidate 与 previousIndex 不同时保持不变
    QCOMPARE(BedtimeHelper::resolveMessageIndex(3, 5, COUNT), 3);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(0, 5, COUNT), 0);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(15, 5, COUNT), 15);

    // ── 有效 previousIndex，candidate == previousIndex ──
    // 7. candidate 与 previousIndex 相同时映射到其他合法索引
    QVERIFY(BedtimeHelper::resolveMessageIndex(5, 5, COUNT) != 5);
    QVERIFY(BedtimeHelper::resolveMessageIndex(0, 0, COUNT) != 0);
    QVERIFY(BedtimeHelper::resolveMessageIndex(15, 15, COUNT) != 15);
    // 确定性：相同输入应得相同输出
    QCOMPARE(BedtimeHelper::resolveMessageIndex(5, 5, COUNT),
             BedtimeHelper::resolveMessageIndex(5, 5, COUNT));

    // 8. previousIndex 边界（0 和 count-1）
    QVERIFY(BedtimeHelper::resolveMessageIndex(0, 0, COUNT) != 0);
    QVERIFY(BedtimeHelper::resolveMessageIndex(COUNT - 1, COUNT - 1, COUNT) != COUNT - 1);

    // ── candidate 越界：安全规范化 ──
    // 9. candidate 越界时自动映射到 [0, count)
    QCOMPARE(BedtimeHelper::resolveMessageIndex(-1, -1, COUNT), COUNT - 1);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(16, -1, COUNT), 0);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(-16, -1, COUNT), 0);
    QCOMPARE(BedtimeHelper::resolveMessageIndex(32, -1, COUNT), 0);
}

void BedtimeReminderTest::test_random_previousIndexOutOfBounds()
{
    // previousIndex >= 文案数量 → 视为从未选择过，安全返回有效索引
    for (int i = 0; i < 200; ++i) {
        const int idx = BedtimeHelper::selectRandomMessageIndex(999);
        QVERIFY2(idx >= 0 && idx < 16,
                 qPrintable(QStringLiteral("越界 previousIndex 返回了 %1").arg(idx)));
    }
}

void BedtimeReminderTest::test_selectMessage_fixedModeMinusOne()
{
    // 固定模式 + previousIndex=-1 → 使用自定义文案，索引保持 -1
    const auto result = BedtimeHelper::selectBedtimeMessage(
        false, -1, QStringLiteral("晚安喵～"));
    QCOMPARE(result.message, QStringLiteral("晚安喵～"));
    QCOMPARE(result.messageIndex, -1);
}

// ============================================================================
QTEST_MAIN(BedtimeReminderTest)
#include "BedtimeReminderTest.moc"
