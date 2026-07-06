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
 *  - geometry 与 PetWindow 完全一致（不按屏幕裁剪）
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

    /// 同步 geometry 到 PetWindow（始终跟随完整 PetWindow，不裁剪）
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
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    /// 统一命中检测（PetWindow-local 坐标）：
    /// preciseRegion 包含该点 AND hitTestAt 通过 AND alphaTestAt 通过才返回 true
    bool isInteractivePetPoint(const QPoint &petLocalPos) const;

    void handleDragMove(QMouseEvent *event);
    void cleanupDrag();

private:
    PetWindow   *m_petWindow   = nullptr;
    Live2DView  *m_live2DView  = nullptr;

    // ── 拖拽 ──
    bool   m_dragging = false;
    /// 按下时记录的 PetWindow-local 锚点（一次拖动期间不变）
    QPoint m_dragAnchorInPet;

    // ── 命中检测（头部/身体） ──
    bool   m_pressInHeadArea = false;
    bool   m_pressInBodyArea = false;
    /// 按下时的全局鼠标位置（用于拖动阈值判断）
    QPoint m_pressGlobalStartPos;

    // ── 精确 mask（逐 Drawable AABB + 4px padding，proxy-local） ──
    QRegion m_preciseRegion;
    bool    m_preciseRegionValid = false;

    // ── 双击抑制（防止第二次 release 再触发一次普通点击） ──
    bool    m_suppressNextReleaseSingle = false;

    // ── syncGeometry 重入保护 ──
    bool m_syncingGeometry = false;

    static constexpr int kMaskPaddingPx = 4;
    static constexpr int kDragThreshold  = 6;

    // ── 诊断开关（已关闭）：裁剪时使用完整矩形 mask 代替 DeriveProxyMask ──
    //   现在 Proxy geometry 始终等于 PetWindow，不会发生裁剪。
    //   保留常量为 false 以便将来排查。
    static constexpr bool kDebugUseFullRectMaskWhenClipped = false;

};

#include "DragAnchorHelpers.h"
