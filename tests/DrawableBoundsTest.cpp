// ── DrawableBoundsTest.cpp ──
// 纯函数测试：验证 DrawableBounds.h 中 AABB 计算、NaN/Inf 过滤、退化拒绝逻辑。
// 不依赖 Cubism SDK、OpenGL 或 Live2D 模型。
#include <QtTest>
#include <limits>

#include "../interaction/DrawableBounds.h"

class DrawableBoundsTest : public QObject
{
    Q_OBJECT

private slots:
    // ═══════════════════════════════════════════════════════════
    // AccumulateVertex — 正常顶点
    // ═══════════════════════════════════════════════════════════

    void test_accumulate_normal()
    {
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(0.0f, 0.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(10.0f, 20.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(5.0f, 15.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 3);
        QCOMPARE(mnX, 0.0f);
        QCOMPARE(mnY, 0.0f);
        QCOMPARE(mxX, 10.0f);
        QCOMPARE(mxY, 20.0f);
    }

    void test_accumulate_samePoint()
    {
        // 所有顶点相同 — 后续 Finalize 应拒绝
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(5.0f, 5.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(5.0f, 5.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(5.0f, 5.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 3);
        // 宽高为 0 → Finalize 应拒绝
    }

    void test_accumulate_negative()
    {
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(-10.0f, -20.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(0.0f, -5.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(-5.0f, 10.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 3);
        QCOMPARE(mnX, -10.0f);
        QCOMPARE(mnY, -20.0f);
        QCOMPARE(mxX, 0.0f);
        QCOMPARE(mxY, 10.0f);
    }

    // ═══════════════════════════════════════════════════════════
    // AccumulateVertex — NaN / Inf 拒绝
    // ═══════════════════════════════════════════════════════════

    void test_accumulate_nan()
    {
        const auto NaN = std::numeric_limits<float>::quiet_NaN();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(NaN, 5.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 0);
    }

    void test_accumulate_inf()
    {
        const auto Inf = std::numeric_limits<float>::infinity();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(3.0f, Inf, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 0);
    }

    void test_accumulate_negInf()
    {
        const auto Inf = std::numeric_limits<float>::infinity();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(-Inf, 5.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 0);
    }

    void test_accumulate_mixedValidInvalid()
    {
        const auto NaN = std::numeric_limits<float>::quiet_NaN();
        const auto Inf = std::numeric_limits<float>::infinity();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(0.0f, 0.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(NaN, 5.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(10.0f, 20.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(5.0f, Inf, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(3.0f, 15.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 3);
        QCOMPARE(mnX, 0.0f);
        QCOMPARE(mnY, 0.0f);
        QCOMPARE(mxX, 10.0f);
        QCOMPARE(mxY, 20.0f);
    }

    void test_accumulate_allNaN()
    {
        const auto NaN = std::numeric_limits<float>::quiet_NaN();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(NaN, NaN, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(NaN, NaN, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(NaN, NaN, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 0);
    }

    void test_accumulate_allInf()
    {
        const auto Inf = std::numeric_limits<float>::infinity();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(Inf, Inf, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(Inf, Inf, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(Inf, Inf, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 0);
    }

    // ═══════════════════════════════════════════════════════════
    // FinalizeDrawable — 有效 bounds
    // ═══════════════════════════════════════════════════════════

    void test_finalize_valid()
    {
        float mnX = 0.0f, mnY = 0.0f, mxX = 10.0f, mxY = 20.0f;
        QVERIFY(DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
        QCOMPARE(mnX, 0.0f);
        QCOMPARE(mnY, 0.0f);
        QCOMPARE(mxX, 10.0f);
        QCOMPARE(mxY, 20.0f);
    }

    void test_finalize_validLarge()
    {
        float mnX = -100.0f, mnY = -200.0f, mxX = 300.0f, mxY = 400.0f;
        QVERIFY(DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 100));
        QCOMPARE(mnX, -100.0f);
        QCOMPARE(mnY, -200.0f);
        QCOMPARE(mxX, 300.0f);
        QCOMPARE(mxY, 400.0f);
    }

    // ═══════════════════════════════════════════════════════════
    // FinalizeDrawable — 拒绝退化 bounds
    // ═══════════════════════════════════════════════════════════

    void test_finalize_zeroWidth()
    {
        float mnX = 0.0f, mnY = 0.0f, mxX = 0.0f, mxY = 20.0f;
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
    }

    void test_finalize_zeroHeight()
    {
        float mnX = 0.0f, mnY = 0.0f, mxX = 10.0f, mxY = 0.0f;
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
    }

    void test_finalize_tinyWidth()
    {
        float mnX = 0.0f, mnY = 0.0f, mxX = 0.0005f, mxY = 20.0f;
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
    }

    void test_finalize_tinyHeight()
    {
        float mnX = 0.0f, mnY = 0.0f, mxX = 10.0f, mxY = 0.0005f;
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
    }

    // ═══════════════════════════════════════════════════════════
    // FinalizeDrawable — 拒绝顶点数不足
    // ═══════════════════════════════════════════════════════════

    void test_finalize_fewVertices()
    {
        float mnX = 0.0f, mnY = 0.0f, mxX = 10.0f, mxY = 20.0f;
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 0));
    }

    void test_finalize_twoVertices()
    {
        float mnX = 0.0f, mnY = 0.0f, mxX = 10.0f, mxY = 20.0f;
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 2));
    }

    // ═══════════════════════════════════════════════════════════
    // FinalizeDrawable — Inf 钳制
    // ═══════════════════════════════════════════════════════════

    void test_finalize_infMinClamped()
    {
        const auto Inf = std::numeric_limits<float>::infinity();
        float mnX = -Inf, mnY = 0.0f, mxX = 10.0f, mxY = 20.0f;
        QVERIFY(DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
        QCOMPARE(mnX, 0.0f);   // clamped from -Inf
        QCOMPARE(mnY, 0.0f);
        QCOMPARE(mxX, 10.0f);
        QCOMPARE(mxY, 20.0f);
    }

    void test_finalize_infMaxClamped()
    {
        const auto Inf = std::numeric_limits<float>::infinity();
        float mnX = 0.0f, mnY = 0.0f, mxX = Inf, mxY = 20.0f;
        QVERIFY(DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
        QCOMPARE(mnX, 0.0f);
        QCOMPARE(mnY, 0.0f);
        QCOMPARE(mxX, 1.0f);   // clamped from Inf
        QCOMPARE(mxY, 20.0f);
    }

    // ═══════════════════════════════════════════════════════════
    // 完整流程：Accumulate + Finalize 组合
    // ═══════════════════════════════════════════════════════════

    void test_fullPipeline_valid()
    {
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(1.0f, 2.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(3.0f, 4.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(0.0f, 5.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 3);
        QVERIFY(DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, cnt));
        QCOMPARE(mnX, 0.0f);
        QCOMPARE(mnY, 2.0f);
        QCOMPARE(mxX, 3.0f);
        QCOMPARE(mxY, 5.0f);
    }

    void test_fullPipeline_allNaNRejected()
    {
        const auto NaN = std::numeric_limits<float>::quiet_NaN();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(NaN, NaN, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(NaN, NaN, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(NaN, NaN, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 0);
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, cnt));
    }

    void test_fullPipeline_allSamePointDegenerate()
    {
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(5.0f, 5.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(5.0f, 5.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(5.0f, 5.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 3);
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, cnt));
    }

    void test_fullPipeline_mixedWithNaN()
    {
        const auto NaN = std::numeric_limits<float>::quiet_NaN();
        float mnX = FLT_MAX, mnY = FLT_MAX, mxX = -FLT_MAX, mxY = -FLT_MAX;
        int cnt = 0;
        DrawableBounds::AccumulateVertex(10.0f, 20.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(NaN, NaN, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(30.0f, 40.0f, mnX, mnY, mxX, mxY, cnt);
        DrawableBounds::AccumulateVertex(5.0f, 15.0f, mnX, mnY, mxX, mxY, cnt);
        QCOMPARE(cnt, 3);
        QVERIFY(DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, cnt));
        QCOMPARE(mnX, 5.0f);
        QCOMPARE(mnY, 15.0f);
        QCOMPARE(mxX, 30.0f);
        QCOMPARE(mxY, 40.0f);
    }

    // ═══════════════════════════════════════════════════════════
    // 边界条件
    // ═══════════════════════════════════════════════════════════

    void test_finalize_nearlyDegenerate()
    {
        // 恰好在阈值上的边界值
        float mnX = 0.0f, mnY = 0.0f, mxX = 0.0009f, mxY = 10.0f;
        QVERIFY(!DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));

        // 略高于阈值
        mxX = 0.0011f;
        QVERIFY(DrawableBounds::FinalizeDrawable(mnX, mnY, mxX, mxY, 3));
    }
};

QTEST_MAIN(DrawableBoundsTest)
#include "DrawableBoundsTest.moc"
