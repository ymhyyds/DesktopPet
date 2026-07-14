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
template<typename HandleT>
struct PendingRestoreEntry {
    HandleT handle = HandleT{};
    std::map<ParamIndex, float> baselines; // paramIndex → 应恢复的值
    int generation = 0;
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
// ====================================================================

template<typename HandleT>
void handleNaturalEnd(
    ActiveMotionState<HandleT>& activeState,
    int currentGeneration,
    const std::function<void(ParamIndex, float)>& setParamValue)
{
    if (activeState.valid && activeState.generation == currentGeneration) {
        // 恢复 active motion 的所有 entry baseline 到模型
        for (const auto& [idx, val] : activeState.entryBaselines) {
            setParamValue(idx, val);
        }
        activeState.valid = false;
    }
    // 不清理 pending chain：已有 pending 由 applyPendingRestores 正常管理
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
