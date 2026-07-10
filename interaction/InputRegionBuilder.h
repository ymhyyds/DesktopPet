#pragma once

#include <QRegion>
#include <QRect>
#include <QSize>
#include <QPoint>
#include <QVector>
#include <QPair>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <type_traits>

// ============================================================================
// InputRegionBuilder — 纯几何辅助函数集合
//
// 不依赖 QWidget，不依赖 Live2D SDK 对象，可直接单元测试。
// 职责：点-三角形包含测试、grid cell 行合并、region 可信度检查。
// ============================================================================

namespace InputRegionBuilder {

// ── 点-三角形包含测试 ──────────────────────────────────────────────────────

/// 测试点 (px, py) 是否在三角形 (ax,ay)-(bx,by)-(cx,cy) 内部。
/// 使用重心坐标系方法（同侧法）：点在三边同侧 → 内部。
/// 边界上的点也算内部（非严格）。
inline bool pointInTriangle(float px, float py,
                            float ax, float ay,
                            float bx, float by,
                            float cx, float cy)
{
    // 同侧法（cross-product sign test）
    const auto sign = [](float p1x, float p1y, float p2x, float p2y, float p3x, float p3y) -> float {
        return (p1x - p3x) * (p2y - p3y) - (p2x - p3x) * (p1y - p3y);
    };

    float d1 = sign(px, py, ax, ay, bx, by);
    float d2 = sign(px, py, bx, by, cx, cy);
    float d3 = sign(px, py, cx, cy, ax, ay);

    bool hasNeg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    bool hasPos = (d1 > 0) || (d2 > 0) || (d3 > 0);

    return !(hasNeg && hasPos);
}

// ── Grid cell 合并（row-span 优化）─────────────────────────────────────────

/// 将 grid cell hit 集合（(col,row) 对）合并为 row-span QRegion。
///
/// 同一行中连续的被合并为更宽的 QRect，大幅降低 QRegion 内部矩形数。
///
/// @param cells   命中的 grid cell 坐标对（允许重复，内部去重）
/// @param cellSize 每个 cell 的逻辑像素边长
/// @param clampW   region 宽度上限
/// @param clampH   region 高度上限
/// @return 合并后的 QRegion（空集合返回空 QRegion）
inline QRegion mergeCellRects(QVector<QPair<int,int>> cells,
                              int cellSize,
                              int clampW, int clampH)
{
    if (cells.isEmpty())
        return QRegion();

    // 去重 + 排序
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());

    QRegion region;
    const int rowCount = cells.size();
    int idx = 0;

    while (idx < rowCount) {
        const int row = cells[idx].second;
        int spanStart = cells[idx].first;
        int spanEnd = spanStart + 1;
        ++idx;

        // 同一行中合并连续 col
        while (idx < rowCount && cells[idx].second == row) {
            if (cells[idx].first == spanEnd) {
                spanEnd = cells[idx].first + 1;
                ++idx;
            } else {
                // 有 gap → 结束当前 span
                break;
            }
        }

        // 将当前 span 转为 QRect
        int x = spanStart * cellSize;
        int w = (spanEnd - spanStart) * cellSize;
        int y = row * cellSize;
        int h = cellSize;

        // 钳制到窗口范围
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x < clampW && y < clampH) {
            w = std::min(w, clampW - x);
            h = std::min(h, clampH - y);
            if (w > 0 && h > 0)
                region |= QRegion(x, y, w, h);
        }
    }

    return region;
}

// ── Region 可信度检查 ─────────────────────────────────────────────────────

/// 检查候选 input region 是否可以发布为 OS mask。
///
/// @return true = 可以安全发布
///
/// 规则：
///   - empty → false
///   - boundingRect 任一维 < 4px → false
///   - 面积 < 16 sq px → false（太小的区域基本不可能覆盖可见模型）
///   - proxy 尺寸无效 → false
///   - region 超出 proxy 范围 → false
inline bool isTrustedInputRegion(const QRegion &region, const QSize &proxySize)
{
    if (region.isEmpty())
        return false;

    if (proxySize.width() <= 0 || proxySize.height() <= 0)
        return false;

    const QRect bb = region.boundingRect();

    // 边界外 → 不信任
    const QRect proxyRect(QPoint(0, 0), proxySize);
    if (!proxyRect.contains(bb))
        return false;

    // 极小尺寸
    if (bb.width() < 4 && bb.height() < 4)
        return false;
    if (bb.width() < 4 || bb.height() < 4)
        return false;

    // 面积接近零
    if (bb.width() * bb.height() < 16)
        return false;

    return true;
}

/// 检查 region 是否 "suspicious"（可疑但 non-empty）。
/// 可疑 region 不应覆盖 last good mask。
/// @return true = 可疑，不应作为 OS mask 发布
inline bool isSuspiciousInputRegion(const QRegion &region, const QSize &proxySize)
{
    // 先过 trust 检查：trusted → 不可能是 suspicious
    if (isTrustedInputRegion(region, proxySize))
        return false;

    // empty → 本身就是 empty，不属于 suspicious（empty 已单独处理）
    if (region.isEmpty())
        return false;

    // 非 empty 但未通过 trust 检查 → suspicious
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// 🔬 诊断 Oracle：五点采样 vs 真值三角形-矩形相交（仅测试用途，不接入生产）
// ═══════════════════════════════════════════════════════════════════════════════

/// 将点投影到轴 (ax, ay) 上
inline float projectOntoAxis(float px, float py, float ax, float ay)
{
    return px * ax + py * ay;
}

/// 投影 AABB 到轴，返回 [min, max]
/// @param cx,cy AABB 中心; hx,hy AABB 半长宽
inline std::pair<float,float> projectAABB(float cx, float cy,
                                           float hx, float hy,
                                           float ax, float ay)
{
    const float c = projectOntoAxis(cx, cy, ax, ay);
    const float r = std::abs(hx * ax) + std::abs(hy * ay);
    return { c - r, c + r };
}

/// 投影三角形到轴，返回 [min, max]
inline std::pair<float,float> projectTriangle(float t0x, float t0y,
                                               float t1x, float t1y,
                                               float t2x, float t2y,
                                               float ax, float ay)
{
    const float d0 = projectOntoAxis(t0x, t0y, ax, ay);
    const float d1 = projectOntoAxis(t1x, t1y, ax, ay);
    const float d2 = projectOntoAxis(t2x, t2y, ax, ay);
    return { std::min({d0, d1, d2}), std::max({d0, d1, d2}) };
}

/// 两个区间是否重叠
inline bool intervalsOverlap(float aMin, float aMax, float bMin, float bMax)
{
    return aMax >= bMin && bMax >= aMin;
}

/// 🔬 真值诊断：三角形与轴对齐矩形是否相交（Separating Axis Theorem）
///
/// 这是三角形-矩形相交的精确判定 oracle。
/// 可用于验证 pointInTriangle-based 五点采样是否有 false negative。
///
/// 非生产用途，不与 grid/mask 逻辑关联。
/// @return true = 三角形与矩形有重叠（含边界相切）
inline bool triangleIntersectsRect(
    float t0x, float t0y, float t1x, float t1y, float t2x, float t2y,
    float rX, float rY, float rW, float rH)
{
    // 矩形中心 + 半长宽
    const float rcx = rX + rW * 0.5f;
    const float rcy = rY + rH * 0.5f;
    const float rhx = rW * 0.5f;
    const float rhy = rH * 0.5f;

    // ── 测试轴 1: AABB X 轴 ──
    const float tMinX = std::min({t0x, t1x, t2x});
    const float tMaxX = std::max({t0x, t1x, t2x});
    if (tMaxX < rX || tMinX > rX + rW)
        return false;

    // ── 测试轴 2: AABB Y 轴 ──
    const float tMinY = std::min({t0y, t1y, t2y});
    const float tMaxY = std::max({t0y, t1y, t2y});
    if (tMaxY < rY || tMinY > rY + rH)
        return false;

    // ── 测试轴 3-5: 三角形三条边的法线 ──
    // 边 AB → 法线
    {
        const float ex = t1y - t0y;
        const float ey = t0x - t1x;  // clockwise perpendicular
        auto tProj = projectTriangle(t0x, t0y, t1x, t1y, t2x, t2y, ex, ey);
        auto rProj = projectAABB(rcx, rcy, rhx, rhy, ex, ey);
        if (!intervalsOverlap(tProj.first, tProj.second, rProj.first, rProj.second))
            return false;
    }

    // 边 BC → 法线
    {
        const float ex = t2y - t1y;
        const float ey = t1x - t2x;
        auto tProj = projectTriangle(t0x, t0y, t1x, t1y, t2x, t2y, ex, ey);
        auto rProj = projectAABB(rcx, rcy, rhx, rhy, ex, ey);
        if (!intervalsOverlap(tProj.first, tProj.second, rProj.first, rProj.second))
            return false;
    }

    // 边 CA → 法线
    {
        const float ex = t0y - t2y;
        const float ey = t2x - t0x;
        auto tProj = projectTriangle(t0x, t0y, t1x, t1y, t2x, t2y, ex, ey);
        auto rProj = projectAABB(rcx, rcy, rhx, rhy, ex, ey);
        if (!intervalsOverlap(tProj.first, tProj.second, rProj.first, rProj.second))
            return false;
    }

    // 全部 5 轴投影均未分离 → 相交
    return true;
}

/// 🔬 五点采样辅助：测试 cellSize 网格中给定 cell 的五点采样能否命中三角形
/// @return true = 五点中至少一点命中
inline bool fivePointSampleHit(
    int col, int row, int cellSize,
    float t0x, float t0y, float t1x, float t1y, float t2x, float t2y)
{
    const float cx = static_cast<float>(col * cellSize + cellSize / 2);
    const float cy = static_cast<float>(row * cellSize + cellSize / 2);

    if (pointInTriangle(cx, cy, t0x, t0y, t1x, t1y, t2x, t2y))
        return true;

    const float x0 = static_cast<float>(col * cellSize);
    const float y0 = static_cast<float>(row * cellSize);
    const float x1 = static_cast<float>((col + 1) * cellSize);
    const float y1 = static_cast<float>((row + 1) * cellSize);

    return pointInTriangle(x0, y0, t0x, t0y, t1x, t1y, t2x, t2y) ||
           pointInTriangle(x1, y0, t0x, t0y, t1x, t1y, t2x, t2y) ||
           pointInTriangle(x0, y1, t0x, t0y, t1x, t1y, t2x, t2y) ||
           pointInTriangle(x1, y1, t0x, t0y, t1x, t1y, t2x, t2y);
}

} // namespace InputRegionBuilder
