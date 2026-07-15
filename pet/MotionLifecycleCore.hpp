#pragma once

/**
 * @file MotionLifecycleCore.hpp
 *
 * Motion 参数生命周期核心算法 — 纯 C++17，无 Cubism SDK / Qt / OpenGL 依赖。
 *
 * 本文件定义 MotionLifecycle 命名空间，包含：
 * - 数据结构（ActiveMotionState / PendingRestoreEntry / ParameterCache）
 * - 纯函数（selectIdleIndexFromBounded）
 * - 模板算法（computeIncomingBaselines / computeOutgoingOnly /
 *          resolveConflict / handleNaturalEnd / clearMotionState）
 *
 * ModelController（生产）和 MotionLifecycleTest（单元测试）都使用同一份实现，
 * 仅 HandleT 不同（生产=void*，测试=int）。
 */

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace MotionLifecycle {

/// 参数索引类型（Cubism SDK GetParameterIndex 返回 int）
using ParamIndex = int;

/// Motion 参数索引缓存（key = "Group_Index"，非插入式查询）
using ParameterCache = std::map<std::string, std::set<ParamIndex>>;

// ====================================================================
// 数据结构（Handle 类型模板化，生产用 void*，测试用 int）
// ====================================================================

/// 当前活跃运动参数状态
template<typename HandleT>
struct ActiveMotionState {
    std::string motionKey;
    HandleT handle = HandleT{};
    std::set<ParamIndex> drivenParams;
    std::map<ParamIndex, float> entryBaselines; // paramIndex → 启动时值（含继承）
    int generation = 0;
    bool valid = false;
};

/// 待恢复参数状态（按 outgoing handle 管理）
///
/// 平滑恢复状态机：
///   initialized=false → [首次 apply]  初始化 startValues ← 当前模型值
///   initialized=true  → [平滑中]      每帧 smoothstep(startValues → baselines)
///   elapsed≥duration  → [锁定中]      写精确 baseline（handle 未结束时防止 FadeOut 残值）
///   elapsed≥duration + handleFinished  → 写最终 baseline 后删除条目
template<typename HandleT>
struct PendingRestoreEntry {
    HandleT handle = HandleT{};
    std::map<ParamIndex, float> baselines;   // paramIndex → 恢复目标值
    int generation = 0;

    // ── 平滑恢复状态（由 applyPendingRestores 管理） ──
    std::map<ParamIndex, float> startValues; // paramIndex → 插值起点（首次 apply 时采集）
    float elapsed = 0.0f;                    // 已累积时间（秒）
    bool initialized = false;                // startValues 是否已初始化
    float duration = 0.10f;                  // 平滑持续时间（秒）
};

/// 待恢复参数链（可同时存在多个 FadeOut 的 Motion）
template<typename HandleT>
using PendingRestoreChain = std::vector<PendingRestoreEntry<HandleT>>;

// ====================================================================
// selectIdleIndex — 纯函数（不依赖随机生成器）
// ====================================================================

/// 纯函数：从 bounded(count-1) 值偏移，避免选中 lastIndex。
/// count ≤ 1 时始终返回 0。boundedValue 由调用者提供（生产用 QRandomGenerator，测试直接传）。
inline int selectIdleIndexFromBounded(int count, int lastIndex, int boundedValue)
{
    if (count <= 1) return 0;
    int pick = boundedValue;
    if (pick >= lastIndex) ++pick;
    return pick;
}

// ====================================================================
// computeIncomingBaselines — 三层继承链计算
// 优先级：active outgoing 继承 > pending 继承 > 当前模型值
// ====================================================================

template<typename HandleT>
std::map<ParamIndex, float> computeIncomingBaselines(
    const std::set<ParamIndex>& incomingParams,
    const ActiveMotionState<HandleT>& activeState,
    int currentGeneration,
    const PendingRestoreChain<HandleT>& pendingChain,
    const std::function<float(ParamIndex)>& getCurrentValue)
{
    std::map<ParamIndex, float> baselines;

    for (ParamIndex idx : incomingParams) {
        float val;

        // 第 1 层：从 active outgoing 继承
        if (activeState.valid && activeState.generation == currentGeneration) {
            auto it = activeState.entryBaselines.find(idx);
            if (it != activeState.entryBaselines.end()) {
                val = it->second;
            } else {
                // 不在 outgoing 中 → 采集当前值
                val = getCurrentValue(idx);
            }
        } else {
            // 第 2 层：从 pending chain 继承
            bool foundInPending = false;
            for (const auto& pending : pendingChain) {
                if (pending.generation != currentGeneration) continue;
                auto pit = pending.baselines.find(idx);
                if (pit != pending.baselines.end()) {
                    val = pit->second;
                    foundInPending = true;
                    break;
                }
            }

            // 第 3 层：采集当前模型值
            if (!foundInPending) {
                val = getCurrentValue(idx);
            }
        }

        baselines[idx] = val;
    }

    return baselines;
}

// ====================================================================
// computeOutgoingOnly — 计算 outgoing-only 参数集并组装恢复信息
// ====================================================================

/// 返回 {PendingRestoreEntry, hasPending}。hasPending=false 表示无 outgoing-only 参数。
template<typename HandleT>
std::pair<PendingRestoreEntry<HandleT>, bool> computeOutgoingOnly(
    const ActiveMotionState<HandleT>& activeState,
    int currentGeneration,
    const std::set<ParamIndex>& incomingParams)
{
    // 无有效 outgoing → 无待恢复参数
    if (!activeState.valid || activeState.generation != currentGeneration
        || activeState.handle == HandleT{}) {
        return {{}, false};
    }

    // 找出 outgoing 驱动但 incoming 不驱动的参数
    std::map<ParamIndex, float> outgoingOnly;
    for (ParamIndex idx : activeState.drivenParams) {
        if (incomingParams.find(idx) == incomingParams.end()) {
            auto bit = activeState.entryBaselines.find(idx);
            if (bit != activeState.entryBaselines.end())
                outgoingOnly[idx] = bit->second;
        }
    }

    if (outgoingOnly.empty())
        return {{}, false};

    PendingRestoreEntry<HandleT> pr;
    pr.handle = activeState.handle;
    pr.baselines = std::move(outgoingOnly);
    pr.generation = currentGeneration;

    return {std::move(pr), true};
}

// ====================================================================
// resolveConflict — 从已有 pending 中移除 incoming 参数
// ====================================================================

template<typename HandleT>
void resolveConflict(PendingRestoreChain<HandleT>& pendingChain,
                     const std::set<ParamIndex>& incomingParams)
{
    // 每个 pending 中的对应参数被新 motion 覆盖 → 从恢复目标中移除
    for (auto& pending : pendingChain) {
        for (ParamIndex idx : incomingParams) {
            pending.baselines.erase(idx);
        }
    }

    // 空 pending → 清理
    pendingChain.erase(
        std::remove_if(pendingChain.begin(), pendingChain.end(),
            [](const PendingRestoreEntry<HandleT>& e) { return e.baselines.empty(); }),
        pendingChain.end());
}

// ====================================================================
// handleNaturalEnd — 自然结束处理（不清理 pending chain）
//
// 不再直接瞬时 SetParameterValue。改为将 active 的 entryBaselines
// 推入 pendingChain，由 applyPendingRestores 统一平滑恢复。
// ====================================================================

template<typename HandleT>
void handleNaturalEnd(
    ActiveMotionState<HandleT>& activeState,
    int currentGeneration,
    PendingRestoreChain<HandleT>& pendingChain)
{
    if (activeState.valid && activeState.generation == currentGeneration) {
        // 创建 pending entry（handle = 空 = 无 motion 要等，平滑完直接删除）
        PendingRestoreEntry<HandleT> pr;
        pr.handle = HandleT{};
        pr.baselines = activeState.entryBaselines;
        pr.generation = currentGeneration;
        pendingChain.push_back(std::move(pr));

        activeState.valid = false;
    }
    // 不清理 pending chain 中其他条目
}

// ====================================================================
// applyPendingRestores — 平滑或完整恢复 pending 链中的参数
//
// 状态机：
//   ┌─initialized=false─→ 初始化 startValues ← readParam
//   ├─elapsed<duration ─→ smoothstep(startValues → baselines)，积累 elapsed
//   ├─elapsed≥duration  ─→ 写精确 baseline
//   │   └─handle 未结束 ─→ 保持 entry（锁定 baseline 防 FadeOut 残值）
//   │   └─handle 已结束 ─→ 写最终 baseline 后 erase
// ====================================================================

template<typename HandleT, typename FnWrite, typename FnRead, typename FnIsFinished>
void applyPendingRestores(
    PendingRestoreChain<HandleT>& chain,
    int currentGeneration,
    float deltaTime,
    FnWrite writeParam,
    FnRead readParam,
    FnIsFinished isHandleFinished)
{
    // ── 0. deltaTime 非负保护 ──
    deltaTime = std::max(deltaTime, 0.0f);

    auto it = chain.begin();
    while (it != chain.end()) {
        // ── 1. Generation 不一致 → 废弃 ──
        if (it->generation != currentGeneration) {
            it = chain.erase(it);
            continue;
        }

        // ── 2. 首次应用：采集当前参数值作为插值起点 ──
        //    注意：调用者保证在 UpdateMotion 之后执行此函数
        if (!it->initialized) {
            for (const auto& [idx, baseline] : it->baselines) {
                it->startValues[idx] = readParam(idx);
            }
            it->initialized = true;
        }

        // ── 3. 确定 handle 状态 ──
        const bool isNullHandle = (it->handle == HandleT{});
        const bool handleFinished = isNullHandle || isHandleFinished(it->handle);

        // ── 4. 推进时间并计算 smoothstep ──
        it->elapsed += deltaTime;
        const float safeDuration = std::max(it->duration, 0.001f);
        const float progress = std::min(it->elapsed / safeDuration, 1.0f);
        const float eased = progress * progress * (3.0f - 2.0f * progress); // smoothstep
        const bool smoothingComplete = (progress >= 1.0f);

        // ── 5. 三路分支 ──
        if (smoothingComplete && handleFinished) {
            // ⑤A. 平滑已完成 + handle 已结束 → 写入精确 baseline 后删除
            for (const auto& [idx, baseline] : it->baselines) {
                writeParam(idx, baseline);
            }
            it = chain.erase(it);
        } else if (smoothingComplete) {
            // ⑤B. 平滑已完成但 handle 仍在（FadeOut 未结束）
            //     继续每帧写入精确 baseline，防止旧 FadeOut 再写回残值
            for (const auto& [idx, baseline] : it->baselines) {
                writeParam(idx, baseline);
            }
            ++it;
        } else {
            // ⑤C. 平滑中：每帧写入插值
            for (const auto& [idx, baseline] : it->baselines) {
                const float start = it->startValues[idx];
                writeParam(idx, start + (baseline - start) * eased);
            }
            ++it;
        }
    }
}

// ====================================================================
// clearMotionState — 清除全部 motion 生命周期状态
// ====================================================================

template<typename HandleT>
void clearMotionState(
    ActiveMotionState<HandleT>& activeState,
    PendingRestoreChain<HandleT>& pendingChain,
    int& generation)
{
    activeState.valid = false;
    pendingChain.clear();
    ++generation;
}

} // namespace MotionLifecycle
