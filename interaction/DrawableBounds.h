#pragma once

#include <cfloat>
#include <cmath>

// ── 纯函数：Drawable 包围盒计算（无 Cubism 依赖，可直接单元测试） ──
// 与 DragAnchorHelpers.h 同模式，提供生产代码和测试共用的 bounds 辅助逻辑

namespace DrawableBounds {

/// 累加一个顶点到运行中的 AABB，拒绝 NaN/Inf。
/// 用于逐 Drawable 的顶点遍历，多个 AccumulateVertex 调用 + 一次 FinalizeDrawable 完成一块。
inline void AccumulateVertex(float vx, float vy,
                             float &mnX, float &mnY,
                             float &mxX, float &mxY,
                             int &validCount)
{
    if (!std::isfinite(vx) || !std::isfinite(vy))
        return;
    ++validCount;
    if (vx < mnX) mnX = vx;
    if (vy < mnY) mnY = vy;
    if (vx > mxX) mxX = vx;
    if (vy > mxY) mxY = vy;
}

/// 终化一个 Drawable 的 AABB：
///   - 有效顶点数 < 3 → 拒绝
///   - 退化包围盒（宽或高 < 0.001）→ 拒绝
///   - 将残留的 Inf 钳制为安全值
/// @return false = 此 Drawable 应被跳过（不在最终 bounds 中）
/// @note 失败时输出重置为 (0,0,1,1) 以防误用。
inline bool FinalizeDrawable(float &mnX, float &mnY,
                              float &mxX, float &mxY,
                              int validCount)
{
    if (validCount < 3) {
        mnX = mnY = 0.0f;
        mxX = mxY = 1.0f;
        return false;
    }
    if (!std::isfinite(mnX)) mnX = 0.0f;
    if (!std::isfinite(mnY)) mnY = 0.0f;
    if (!std::isfinite(mxX)) mxX = 1.0f;
    if (!std::isfinite(mxY)) mxY = 1.0f;
    const float w = mxX - mnX;
    const float h = mxY - mnY;
    if (w < 0.001f || h < 0.001f) {
        mnX = mnY = 0.0f;
        mxX = mxY = 1.0f;
        return false;
    }
    return true;
}

} // namespace DrawableBounds
