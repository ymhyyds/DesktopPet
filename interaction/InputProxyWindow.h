#pragma once

#include <QWidget>
#include <QPoint>
#include <QRegion>

class PetWindow;
class Live2DView;
class QHideEvent;

/**
 * @brief InputProxyWindow 是桌宠的透明输入代理窗口。
 *
 * 职责：
 *  - 使用多矩形 QRegion mask（基于每个可见 Drawable 的 AABB + 4px padding）
 *  - mask 内 → 接收鼠标事件，转交 InteractionManager + 命中检测
 *  - mask 外 → 系统级穿透到桌面
 *  - 不绘制任何内容，不抢焦点，不激活
 *  - geometry 与 PetWindow 一致，必要时裁剪到显示器可用区域
 *
 * PetWindow 不再使用 setMask/SetWindowRgn，仅负责 Live2D 渲染。
 * 两者分离后，mask 不再裁剪 OpenGL 输出，消除闪烁和渲染裁剪。
 */
class InputProxyWindow : public QWidget
{
    Q_OBJECT

public:
    explicit InputProxyWindow(PetWindow *petWindow, Live2DView *live2DView,
                              QWidget *parent = nullptr);
    ~InputProxyWindow() override;

    /// 同步 geometry 到 PetWindow（必要时裁剪到显示器可用区域）
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

private:
    /// 统一命中检测：仅当点在 preciseRegion 内 AND hitTestAt 通过 AND alphaTestAt 通过才返回 true
    bool isInteractivePoint(const QPointF &proxyLocalPos);

    void handleDragMove(QMouseEvent *event);
    void cleanupDrag();

private:
    PetWindow   *m_petWindow   = nullptr;
    Live2DView  *m_live2DView  = nullptr;

    // ── 拖拽 ──
    bool   m_dragging = false;
    QPoint m_dragStartOffset;

    // ── 命中检测（头部/身体） ──
    bool   m_pressInHeadArea = false;
    bool   m_pressInBodyArea = false;
    QPoint m_pressStartPos;

    // ── 精确 mask（逐 Drawable AABB + 4px padding） ──
    QRegion m_preciseRegion;
    bool    m_preciseRegionValid = false;

    // ── 几何裁剪偏移（PetWindow origin → InputProxyWindow origin） ──
    int m_geoOffsetX = 0;
    int m_geoOffsetY = 0;

    static constexpr int kMaskPaddingPx = 4;
    static constexpr int kDragThreshold  = 6;
};
