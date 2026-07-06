#pragma once

#include <QPoint>
#include <QSize>
#include <QRegion>
#include <QRect>

// ============================================================================
// 纯函数：坐标系转换 & mask 推导（无 Qt Widgets 依赖，可直接单元测试）
// ============================================================================
//
// 全局约定：
//   - PetWindow-local：原点 = PetWindow 客户区左上角
//   - Proxy-local：    原点 = InputProxyWindow 客户区左上角
//   - Global：          屏幕绝对坐标（像素）
//
// 鼠标命中语义坐标链（不再经过 proxy-local）：
//   global mouse position
//   → PetWindow::mapFromGlobal()
//   → PetWindow-local point
//   → isInteractivePetPoint() / hitTestAt() / alphaTestAt()
//
// Proxy mask 仍由 PetWindow-local region 偏移得到：
//   canonicalPetRegion → translate(petTopLeft - proxyTopLeft) → proxyMask
// ============================================================================

/// 全局鼠标坐标 → PetWindow 本地坐标
inline QPoint GlobalToPetPoint(const QPoint &globalMousePos, const QPoint &petTopLeft)
{
    return QPoint(globalMousePos.x() - petTopLeft.x(),
                  globalMousePos.y() - petTopLeft.y());
}

/// 从全局鼠标位置和 PetWindow 本地锚点计算 PetWindow 新左上角
inline QPoint ComputeNewPosition(QPoint globalMousePos, QPoint dragAnchorInPet)
{
    return globalMousePos - dragAnchorInPet;
}

/// 从全局鼠标位置和 PetWindow 本地锚点计算 PetWindow 新左上角（别名）
inline QPoint ComputePetTopLeftFromDrag(const QPoint &globalMousePos,
                                         const QPoint &anchorInPet)
{
    return globalMousePos - anchorInPet;
}

// ============================================================================
// 已弃用函数保留（为现有 test_drag_anchor 提供向后兼容）
// 新代码不应使用：鼠标语义坐标改用 GlobalToPetPoint 直接从 global→PetWindow-local
// ============================================================================

/// 从 proxy 本地坐标 + geoOffset 计算 PetWindow 本地锚点（已弃用）
inline QPoint ComputeDragAnchor(QPoint proxyLocalPos, int geoOffsetX, int geoOffsetY)
{
    return proxyLocalPos + QPoint(geoOffsetX, geoOffsetY);
}

/// 判断是否需要重新生成 mask（已弃用，由 DeriveProxyMask 替代）
inline bool NeedsMaskRefresh(int oldOffsetX, int oldOffsetY,
                              int newOffsetX, int newOffsetY,
                              QSize oldSize, QSize newSize)
{
    return oldOffsetX != newOffsetX
        || oldOffsetY != newOffsetY
        || oldSize != newSize;
}

/// 从规范 PetWindow-local region 推导 proxy-local mask
///
/// @param canonicalPetRegion  PetWindow-local 的完整交互区域（不被修改）
/// @param petGeometry         PetWindow 当前 geometry
/// @param actualProxyGeometry InputProxyWindow 当前 geometry（setGeometry 后读取）
/// @return proxy-local 的 QRegion，可直接用于 setMask()
inline QRegion DeriveProxyMask(const QRegion &canonicalPetRegion,
                                const QRect &petGeometry,
                                const QRect &actualProxyGeometry)
{
    const QPoint translation = petGeometry.topLeft() - actualProxyGeometry.topLeft();
    return canonicalPetRegion
        .translated(translation)
        .intersected(QRect(QPoint(0, 0), actualProxyGeometry.size()));
}
