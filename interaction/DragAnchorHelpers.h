#pragma once

#include <QPoint>
#include <QSize>
#include <QRect>
#include <QRegion>

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

// ── 纯函数：PetWindow-local ↔ proxy-local 区域转换 ──

/// 将点从 proxy-local 转换为 PetWindow-local
inline QPoint ProxyToPetPoint(QPoint proxyLocalPos, int geoOffsetX, int geoOffsetY)
{
    return proxyLocalPos + QPoint(geoOffsetX, geoOffsetY);
}

/// 从 PetWindow-local 永久交互区域派生 proxy-local mask
/// 1. 平移 (-geoOffsetX, -geoOffsetY)
/// 2. 相交到 proxy 边界 (0, 0 ~ proxySize)
inline QRegion DeriveProxyMask(const QRegion &petRegion, int geoOffsetX, int geoOffsetY,
                                QSize proxySize)
{
    return petRegion.translated(-geoOffsetX, -geoOffsetY)
                    .intersected(QRect(QPoint(0, 0), proxySize));
}

/// 判断 petRegion 平移后是否完全在 proxy 边界内（无需裁剪）
inline bool PetRegionFitsProxy(const QRegion &petRegion, int geoOffsetX, int geoOffsetY,
                                QSize proxySize)
{
    QRegion translated = petRegion.translated(-geoOffsetX, -geoOffsetY);
    return translated == translated.intersected(QRect(QPoint(0, 0), proxySize));
}
