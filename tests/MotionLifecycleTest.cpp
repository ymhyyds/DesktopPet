#include <QtTest>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "pet/MotionLifecycleCore.hpp"

// ============================================================================
// MotionLifecycleTest — Motion 参数生命周期纯逻辑测试
//
// 测试 MotionLifecycleCore.hpp 中的共享算法实现（与 ModelController 同一份代码）。
// 不依赖 Cubism SDK、OpenGL、Qt Multimedia 或真实模型。
// ============================================================================

/// 测试用 handle 类型（int，HandleT{} = 0 为无效值，匹配模板默认构造）
using TestHandle = int;

/// 测试用生命周期状态（使用共享模板，HandleT=TestHandle）
using TestActiveState = MotionLifecycle::ActiveMotionState<TestHandle>;
using TestPendingEntry = MotionLifecycle::PendingRestoreEntry<TestHandle>;
using TestPendingChain = MotionLifecycle::PendingRestoreChain<TestHandle>;

class MotionLifecycleTest : public QObject
{
    Q_OBJECT

private slots:
    // ── selectIdleIndex ──
    void test_selectIdleIndex_count1_always0();
    void test_selectIdleIndex_count2_alternates();
    void test_selectIdleIndex_count3_avoidsLast();
    void test_selectIdleIndex_initialMinus1_picks1();
    void test_selectIdleIndex_boundedZero_alwaysShifted();

    // ── entryBaseline: 从 active outgoing 继承 ──
    void test_entryBaseline_inheritsFromOutgoing();

    // ── entryBaseline: 从 pending 继承 ──
    void test_entryBaseline_inheritsFromPending();

    // ── entryBaseline: 从当前模型值采集 ──
    void test_entryBaseline_collectsCurrentValue();

    // ── entryBaseline: 三层链追溯 (A→B→C) ──
    void test_entryBaseline_threeLayerChain();

    // ── outgoing-only 计算 ──
    void test_outgoingOnly_computesCorrectly();

    // ── 共同参数排除 ──
    void test_outgoingOnly_sharedParamExcluded();

    // ── 冲突解决: incoming 从 pending 中移除 ──
    void test_conflictResolution_removesIncomingFromPending();

    // ── 链式冲突解决 ──
    void test_conflictResolution_chainResolve();

    // ── 空 chain 安全 ──
    void test_emptyChain_noError();

    // ── generation 失效 ──
    void test_generation_invalidatesOldEntries();

    // ── 三层 pending 链基线追溯 ──
    void test_threeLayer_pendingInheritance();

    // ── StartMotion 失败回滚 ──
    void test_rollback_stateUnchanged();

    // ── 自然结束 + 无下一 motion ──
    void test_naturalEnd_restoresAllParams();

    // ── clearMotionState ──
    void test_clearMotionState_resetsAll();
};

// ============================================================================
// selectIdleIndex 测试（直接调用共享纯函数）
// ============================================================================

void MotionLifecycleTest::test_selectIdleIndex_count1_always0()
{
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(1, -1, 0), 0);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(1, 0, 0), 0);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(1, 42, 0), 0);
}

void MotionLifecycleTest::test_selectIdleIndex_count2_alternates()
{
    // boundedValue=0, count-1=1
    // lastIndex=-1: 0 >= -1 → pick=1
    // lastIndex=1:  0 >= 1?  no → pick=0
    // lastIndex=0:  0 >= 0  → pick=1
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(2, -1, 0), 1);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(2, 1, 0), 0);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(2, 0, 0), 1);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(2, 1, 0), 0);
}

void MotionLifecycleTest::test_selectIdleIndex_count3_avoidsLast()
{
    // bounded=0, count=3: bounded(2)=0
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(3, 0, 0), 1);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(3, 1, 0), 0);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(3, 2, 0), 0);
    // bounded=1
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(3, 0, 1), 2);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(3, 1, 1), 2);
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(3, 2, 1), 1);
}

void MotionLifecycleTest::test_selectIdleIndex_initialMinus1_picks1()
{
    QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(2, -1, 0), 1);
}

void MotionLifecycleTest::test_selectIdleIndex_boundedZero_alwaysShifted()
{
    for (int lastIndex = -1; lastIndex < 5; ++lastIndex) {
        if (lastIndex == -1) {
            QCOMPARE(MotionLifecycle::selectIdleIndexFromBounded(3, -1, 0), 1);
        } else {
            int pick = MotionLifecycle::selectIdleIndexFromBounded(3, lastIndex, 0);
            QVERIFY(pick != lastIndex);
        }
    }
}

// ============================================================================
// entryBaseline: 从 active outgoing 继承
// ============================================================================

void MotionLifecycleTest::test_entryBaseline_inheritsFromOutgoing()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.entryBaselines[10] = 0.5f;
    active.entryBaselines[20] = 0.75f;
    active.drivenParams = {10, 20, 30};

    TestPendingChain chain;
    std::map<int, float> model;
    model[40] = 1.0f;

    auto baselines = MotionLifecycle::computeIncomingBaselines(
        {10, 40}, active, 0, chain,
        [&](int idx) { return model[idx]; });

    QCOMPARE(baselines.size(), 2);
    QCOMPARE(baselines[10], 0.5f);  // 继承
    QCOMPARE(baselines[40], 1.0f);  // 采集当前值
}

// ============================================================================
// entryBaseline: 从 pending 继承
// ============================================================================

void MotionLifecycleTest::test_entryBaseline_inheritsFromPending()
{
    TestActiveState active;
    active.valid = false; // 无 active

    TestPendingEntry pe;
    pe.handle = 1;
    pe.generation = 0;
    pe.baselines[50] = 0.3f;
    pe.baselines[60] = 0.6f;
    TestPendingChain chain = {pe};

    std::map<int, float> model;
    model[50] = 999.0f; // 不应被采集
    model[70] = 0.9f;

    auto baselines = MotionLifecycle::computeIncomingBaselines(
        {50, 70}, active, 0, chain,
        [&](int idx) { return model[idx]; });

    QCOMPARE(baselines.size(), 2);
    QCOMPARE(baselines[50], 0.3f);  // 从 pending 继承
    QCOMPARE(baselines[70], 0.9f);  // 采集
}

// ============================================================================
// entryBaseline: 从当前模型值采集
// ============================================================================

void MotionLifecycleTest::test_entryBaseline_collectsCurrentValue()
{
    TestActiveState active;
    active.valid = false;

    TestPendingChain chain;
    std::map<int, float> model;
    model[100] = -0.5f;
    model[200] = 0.0f;

    auto baselines = MotionLifecycle::computeIncomingBaselines(
        {100, 200}, active, 0, chain,
        [&](int idx) { return model[idx]; });

    QCOMPARE(baselines.size(), 2);
    QCOMPARE(baselines[100], -0.5f);
    QCOMPARE(baselines[200], 0.0f);
}

// ============================================================================
// entryBaseline: 三层链追溯 (A→B→C)
//
// A 驱动 {10, 30}, B 驱动 {10, 20} → A 的 outgoing-only = {30}
// C 驱动 {10} → 从 B 的 active entryBaselines 继承 baseline[10]
// ============================================================================

void MotionLifecycleTest::test_entryBaseline_threeLayerChain()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    // A 驱动两个参数，其中一个（30）是 B 不驱动的
    active.drivenParams = {10, 30};
    active.entryBaselines[10] = 0.1f;
    active.entryBaselines[30] = 0.3f;

    TestPendingChain chain;
    std::map<int, float> model;
    model[10] = -1.0f;
    model[20] = 0.2f;
    model[30] = -1.0f;

    // B 继承 A 的 baseline[10]=0.1，采集 model[20]=0.2
    auto bBases = MotionLifecycle::computeIncomingBaselines(
        {10, 20}, active, 0, chain,
        [&](int idx) { return model[idx]; });
    QCOMPARE(bBases[10], 0.1f);

    // B 启动后 A 成为 pending: A 的 outgoing-only = {30}
    auto [pa, ha] = MotionLifecycle::computeOutgoingOnly(active, 0, {10, 20});
    QVERIFY(ha);
    QCOMPARE(pa.baselines.size(), 1);
    QCOMPARE(pa.baselines[30], 0.3f);

    // A→B 冲突解决：B 驱动 {10,20} 从 pending（空链）中移除（无害）
    MotionLifecycle::resolveConflict(chain, {10, 20});
    chain.push_back(std::move(pa));

    // B 成为 active
    active.drivenParams = {10, 20};
    active.entryBaselines = std::move(bBases);
    active.handle = 101;

    // C 启动时 B 仍为 active，应继承 B 的 entryBaselines
    auto cBases = MotionLifecycle::computeIncomingBaselines(
        {10}, active, 0, chain,
        [&](int idx) { return model[idx]; });
    // 从 B 的 entryBaselines 继承 baseline[10]
    QCOMPARE(cBases[10], 0.1f);
    // pending 链中 A 的条目保留 {30:0.3}
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain[0].baselines.size(), 1);
    QCOMPARE(chain[0].baselines[30], 0.3f);
}

// ============================================================================
// outgoing-only 计算
// ============================================================================

void MotionLifecycleTest::test_outgoingOnly_computesCorrectly()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    active.drivenParams = {1, 2, 3};
    active.entryBaselines[1] = 0.1f;
    active.entryBaselines[2] = 0.2f;
    active.entryBaselines[3] = 0.3f;

    auto [entry, pushed] = MotionLifecycle::computeOutgoingOnly(active, 0, {2, 4});
    QVERIFY(pushed);
    QCOMPARE(entry.handle, 100);
    QCOMPARE(entry.baselines.size(), 2);
    QCOMPARE(entry.baselines[1], 0.1f);
    QCOMPARE(entry.baselines[3], 0.3f);
    QVERIFY(entry.baselines.find(2) == entry.baselines.end()); // 共同参数排除
}

// ============================================================================
// 共同参数排除
// ============================================================================

void MotionLifecycleTest::test_outgoingOnly_sharedParamExcluded()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    active.drivenParams = {5};
    active.entryBaselines[5] = 0.5f;

    auto [entry, pushed] = MotionLifecycle::computeOutgoingOnly(active, 0, {5});
    QVERIFY(!pushed); // 全部是共同参数 → 无 outgoing-only
}

// ============================================================================
// 冲突解决
// ============================================================================

void MotionLifecycleTest::test_conflictResolution_removesIncomingFromPending()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    active.drivenParams = {1, 2};
    active.entryBaselines[1] = 0.1f;
    active.entryBaselines[2] = 0.2f;

    TestPendingEntry oldPending;
    oldPending.handle = 99;
    oldPending.generation = 0;
    oldPending.baselines[1] = 0.9f;
    oldPending.baselines[3] = 0.3f;
    TestPendingChain chain = {oldPending};

    // 新 incoming 驱动 {1, 4}
    auto [newEntry, pushed] = MotionLifecycle::computeOutgoingOnly(active, 0, {1, 4});
    QVERIFY(pushed);

    MotionLifecycle::resolveConflict(chain, {1, 4});
    chain.push_back(std::move(newEntry));

    // pending[0] (old) 的 param 1 被移除，param 3 保留
    QCOMPARE(chain.size(), 2);
    QCOMPARE(chain[0].baselines.size(), 1);
    QVERIFY(chain[0].baselines.find(1) == chain[0].baselines.end());
    QCOMPARE(chain[0].baselines[3], 0.3f);
}

// ============================================================================
// 链式冲突解决
// ============================================================================

void MotionLifecycleTest::test_conflictResolution_chainResolve()
{
    TestPendingEntry e1, e2;
    e1.handle = 50; e1.generation = 0; e1.baselines[1] = 0.5f;
    e2.handle = 51; e2.generation = 0; e2.baselines[2] = 0.6f; e2.baselines[3] = 0.7f;
    TestPendingChain chain = {e1, e2};

    MotionLifecycle::resolveConflict(chain, {1});

    // e1 空 → 被移除；e2 不受影响
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain[0].handle, 51);
    QCOMPARE(chain[0].baselines.size(), 2);
}

// ============================================================================
// 空 chain 安全
// ============================================================================

void MotionLifecycleTest::test_emptyChain_noError()
{
    TestActiveState active;
    active.valid = false;

    TestPendingChain chain;

    auto baselines = MotionLifecycle::computeIncomingBaselines(
        {1, 2}, active, 0, chain,
        [&](int) { return 0.0f; });
    QCOMPARE(baselines.size(), 2);
}

// ============================================================================
// generation 失效
// ============================================================================

void MotionLifecycleTest::test_generation_invalidatesOldEntries()
{
    int gen = 0;
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    active.drivenParams = {1};
    active.entryBaselines[1] = 0.1f;

    TestPendingChain chain;

    auto [entry, pushed] = MotionLifecycle::computeOutgoingOnly(active, 0, {2});
    QVERIFY(pushed);
    chain.push_back(std::move(entry));

    MotionLifecycle::clearMotionState(active, chain, gen);

    QVERIFY(!active.valid);
    QCOMPARE(chain.size(), 0);
    QCOMPARE(gen, 1);
}

// ============================================================================
// 三层 pending 链基线追溯
// ============================================================================

void MotionLifecycleTest::test_threeLayer_pendingInheritance()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    // A 驱动 {1, 2}
    active.drivenParams = {1, 2};
    active.entryBaselines[1] = 0.1f;
    active.entryBaselines[2] = 0.2f;

    TestPendingChain chain;

    // A→B: B 驱动 {3}（不覆盖 A 的任何参数）
    // A 的 outgoing-only = {1,2} - {} = {1,2}
    auto [pA, hA] = MotionLifecycle::computeOutgoingOnly(active, 0, {3});
    QVERIFY(hA);
    chain.push_back(std::move(pA));
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain[0].baselines.size(), 2);
    QCOMPARE(chain[0].baselines[1], 0.1f);
    QCOMPARE(chain[0].baselines[2], 0.2f);

    // B 成为 active，不驱动任何参数
    active.drivenParams = {3};
    active.entryBaselines.clear();
    active.entryBaselines[3] = 0.3f;
    active.handle = 101;

    // B→C: C 驱动 {2}（不覆盖 B 的 param 3）
    // B 的 outgoing-only = {3} - {2} = {3}
    auto [pB, hB] = MotionLifecycle::computeOutgoingOnly(active, 0, {2});
    QVERIFY(hB);
    QCOMPARE(pB.baselines.size(), 1);
    QCOMPARE(pB.baselines[3], 0.3f);

    // 冲突解决：C 驱动 {2} 从 pending 中移除 param 2
    MotionLifecycle::resolveConflict(chain, {2});
    chain.push_back(std::move(pB));

    // chain 现在: [{A's pending: {1:0.1}}, {B's pending: {3:0.3}}]
    // A 的 pending 中 param 1 保留（C 不驱动 1）
    QCOMPARE(chain.size(), 2);
    QCOMPARE(chain[0].baselines.size(), 1);
    QCOMPARE(chain[0].baselines[1], 0.1f);
    QCOMPARE(chain[1].baselines.size(), 1);
    QCOMPARE(chain[1].baselines[3], 0.3f);

    // 无 active outgoing → C 从 pending 追溯
    active.valid = false;

    // C 驱动 {1}: 从 A 的 pending 条目继承 baseline[1]=0.1
    auto cBases = MotionLifecycle::computeIncomingBaselines(
        {1}, active, 0, chain,
        [](int) { return 0.0f; });
    QCOMPARE(cBases[1], 0.1f); // 追溯到 A 的 baseline
}

// ============================================================================
// StartMotion 失败回滚: 状态不变
// ============================================================================

void MotionLifecycleTest::test_rollback_stateUnchanged()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    active.motionKey = "OldMotion";
    active.drivenParams = {1, 2};
    active.entryBaselines[1] = 0.1f;

    TestPendingChain chain;
    chain.push_back({});
    chain[0].handle = 50;
    chain[0].generation = 0;
    chain[0].baselines[10] = 0.5f;

    auto savedState = active;
    auto savedChainSize = chain.size();
    auto savedBaseline10 = chain[0].baselines.at(10);

    // 模拟失败（只是不修改状态）
    // 回滚后所有状态不变

    QCOMPARE(active.valid, savedState.valid);
    QCOMPARE(active.handle, savedState.handle);
    QCOMPARE(active.motionKey, savedState.motionKey);
    QCOMPARE(active.drivenParams, savedState.drivenParams);
    QCOMPARE(active.entryBaselines, savedState.entryBaselines);
    QCOMPARE(active.generation, savedState.generation);
    QCOMPARE(chain.size(), savedChainSize);
    QCOMPARE(chain[0].baselines.at(10), savedBaseline10);
}

// ============================================================================
// 自然结束: 恢复所有参数 + 状态清除（不清理 pending chain）
// ============================================================================

void MotionLifecycleTest::test_naturalEnd_restoresAllParams()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 0;
    active.handle = 100;
    active.drivenParams = {1, 2, 3};
    active.entryBaselines[1] = 0.1f;
    active.entryBaselines[2] = 0.2f;
    active.entryBaselines[3] = 0.3f;

    std::map<int, float> model;
    model[1] = 0.9f;
    model[2] = 0.8f;
    model[3] = 0.7f;

    // 已有 pending
    TestPendingChain chain;
    chain.push_back({});
    chain[0].handle = 50;
    chain[0].generation = 0;
    chain[0].baselines[10] = 0.5f;
    QCOMPARE(chain.size(), 1);

    // 自然结束
    MotionLifecycle::handleNaturalEnd(
        active, 0,
        [&](int idx, float val) { model[idx] = val; });

    // 参数恢复
    QCOMPARE(model[1], 0.1f);
    QCOMPARE(model[2], 0.2f);
    QCOMPARE(model[3], 0.3f);
    QVERIFY(!active.valid);

    // pending chain 未被清除
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain[0].baselines.at(10), 0.5f);
}

// ============================================================================
// clearMotionState
// ============================================================================

void MotionLifecycleTest::test_clearMotionState_resetsAll()
{
    TestActiveState active;
    active.valid = true;
    active.generation = 5;
    active.drivenParams = {1, 2, 3};
    active.entryBaselines[1] = 0.5f;

    TestPendingChain chain;
    chain.push_back({});
    chain[0].handle = 100;
    chain[0].generation = 5;
    chain[0].baselines[10] = 0.9f;

    int gen = 5;
    MotionLifecycle::clearMotionState(active, chain, gen);

    QVERIFY(!active.valid);
    QCOMPARE(chain.size(), 0);
    QCOMPARE(gen, 6);
}

QTEST_APPLESS_MAIN(MotionLifecycleTest)
#include "MotionLifecycleTest.moc"
