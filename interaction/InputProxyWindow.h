#pragma once

#include <QWidget>
#include <QPoint>
#include <QRegion>

class PetWindow;
class Live2DView;

/**
 * @brief InputProxyWindow 是桌宠的透明输入代理窗口。
 *
 * 职责：
 *  - 只有它使用 QRegion mask 做输入穿透
 *  - mask 内 → 接收鼠标事件，转交 InteractionManager + 命中检测
 *  - mask 外 → 系统级穿透到桌面
 *  - 不绘制任何内容，不抢焦点，不激活
 *  - geometry 与 PetWindow 完全同步
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

    /// 同步 geometry 到 PetWindow
    void syncGeometry();

    /// 更新输入 mask（基于模型 pixel rect + 固定 padding）
    void updateInputMask();

signals:
    void dragFinished();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void handleDragMove(QMouseEvent *event);

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

    // ── mask ──
    QRegion m_cachedMask;
    bool    m_hasCachedMask = false;

    static constexpr int kMaskPaddingPx = 30;
};
