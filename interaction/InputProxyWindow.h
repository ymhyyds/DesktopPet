#pragma once

#include <QWidget>
#include <QPoint>
#include <QRegion>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QVector>

class PetWindow;
class Live2DView;
class QHideEvent;

/**
 * @brief InputProxyWindow 是桌宠的透明输入代理窗口。
 *
 * 职责：
 *  - 使用精确 point-in-triangle grid detection（基于每个可见 Drawable 的顶点/索引数据）
 *  - mask 内 → 接收鼠标事件，转交 InteractionManager + 命中检测
 *  - mask 外 → 系统级穿透到桌面
 *  - 不绘制任何内容，不抢焦点，不激活
 *  - geometry 与 PetWindow 完全一致（不按屏幕裁剪）
 *
 * PetWindow 不再使用 setMask/SetWindowRgn，仅负责 Live2D 渲染。
 * 两者分离后，mask 不再裁剪 OpenGL 输出，消除闪烁和渲染裁剪。
 *
 * 关键设计：
 *  - last-good-mask 事务发布：新 mask 成功才替换旧 mask
 *  - empty / suspicious mask 永远不会覆盖 last good
 *  - model switch 不清空 OS mask
 *  - 不做复杂状态机
 *  - cell size = 2 logical px
 *  - 无 AABB fallback
 *  - 无 alpha/PBO/HTTRANSPARENT/低层钩子
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

    /// 更新输入 mask（立即执行，基于精确 point-in-triangle grid detection）
    void updateInputMask();

    /// 请求延迟更新输入 mask（防抖，合并短时间内的多次请求）
    void requestInputMaskUpdate(const QString &reason, int delayMs);


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

    // ── Input Mask v2: last-good-mask 事务发布 ──

    /// 实际执行 input mask 更新（由定时器或直接调用触发）
    void performInputMaskUpdate(const QString &reason);

    /// 将候选 region 发布为 OS mask，更新 last good 缓存
    void publishInputRegion(const QRegion &region, const QString &reason);

    /// 确保 m_inputMaskTimer 存在
    bool ensureInputMaskTimer();

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

    // ── 当前 OS mask 缓存（用于 isInteractivePetPoint 命中检测） ──
    QRegion m_preciseRegion;
    bool    m_preciseRegionValid = false;

    // ── 双击抑制（防止第二次 release 再触发一次普通点击） ──
    bool    m_suppressNextReleaseSingle = false;

    // ── syncGeometry 重入保护 ──
    bool m_syncingGeometry = false;

    // ══════════════════════════════════════════════════════════════════
    // Input Mask v2 — last-good-mask 事务发布
    // ══════════════════════════════════════════════════════════════════

    /// 上次成功发布的 OS mask（发布新 mask 前不覆盖）
    QRegion m_lastGoodInputRegion;
    /// 是否有 published 过的 last good mask
    bool    m_hasLastGoodInputRegion = false;
    /// 上次成功发布时的 proxy 尺寸（用于信任检查）
    QSize   m_lastGoodProxySize;

    /// grid cell 边长（逻辑像素）
    int     m_inputMaskCellSize = 2;

    /// 是否有 pending 的延迟更新请求
    bool    m_inputMaskUpdatePending = false;
    /// 延迟调度定时器
    QTimer *m_inputMaskTimer = nullptr;

    static constexpr int kDragThreshold  = 6;

    // ── 诊断开关（已关闭）：裁剪时使用完整矩形 mask 代替 DeriveProxyMask ──
    //   现在 Proxy geometry 始终等于 PetWindow，不会发生裁剪。
    //   保留常量为 false 以便将来排查。
    static constexpr bool kDebugUseFullRectMaskWhenClipped = false;

};

#include "DragAnchorHelpers.h"
