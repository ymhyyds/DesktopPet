#pragma once

#include <QPoint>
#include <QSize>

// ── 纯函数：拖动坐标计算（无 Qt Widgets 依赖，可直接单元测试） ──

/// 从 proxy 本地坐标 + geoOffset 计算 PetWindow 本地锚点
inline QPoint ComputeDragAnchor(QPoint proxyLocalPos, int geoOffsetX, int geoOffsetY)
{
    return proxyLocalPos + QPoint(geoOffsetX, geoOffsetY);
}

/// 从全局鼠标位置和 PetWindow 本地锚点计算 PetWindow 新左上角
inline QPoint ComputeNewPosition(QPoint globalMousePos, QPoint dragAnchorInPet)
{
    return globalMousePos - dragAnchorInPet;
}

/// 判断 InputProxyWindow 是否需要重新生成 mask
/// @return true = offset 或 size 发生变化，需要刷新 OS-level mask
inline bool NeedsMaskRefresh(int oldOffsetX, int oldOffsetY,
                              int newOffsetX, int newOffsetY,
                              QSize oldSize, QSize newSize)
{
    return oldOffsetX != newOffsetX
        || oldOffsetY != newOffsetY
        || oldSize != newSize;
}
