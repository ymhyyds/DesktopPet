#pragma once

#include <QWidget>
#include <QPoint>
#include <QRegion>

class PetWindow;
class Live2DView;
class QHideEvent;
class QMoveEvent;
class QResizeEvent;

/**
 * @brief InputProxyWindow 是桌宠的透明输入代理窗口。
 *
 * 职责：
 *  - 使用多矩形 QRegion mask（基于每个可见 Drawable 的 AABB + 4px padding）
 *  - mask 内 → 接收鼠标事件，转交 InteractionManager + 命中检测
 *  - mask 外 → 系统级穿透到桌面
 *  - 不绘制任何内容，不抢焦点，不激活
 *  - geometry 裁剪到显示器边界（不变区域外 OS 可能自动裁剪导致坐标不一致）
 *
 * ── 关键设计：PetWindow-local 永久交互区域 ≠ Proxy-local 临时裁剪 mask ──
 *
 * m_petInteractiveRegion：
 *   - 始终以完整 PetWindow 为坐标系；
 *   - 不因窗口移出屏幕而裁剪；
 *   - 不因 m_geoOffset 变化而平移；
 *   - 由 Drawable 信息重新生成，不得把裁剪后的结果写回。
 *
 * m_proxyMaskRegion：
 *   - 每次根据当前 proxy geometry、offset 和 m_petInteractiveRegion 临时派生；
 *   - 只用于 setMask()，不作为后续 hit-test 的 canonical 数据。
 */
class InputProxyWindow : public QWidget
{
    Q_OBJECT

public:
    explicit InputProxyWindow(PetWindow *petWindow, Live2DView *live2DView,
                              QWidget *parent = nullptr);
    ~InputProxyWindow() override;

    /// 同步 geometry 到 PetWindow（裁剪到显示器边界，计算 geoOffset）
    void syncGeometry();

    /// 更新输入 mask（基于每个可见 Drawable 的 AABB + 4px padding）
    void updateInputMask();

signals:
    void dragFinished();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    /// 统一命中检测：仅当点在 m_petInteractiveRegion 内 AND hitTestAt 通过 AND alphaTestAt 通过才返回 true
    bool isInteractivePoint(const QPointF &proxyLocalPos);

    void handleDragMove(QMouseEvent *event);
    void cleanupDrag();

    /// 查找与 PetWindow geometry 相交面积最大的屏幕
    QScreen *bestScreenForPetWindow(const QRect &petGeo) const;

    /// 在 syncGeometry 重入后确保最终刷新一次 mask
    void ensureMaskRefresh();

private:
    PetWindow   *m_petWindow   = nullptr;
    Live2DView  *m_live2DView  = nullptr;

    // ── 拖拽 ──
    bool   m_dragging = false;
    /// 相对于 PetWindow 左上角的拖动锚点（一次拖动期间不变）
    QPoint m_dragAnchorInPet;
    /// 按下时的 geo offset（拖动期间保持不变，用于验证和调试）
    QPoint m_dragGeoOffsetAtPress;

    // ── 命中检测（头部/身体） ──
    bool   m_pressInHeadArea = false;
    bool   m_pressInBodyArea = false;
    QPoint m_pressStartPos;

    // ── PetWindow-local 永久交互区域（不被裁剪/平移） ──
    //   所有 hit-test 基于此区域，坐标 = PetWindow 本地坐标
    QRegion m_petInteractiveRegion;
    bool    m_petInteractiveRegionValid = false;

    // ── Proxy-local 临时裁剪 mask ──
    //   仅用于 setMask()，每次从 m_petInteractiveRegion 派生
    QRegion m_proxyMaskRegion;
    bool    m_proxyMaskRegionValid = false;

    // ── 双击抑制（防止第二次 release 再触发一次普通点击） ──
    bool    m_suppressNextReleaseSingle = false;

    // ── 几何裁剪偏移（PetWindow origin → InputProxyWindow origin） ──
    int m_geoOffsetX = 0;
    int m_geoOffsetY = 0;

    // ── syncGeometry 重入保护 ──
    bool m_syncingGeometry = false;

    // ── syncGeometry 重入时标记需要最终刷新 mask ──
    bool m_maskRefreshPending = false;

    // ── 拖动期间 PetWindow 移动后标记延迟同步 ──
    bool m_geometrySyncPending = false;

    static constexpr int kMaskPaddingPx = 4;
    static constexpr int kDragThreshold  = 6;

};

#include "DragAnchorHelpers.h"
