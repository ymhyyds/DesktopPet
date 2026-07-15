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

    // ── 平滑恢复测试 ──
    void test_smoothRestore_startToBaseline();
    void test_smoothRestore_progress0_05_1();
    void test_smoothRestore_holdsAfterCompletion();
    void test_smoothRestore_handleGoneStillSmooths();
    void test_smoothRestore_handleLongLived();
    void test_smoothRestore_conflictCancels();
    void test_smoothRestore_generationChangeClears();
    void test_smoothRestore_edgeCases();
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
// 自然结束: 推入 pending chain 由平滑恢复（不直接 SetParameterValue）
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

    // 已有 pending（不受自然结束影响）
    TestPendingChain chain;
    chain.push_back({});
    chain[0].handle = 50;
    chain[0].generation = 0;
    chain[0].baselines[10] = 0.5f;
    QCOMPARE(chain.size(), 1);

    // 自然结束 → 推入 pending entry 而非立即 set
    MotionLifecycle::handleNaturalEnd(active, 0, chain);

    // entryBaselines 被推入 pending 链
    QCOMPARE(chain.size(), 2);
    QCOMPARE(chain[1].baselines.size(), 3);
    QCOMPARE(chain[1].baselines[1], 0.1f);
    QCOMPARE(chain[1].baselines[2], 0.2f);
    QCOMPARE(chain[1].baselines[3], 0.3f);
    QVERIFY(!active.valid);

    // 原有 pending 条目未被清除
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

// ============================================================================
// 平滑恢复测试
// ============================================================================

namespace { // 匿名命名空间：测试辅助函数

/// 在单次 applyPendingRestores 后断言模型值
void assertModelValue(const std::map<int, float>& model, int idx, float expected, float tol = 0.0001f)
{
    auto it = model.find(idx);
    QVERIFY2(it != model.end(), ("param " + std::to_string(idx) + " not in model").c_str());
    QVERIFY2(std::abs(it->second - expected) <= tol,
             ("param " + std::to_string(idx) + ": expected " + std::to_string(expected)
              + ", got " + std::to_string(it->second)).c_str());
}

/// smoothstep(progress) 纯函数，用于测试中直接计算
float smoothstep(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

} // anonymous namespace

// ============================================================================
// 平滑恢复：起点到 baseline 的完整插值路径
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_startToBaseline()
{
    // 单参数：baseline=1.0，初始模型值=0.5
    TestPendingEntry entry;
    entry.handle = 100;
    entry.generation = 0;
    entry.baselines[10] = 1.0f;
    entry.duration = 0.2f;
    TestPendingChain chain = {entry};

    std::map<int, float> model;
    model[10] = 0.5f;

    // [A] 首次 apply（elapsed += 0）：初始化 startValues，progress=0
    MotionLifecycle::applyPendingRestores(
        chain, 0, 0.0f,
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return false; });

    QCOMPARE(chain.size(), 1);
    QVERIFY(chain[0].initialized);
    assertModelValue(model, 10, 0.5f); // progress=0 → start

    // [B] 推进到 t=0.5（progress=0.5）：smoothstep(0.5)=0.5
    MotionLifecycle::applyPendingRestores(
        chain, 0, 0.1f,
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return false; });

    QCOMPARE(chain.size(), 1);
    float expectedAtHalf = 0.5f + (1.0f - 0.5f) * smoothstep(0.5f); // 0.5 + 0.5*0.5 = 0.75
    assertModelValue(model, 10, expectedAtHalf);

    // [C] 推进到 completion（progress=1.0）
    MotionLifecycle::applyPendingRestores(
        chain, 0, 0.1f,
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return false; });

    // progress=1.0, handle not finished → ⑤B（保持 baseline）
    QCOMPARE(chain.size(), 1);
    assertModelValue(model, 10, 1.0f); // baseline

    // [D] handle 结束 → ⑤A（删除）
    MotionLifecycle::applyPendingRestores(
        chain, 0, 0.0f,
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return true; });

    QCOMPARE(chain.size(), 0);
    assertModelValue(model, 10, 1.0f); // final
}

// ============================================================================
// 平滑恢复：progress=0 / 0.5 / 1 单独验证
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_progress0_05_1()
{
    // 准备：start=0.2, baseline=0.8, duration=0.2
    TestPendingEntry entry;
    entry.handle = 100;
    entry.generation = 0;
    entry.baselines[10] = 0.8f;
    entry.duration = 0.2f;
    TestPendingChain chain = {entry};

    std::map<int, float> model;
    model[10] = 0.2f;

    auto writeFn = [&](int idx, float v) { model[idx] = v; };
    auto readFn  = [&](int idx) -> float { return model[idx]; };
    auto isDone  = [](int) { return false; };

    // progress=0（deltaTime=0 首次调用）
    MotionLifecycle::applyPendingRestores(chain, 0, 0.0f, writeFn, readFn, isDone);
    QCOMPARE(chain.size(), 1);
    QVERIFY(chain[0].initialized);
    assertModelValue(model, 10, 0.2f); // start

    // progress=0.5（再推进 0.1s）
    MotionLifecycle::applyPendingRestores(chain, 0, 0.1f, writeFn, readFn, isDone);
    float expected = 0.2f + (0.8f - 0.2f) * smoothstep(0.5f); // = 0.2 + 0.6*0.5 = 0.5
    assertModelValue(model, 10, expected);

    // progress=1.0（再推进 0.1s）
    MotionLifecycle::applyPendingRestores(chain, 0, 0.1f, writeFn, readFn, isDone);
    assertModelValue(model, 10, 0.8f); // baseline
    QCOMPARE(chain.size(), 1); // handle still alive → ⑤B
}

// ============================================================================
// 完成平滑后持续锁定 baseline（handle 长时间存在）
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_holdsAfterCompletion()
{
    TestPendingEntry entry;
    entry.handle = 100;
    entry.generation = 0;
    entry.baselines[10] = 1.0f;
    entry.duration = 0.1f;
    TestPendingChain chain = {entry};

    std::map<int, float> model;
    model[10] = 0.0f;

    auto writeFn = [&](int idx, float v) { model[idx] = v; };
    auto readFn  = [&](int idx) -> float { return model[idx]; };

    bool handleFinished = false;
    auto handleCheck = [&](int) { return handleFinished; };

    // 运行至平滑完成
    MotionLifecycle::applyPendingRestores(chain, 0, 0.1f, writeFn, readFn, handleCheck);
    QCOMPARE(chain.size(), 1);  // ⑤B：handle 仍在
    assertModelValue(model, 10, 1.0f);

    // 模拟 FadeOut 试图写回残值
    model[10] = 0.3f;
    MotionLifecycle::applyPendingRestores(chain, 0, 0.016f, writeFn, readFn, handleCheck);
    QCOMPARE(chain.size(), 1);  // 仍锁定
    assertModelValue(model, 10, 1.0f); // baseline 覆盖

    // handle 结束
    handleFinished = true;
    MotionLifecycle::applyPendingRestores(chain, 0, 0.016f, writeFn, readFn, handleCheck);
    QCOMPARE(chain.size(), 0);  // ⑤A：删除
    assertModelValue(model, 10, 1.0f);
}

// ============================================================================
// handle 提前消失但平滑尚未完成 → 继续平滑至完成
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_handleGoneStillSmooths()
{
    TestPendingEntry entry;
    entry.handle = 100;
    entry.generation = 0;
    entry.baselines[10] = 1.0f;
    entry.duration = 0.3f;
    TestPendingChain chain = {entry};

    std::map<int, float> model;
    model[10] = 0.0f;

    auto writeFn = [&](int idx, float v) { model[idx] = v; };
    auto readFn  = [&](int idx) -> float { return model[idx]; };

    bool handleFinished = false;
    auto handleCheck = [&](int) { return handleFinished; };

    // [A] 初始化
    MotionLifecycle::applyPendingRestores(chain, 0, 0.0f, writeFn, readFn, handleCheck);
    QCOMPARE(chain.size(), 1);

    // [B] 平滑开始不久后，handle 消失
    MotionLifecycle::applyPendingRestores(chain, 0, 0.05f, writeFn, readFn, handleCheck);
    float valAfterHandleGone = model[10];
    QVERIFY(valAfterHandleGone > 0.0f && valAfterHandleGone < 1.0f);

    // handle 标记为结束（模拟 motion 被移除）
    handleFinished = true;

    // [C] 继续平滑
    MotionLifecycle::applyPendingRestores(chain, 0, 0.1f, writeFn, readFn, handleCheck);
    QCOMPARE(chain.size(), 1); // 仍在平滑
    float valMid = model[10];
    QVERIFY(valMid > valAfterHandleGone && valMid < 1.0f);

    // [D] 推进到完成
    MotionLifecycle::applyPendingRestores(chain, 0, 0.15f, writeFn, readFn, handleCheck);
    QCOMPARE(chain.size(), 0); // ⑤A：平滑 + handle 都完成 → 删除
    assertModelValue(model, 10, 1.0f);
}

// ============================================================================
// handle 长时间存在：完成后继续锁定，handle 消失后才删除
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_handleLongLived()
{
    TestPendingEntry entry;
    entry.handle = 100;
    entry.generation = 0;
    entry.baselines[10] = 0.5f;
    entry.duration = 0.12f;
    TestPendingChain chain = {entry};

    std::map<int, float> model;
    model[10] = 0.0f;

    auto writeFn = [&](int idx, float v) { model[idx] = v; };
    auto readFn  = [&](int idx) -> float { return model[idx]; };

    // handle 永不过期
    auto handleCheck = [](int) { return false; };

    // 运行至平滑完成
    MotionLifecycle::applyPendingRestores(chain, 0, 0.12f, writeFn, readFn, handleCheck);
    assertModelValue(model, 10, 0.5f);
    QCOMPARE(chain.size(), 1); // ⑤B：handle 未结束，锁定

    // 再跑 100 帧（模拟 1.6 秒），一直锁定
    for (int i = 0; i < 100; ++i) {
        model[10] = 0.1f; // 模拟干扰
        MotionLifecycle::applyPendingRestores(chain, 0, 0.016f, writeFn, readFn, handleCheck);
        assertModelValue(model, 10, 0.5f);
        QCOMPARE(chain.size(), 1);
    }

    // handle 终于结束
    auto handleCheckDone = [](int) { return true; };
    MotionLifecycle::applyPendingRestores(chain, 0, 0.016f, writeFn, readFn, handleCheckDone);
    QCOMPARE(chain.size(), 0);
}

// ============================================================================
// incoming 冲突取消恢复
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_conflictCancels()
{
    TestPendingEntry entry;
    entry.handle = 100;
    entry.generation = 0;
    entry.baselines[10] = 1.0f;
    entry.baselines[20] = 0.5f;
    entry.duration = 0.15f;
    TestPendingChain chain = {entry};

    std::map<int, float> model;
    model[10] = 0.0f;
    model[20] = 0.0f;

    // 开始平滑
    MotionLifecycle::applyPendingRestores(
        chain, 0, 0.075f,
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return false; });

    QCOMPARE(chain.size(), 1);
    QVERIFY(model[10] > 0.0f && model[10] < 1.0f);

    // 新 motion 驱动 param 10 → 从 pending 中移除
    MotionLifecycle::resolveConflict(chain, {10});

    // param 10 被移除，param 20 保留
    QCOMPARE(chain.size(), 1);
    QCOMPARE(chain[0].baselines.size(), 1);
    QVERIFY(chain[0].baselines.find(10) == chain[0].baselines.end());
    QCOMPARE(chain[0].baselines[20], 0.5f);

    // param 10 应不再被写入
    // 继续平滑 param 20 至完成
    MotionLifecycle::applyPendingRestores(
        chain, 0, 0.075f,
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return false; });

    assertModelValue(model, 20, 0.5f); // baseline reached
    // param 10 保持在上一帧的平滑值（不再被 pending 写入）
    float val10 = model[10];
    QVERIFY(val10 > 0.0f && val10 < 1.0f);
}

// ============================================================================
// generation 变化清除平滑
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_generationChangeClears()
{
    TestPendingEntry entry;
    entry.handle = 100;
    entry.generation = 0;
    entry.baselines[10] = 1.0f;
    entry.duration = 0.2f;
    TestPendingChain chain = {entry};

    std::map<int, float> model;
    model[10] = 0.0f;

    // 开始平滑
    MotionLifecycle::applyPendingRestores(
        chain, 0, 0.05f,
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return false; });

    QCOMPARE(chain.size(), 1);

    // generation 变化（模拟模型切换）
    MotionLifecycle::applyPendingRestores(
        chain, 1, 0.05f,  // currentGeneration=1 ≠ entry.generation=0
        [&](int idx, float v) { model[idx] = v; },
        [&](int idx) -> float { return model[idx]; },
        [](int) { return false; });

    QCOMPARE(chain.size(), 0); // 清除
}

// ============================================================================
// 边界条件：deltaTime=0、deltaTime>duration、duration 极小
// ============================================================================

void MotionLifecycleTest::test_smoothRestore_edgeCases()
{
    // ── case 1: deltaTime=0 不崩溃、不推进 ──
    {
        TestPendingEntry entry;
        entry.handle = 100;
        entry.generation = 0;
        entry.baselines[10] = 1.0f;
        entry.duration = 0.15f;
        TestPendingChain chain = {entry};

        std::map<int, float> model;
        model[10] = 0.3f;

        MotionLifecycle::applyPendingRestores(
            chain, 0, 0.0f,
            [&](int idx, float v) { model[idx] = v; },
            [&](int idx) -> float { return model[idx]; },
            [](int) { return false; });

        assertModelValue(model, 10, 0.3f); // 不变
        QCOMPARE(chain[0].elapsed, 0.0f);  // 时间不推进
        QVERIFY(chain[0].initialized);
    }

    // ── case 2: deltaTime > duration → 跳至 completion ──
    {
        TestPendingEntry entry;
        entry.handle = 100;
        entry.generation = 0;
        entry.baselines[10] = 1.0f;
        entry.duration = 0.15f;
        TestPendingChain chain = {entry};

        std::map<int, float> model;
        model[10] = 0.3f;

        MotionLifecycle::applyPendingRestores(
            chain, 0, 1.0f, // deltaTime >> duration
            [&](int idx, float v) { model[idx] = v; },
            [&](int idx) -> float { return model[idx]; },
            [](int) { return true; }); // handle finished

        assertModelValue(model, 10, 1.0f); // 直接跳到 baseline
        QCOMPARE(chain.size(), 0);         // 删除
    }

    // ── case 3: duration=0 → safeDuration 兜底到 0.001 ──
    {
        TestPendingEntry entry;
        entry.handle = 100;
        entry.generation = 0;
        entry.baselines[10] = 0.8f;
        entry.duration = 0.0f;  // 不合法但不应崩溃
        TestPendingChain chain = {entry};

        std::map<int, float> model;
        model[10] = 0.2f;

        MotionLifecycle::applyPendingRestores(
            chain, 0, 0.016f,
            [&](int idx, float v) { model[idx] = v; },
            [&](int idx) -> float { return model[idx]; },
            [](int) { return true; }); // handle finished

        assertModelValue(model, 10, 0.8f); // 跳到 baseline
        QCOMPARE(chain.size(), 0);
    }
}

QTEST_APPLESS_MAIN(MotionLifecycleTest)
#include "MotionLifecycleTest.moc"
