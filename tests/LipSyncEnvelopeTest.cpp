// ── LipSyncEnvelope unit tests ──────────────────────────────────────────
// Pure logic tests for the lip-sync envelope algorithm:
//   - No Cubism SDK
//   - No OpenGL / Qt Multimedia
//   - No audio device or mock server
//   - Only Qt6::Test (QObject) + Qt6::Core (QRandomGenerator)

#include <QObject>
#include <QTest>
#include <QtMath>
#include <cmath>

#include "pet/LipSyncEnvelope.h"

// ═════════════════════════════════════════════════════════════════════════
// Deterministic random source for reproducible tests
// ═════════════════════════════════════════════════════════════════════════
static float g_testRandomValues[] = {
    0.12f, 0.55f, 0.83f, 0.01f, 0.92f,
    0.34f, 0.67f, 0.45f, 0.78f, 0.22f,
};
static int g_testRandomIndex = 0;
static float g_testRandomLimit = -1.0f;  // -1 = unlimited

static float testRandom()
{
    if (g_testRandomLimit >= 0.0f) {
        // For tests that want to control close duration precisely
        return g_testRandomLimit;
    }
    float val = g_testRandomValues[g_testRandomIndex % 10];
    ++g_testRandomIndex;
    return val;
}

static void resetTestRandom()
{
    g_testRandomIndex = 0;
    g_testRandomLimit = -1.0f;
}

// ═════════════════════════════════════════════════════════════════════════

class LipSyncEnvelopeTest : public QObject
{
    Q_OBJECT

private slots:
    // ── 1. 默认状态输出为 0 ──
    void test_defaultValueIsZero();

    // ── 2. setSpeaking(true) 后输出始终在合法范围 ──
    void test_outputInRange();

    // ── 3. 能经历张嘴与闭嘴两个阶段 ──
    void test_alternatesBetweenOpenAndClose();

    // ── 4. setSpeaking(false) 后平滑回到 0 ──
    void test_releaseReturnsToZero();

    // ── 5. reset() 后状态完全复位 ──
    void test_resetResetsCompletely();

    // ── 6. 大 deltaTime 下不产生 NaN、Inf 或越界值 ──
    void test_extremeDeltaTimeSafe();

    // ── 7. 注入固定随机源后结果可重复 ──
    void test_deterministicRandom();

    // ── 8. 空闲状态长时间更新仍保持 0 ──
    void test_idleStaysZero();

    // ── 9. 连续 start/stop 不会卡在张嘴状态 ──
    void test_startStopDoesNotStick();

private:
    /// Run many frames to let the envelope stabilize
    void runFrames(LipSyncEnvelope &e, int count, float dt = 1.0f / 60.0f);
};

// ═════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::runFrames(LipSyncEnvelope &e, int count, float dt)
{
    for (int i = 0; i < count; ++i) {
        e.update(dt);
    }
    Q_UNUSED(QTest::qWait(0)); // allow events to process
}

// ═════════════════════════════════════════════════════════════════════════
// 1. 默认状态输出为 0
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_defaultValueIsZero()
{
    LipSyncEnvelope e;
    QCOMPARE(e.currentValue(), 0.0f);
    QCOMPARE(e.isSpeaking(), false);

    // Update while idle → stays 0
    QCOMPARE(e.update(0.016f), 0.0f);
    QCOMPARE(e.currentValue(), 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════
// 2. setSpeaking(true) 后输出始终在合法范围 [0, 1]
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_outputInRange()
{
    LipSyncEnvelope e;
    e.setRandomSource(&testRandom);
    resetTestRandom();

    e.setSpeaking(true);
    QVERIFY(e.isSpeaking());

    // Run many frames — each output must be in [0, 1]
    const int totalFrames = 300; // ~5 seconds @ 60fps
    for (int i = 0; i < totalFrames; ++i) {
        const float val = e.update(1.0f / 60.0f);
        QVERIFY2(val >= 0.0f && val <= 1.0f,
                 qPrintable(QStringLiteral("Frame %1: value %2 out of range [0,1]")
                            .arg(i).arg(val)));
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 3. 能经历张嘴与闭嘴两个阶段
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_alternatesBetweenOpenAndClose()
{
    LipSyncEnvelope e;
    e.setRandomSource(&testRandom);
    resetTestRandom();

    e.setSpeaking(true);

    // Run enough frames to see at least 2 full open-close cycles
    // (each cycle ~120-320ms, 300 frames @ 60fps = 5s → enough)
    float prevVal = e.update(1.0f / 60.0f);

    bool seenRise = false;    // value went up (open phase)
    bool seenFall = false;    // value went down (close phase)
    bool seenRiseAgain = false; // second open phase

    for (int i = 0; i < 400; ++i) {
        const float val = e.update(1.0f / 60.0f);
        if (val > prevVal + 0.01f) {
            if (seenFall) seenRiseAgain = true;
            else seenRise = true;
        }
        if (val < prevVal - 0.01f && seenRise) {
            seenFall = true;
        }
        prevVal = val;
    }

    QVERIFY2(seenRise, "Value never rose above 0 — no open phase detected");
    QVERIFY2(seenFall, "Value never fell after rising — no close phase detected");
    QVERIFY2(seenRiseAgain, "Value never rose a second time — no alternating cycles");
}

// ═════════════════════════════════════════════════════════════════════════
// 4. setSpeaking(false) 后平滑回到 0
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_releaseReturnsToZero()
{
    LipSyncEnvelope e;
    e.setRandomSource(&testRandom);
    resetTestRandom();

    e.setSpeaking(true);

    // Let it speak for a while to reach non-zero
    for (int i = 0; i < 100; ++i) {
        e.update(1.0f / 60.0f);
    }

    // Confirm value is non-zero
    QVERIFY(e.currentValue() > 0.01f);

    // Stop speaking
    e.setSpeaking(false);

    // Run release frames — should decay to 0 quickly
    int framesToZero = 0;
    bool reachedZero = false;
    for (int i = 0; i < 200; ++i) {
        const float val = e.update(1.0f / 60.0f);
        if (val < 0.001f && !reachedZero) {
            reachedZero = true;
            framesToZero = i;
        }
    }

    QVERIFY2(reachedZero,
             "Envelope never returned to zero after setSpeaking(false)");
    QVERIFY2(framesToZero <= 60,
             qPrintable(QStringLiteral("Took %1 frames to reach zero (≥60 = too slow)")
                        .arg(framesToZero)));

    // After release, output stays 0
    QCOMPARE(e.currentValue(), 0.0f);
    for (int i = 0; i < 100; ++i) {
        QCOMPARE(e.update(1.0f / 60.0f), 0.0f);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 5. reset() 后状态完全复位
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_resetResetsCompletely()
{
    LipSyncEnvelope e;
    e.setRandomSource(&testRandom);
    resetTestRandom();

    // Activate and run for a while
    e.setSpeaking(true);
    for (int i = 0; i < 100; ++i) {
        e.update(1.0f / 60.0f);
    }

    // Reset
    e.reset();

    // Verify all state is back to default
    QCOMPARE(e.isSpeaking(), false);
    QCOMPARE(e.currentValue(), 0.0f);

    // Update while reset → still 0
    QCOMPARE(e.update(0.016f), 0.0f);
    QCOMPARE(e.currentValue(), 0.0f);

    // Re-activate after reset should work normally
    e.setSpeaking(true);
    QVERIFY(e.isSpeaking());
    bool becameNonZero = false;
    for (int i = 0; i < 60; ++i) {
        if (e.update(1.0f / 60.0f) > 0.01f) {
            becameNonZero = true;
            break;
        }
    }
    QVERIFY2(becameNonZero, "After reset → setSpeaking(true), value never exceeded 0.01");
}

// ═════════════════════════════════════════════════════════════════════════
// 6. 大 deltaTime 下不产生 NaN、Inf 或越界值
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_extremeDeltaTimeSafe()
{
    LipSyncEnvelope e;

    // ── Speaking with extreme dt ──
    e.setSpeaking(true);

    // dt = 0
    float v = e.update(0.0f);
    QVERIFY(!std::isnan(v));
    QVERIFY(!std::isinf(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    // dt = negative
    v = e.update(-1.0f);
    QVERIFY(!std::isnan(v));
    QVERIFY(!std::isinf(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    // dt = huge (10 seconds — clamped to 100ms by implementation)
    v = e.update(10.0f);
    QVERIFY(!std::isnan(v));
    QVERIFY(!std::isinf(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    // dt = NaN
    v = e.update(std::nanf(""));
    QVERIFY(!std::isnan(v));      // should be rejected
    QVERIFY(!std::isinf(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    // dt = Inf
    v = e.update(std::numeric_limits<float>::infinity());
    QVERIFY(!std::isnan(v));
    QVERIFY(!std::isinf(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    // ── Release with extreme dt ──
    e.setSpeaking(false);

    v = e.update(0.0f);
    QVERIFY(!std::isnan(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    v = e.update(10.0f);
    QVERIFY(!std::isnan(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    v = e.update(std::nanf(""));
    QVERIFY(!std::isnan(v));
    QVERIFY(v >= 0.0f && v <= 1.0f);

    // ── Idle with extreme dt ──
    e.reset();
    v = e.update(std::numeric_limits<float>::infinity());
    QCOMPARE(v, 0.0f);

    v = e.update(std::nanf(""));
    QCOMPARE(v, 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════
// 7. 注入固定随机源后结果可重复
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_deterministicRandom()
{
    // Run two instances with identical random source → identical results
    LipSyncEnvelope e1;
    LipSyncEnvelope e2;

    float seq1[120];
    float seq2[120];

    // First run
    {
        resetTestRandom();
        e1.setRandomSource(&testRandom);
        e1.setSpeaking(true);
        for (int i = 0; i < 120; ++i) {
            seq1[i] = e1.update(1.0f / 60.0f);
        }
    }

    // Second run
    {
        resetTestRandom();
        e2.setRandomSource(&testRandom);
        e2.setSpeaking(true);
        for (int i = 0; i < 120; ++i) {
            seq2[i] = e2.update(1.0f / 60.0f);
        }
    }

    // Compare
    for (int i = 0; i < 120; ++i) {
        QCOMPARE(seq1[i], seq2[i]);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 8. 空闲状态长时间更新仍保持 0
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_idleStaysZero()
{
    LipSyncEnvelope e;

    // Idle for 1000 frames (~16 seconds)
    for (int i = 0; i < 1000; ++i) {
        float dt = 1.0f / 60.0f;
        // Vary dt randomly to stress the idle path
        if (i % 3 == 0) dt = 1.0f / 30.0f;
        if (i % 7 == 0) dt = 1.0f / 120.0f;
        QCOMPARE(e.update(dt), 0.0f);
    }
    QCOMPARE(e.currentValue(), 0.0f);
    QCOMPARE(e.isSpeaking(), false);
}

// ═════════════════════════════════════════════════════════════════════════
// 9. 连续 start/stop 不会卡在张嘴状态
// ═════════════════════════════════════════════════════════════════════════

void LipSyncEnvelopeTest::test_startStopDoesNotStick()
{
    LipSyncEnvelope e;
    e.setRandomSource(&testRandom);
    resetTestRandom();

    for (int cycle = 0; cycle < 10; ++cycle) {
        // Start speaking
        e.setSpeaking(true);
        // Let it run for a few frames (might be in open phase)
        for (int f = 0; f < 30; ++f) {
            e.update(1.0f / 60.0f);
        }

        // Stop
        e.setSpeaking(false);

        // Run release frames
        for (int f = 0; f < 60; ++f) {
            e.update(1.0f / 60.0f);
        }

        // After each cycle: should be back to 0
        QVERIFY2(e.currentValue() < 0.01f,
                 qPrintable(QStringLiteral("Cycle %1: value %2 did not release to ~0")
                            .arg(cycle).arg(e.currentValue())));
    }

    // Final state: not speaking, value 0
    QCOMPARE(e.isSpeaking(), false);
    QCOMPARE(e.currentValue(), 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════

QTEST_MAIN(LipSyncEnvelopeTest)
#include "LipSyncEnvelopeTest.moc"
