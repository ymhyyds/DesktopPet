#include "InputProxyWindow.h"

#include "../pet/PetWindow.h"
#include "../pet/Live2DView.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QHideEvent>
#include <QPaintEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QVector>
#include <QScopedValueRollback>

// ============================================================================
// 构造 / 析构
// ============================================================================

InputProxyWindow::InputProxyWindow(PetWindow *petWindow, Live2DView *live2DView,
                                   QWidget *parent)
    : QWidget(parent)
    , m_petWindow(petWindow)
    , m_live2DView(live2DView)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAutoFillBackground(false);

    // 不显示在任务栏
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);

    // 初始 geometry 与 PetWindow 一致
    syncGeometry();

    // ★ 安装 qApp 级事件过滤器，在 InteractionManager（widget 级 eventFilter）之前拦截
    qApp->installEventFilter(this);

    // ★ 跟踪 PetWindow 的 moveEvent — 确保 InputProxyWindow 始终跟随其 geometry
    if (m_petWindow)
        m_petWindow->installEventFilter(this);
}

InputProxyWindow::~InputProxyWindow()
{
    cleanupDrag();
}

// ============================================================================
// 事件过滤器 — 在 InteractionManager（widget 级 filter）之前拦截
// 确保非模型区域的鼠标事件不会到达 InteractionManager
// ============================================================================

bool InputProxyWindow::eventFilter(QObject *watched, QEvent *event)
{
    // ── (A) PetWindow move/resize 跟踪 — 自动同步 geometry ──
    if (watched == m_petWindow) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            if (!m_dragging)
                syncGeometry();
            else
                m_geometrySyncPending = true;   // 拖拽期间仅标记，释放后统一同步
        }
        return false;   // 不吞事件，PetWindow 正常处理
    }

    // ── (B) 仅处理本窗口或其子窗口的事件 ──
    //   watched == this              → 本窗口自身
    //   watched->window() == this    → 本窗口的子控件（需先转为 QWidget）
    // 其他对象直接放行 return false，禁止影响：
    //   托盘菜单、ChatWindow、SettingsWindow、SpeechBubble、其他 Qt 窗口
    if (watched != this) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (!w || w->window() != this)
            return false;
    }

    // ── (C) 本窗口事件 ──

    if (event->type() == QEvent::WindowDeactivate) {
        cleanupDrag();
    }

    // ── (D) 命中测试守卫 — 统一通过 isInteractivePoint 拦截非交互点击 ──
    //  只有 m_petInteractiveRegion.contains + hitTestAt + alphaTestAt 全部通过才放行。
    //  return true 只阻止 Qt 事件循环内继续派发（不涉及跨进程穿透），
    //  跨进程穿透已由 OS 级 setMask(proxyMask) 处理。
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::ContextMenu:
    {
        QPointF localPos;
        if (event->type() == QEvent::ContextMenu) {
            auto *ce = static_cast<QContextMenuEvent *>(event);
            localPos = QPointF(ce->pos());
        } else {
            auto *me = static_cast<QMouseEvent *>(event);
            localPos = me->position();
        }

        if (!isInteractivePoint(localPos)) {
            // 未命中交互点 → 清理 pending 状态并阻止事件派发
            cleanupDrag();
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

// ============================================================================
// Geometry 同步 — 裁剪到显示器边界，计算 geoOffset
// ============================================================================

void InputProxyWindow::syncGeometry()
{
    // ── 重入保护：第二次进入时标记 pending，不执行裁剪逻辑 ──
    if (m_syncingGeometry) {
        m_maskRefreshPending = true;
        return;
    }
    QScopedValueRollback<bool> guard(m_syncingGeometry, true);
    m_maskRefreshPending = false;

    if (!m_petWindow) return;

    const QRect petGeo = m_petWindow->geometry();

    // ── 记录旧状态（用于统一判断是否需要刷新 mask） ──
    const int oldOffsetX = m_geoOffsetX;
    const int oldOffsetY = m_geoOffsetY;
    const QSize oldSize = size();

    // ── 查找最佳屏幕 ──
    QScreen *screen = bestScreenForPetWindow(petGeo);

    QRect proxyGeo;
    if (!screen) {
        // ── 窗口完全不在任何屏幕上 → fallback 到主屏幕 ──
        QScreen *primary = QGuiApplication::primaryScreen();
        if (primary) {
            const QRect screenGeo = primary->geometry();
            int cx = qBound(screenGeo.left(), petGeo.left(),
                            screenGeo.right() - qMin(petGeo.width(), screenGeo.width()));
            int cy = qBound(screenGeo.top(), petGeo.top(),
                            screenGeo.bottom() - qMin(petGeo.height(), screenGeo.height()));
            proxyGeo = QRect(QPoint(cx, cy), petGeo.size());
            m_geoOffsetX = proxyGeo.left() - petGeo.left();
            m_geoOffsetY = proxyGeo.top()  - petGeo.top();
        } else {
            // 无可用屏幕 → 跟随 PetWindow
            proxyGeo = petGeo;
            m_geoOffsetX = 0;
            m_geoOffsetY = 0;
        }
    } else {
        // ── 裁剪 proxy 到该屏幕边界 ──
        const QRect screenGeo = screen->geometry();

        int newX  = qMax(screenGeo.left(),   petGeo.left());
        int newY  = qMax(screenGeo.top(),    petGeo.top());
        int newR  = qMin(screenGeo.right(),  petGeo.right());
        int newB  = qMin(screenGeo.bottom(), petGeo.bottom());

        m_geoOffsetX = newX - petGeo.left();
        m_geoOffsetY = newY - petGeo.top();

        int newW = qMax(1, newR - newX);
        int newH = qMax(1, newB - newY);

        proxyGeo = QRect(QPoint(newX, newY), QSize(newW, newH));
    }

    // ── 应用 proxy geometry ──
    setGeometry(proxyGeo);

    // ── ★ 重新读取实际 geometry，以实际值为准 ──
    //    Windows 可能调整请求的 geometry（如顶部越界时 clamp 到屏幕顶部），
    //    必须用实际值计算 offset，否则 proxy-local ↔ pet-local 转换会偏移。
    const QRect actualGeo = geometry();
    m_geoOffsetX = actualGeo.x() - petGeo.x();
    m_geoOffsetY = actualGeo.y() - petGeo.y();

#ifndef QT_NO_DEBUG
    if (proxyGeo != actualGeo) {
        qDebug() << "[InputProxy] syncGeometry: actual differs from requested"
                 << "requested=" << proxyGeo
                 << "actual=" << actualGeo
                 << "offset=" << m_geoOffsetX << m_geoOffsetY;
    }
#endif

    // ── 确保可见 ──
    if (!isVisible()) {
        show();
        raise();
    }

    // ── 判断是否需要刷新 mask ──
    if (NeedsMaskRefresh(oldOffsetX, oldOffsetY,
                          m_geoOffsetX, m_geoOffsetY,
                          oldSize, actualGeo.size()))
    {
        updateInputMask();
    } else if (m_maskRefreshPending) {
        // 重入时标记了 pending，即使在最外层也没触发 refresh → 手动触发
        ensureMaskRefresh();
    }
}

void InputProxyWindow::ensureMaskRefresh()
{
    if (!m_petInteractiveRegionValid) {
        updateInputMask();
    } else {
        // 至少用当前 petRegion 重新派生一次 proxy mask
        QRegion proxyMask = m_petInteractiveRegion.translated(-m_geoOffsetX, -m_geoOffsetY)
                                              .intersected(QRect(0, 0, width(), height()));
        if (proxyMask.isEmpty())
            return;
        if (m_proxyMaskRegionValid && m_proxyMaskRegion == proxyMask)
            return;
        setMask(proxyMask);
        m_proxyMaskRegion = proxyMask;
        m_proxyMaskRegionValid = true;
    }
}

QScreen *InputProxyWindow::bestScreenForPetWindow(const QRect &petGeo) const
{
    // 1. 优先使用 PetWindow 当前所属 screen
    QScreen *bestScreen = m_petWindow ? m_petWindow->screen() : nullptr;
    int maxArea = 0;
    if (bestScreen) {
        QRect intersect = petGeo.intersected(bestScreen->geometry());
        if (!intersect.isEmpty())
            maxArea = intersect.width() * intersect.height();
        else
            bestScreen = nullptr;
    }

    // 2. 遍历所有屏幕，选择相交面积最大的
    const auto screens = QGuiApplication::screens();
    for (QScreen *s : screens) {
        QRect intersect = petGeo.intersected(s->geometry());
        int area = intersect.width() * intersect.height();
        if (area > maxArea) {
            maxArea = area;
            bestScreen = s;
        }
    }

    return bestScreen;
}

// ============================================================================
// Mask 更新 — 使用逐 Drawable AABB + 4px padding 构建多矩形 QRegion
//
// 流程：
//   1. 从 Live2DView 获取所有可见 Drawable 的紧贴包围盒（PetWindow 坐标）
//   2. 构建 m_petInteractiveRegion（PetWindow-local，不裁剪，不作废）
//   3. 派生 m_proxyMaskRegion（proxy-local，平移 + intersection）
//   4. setMask(m_proxyMaskRegion)
// ============================================================================

void InputProxyWindow::updateInputMask()
{
    if (!m_live2DView) {
        // 无模型 → 保留上一次有效的 any region
        return;
    }

    // ── Step 1: 获取所有可见 Drawable 的紧贴包围盒（PetWindow 本地坐标） ──
    QVector<QRect> drawableRects;
    m_live2DView->getVisibleDrawableRects(drawableRects);

    if (drawableRects.isEmpty()) {
        // 无可见 drawable → 保留上一次有效的 m_petInteractiveRegion
        return;
    }

    // ── Step 2: 构建 PetWindow-local 永久交互区域 ──
    //    使用 Live2DView/PetWindow 的完整尺寸，不使用被裁剪后的 proxy size
    //    不裁剪、不平移，始终保持 PetWindow 坐标系
    QRegion petRegion;
    for (const QRect &rect : drawableRects) {
        QRect r = rect.adjusted(-kMaskPaddingPx, -kMaskPaddingPx,
                                 kMaskPaddingPx, kMaskPaddingPx);
        petRegion |= QRegion(r);
    }

    if (petRegion.isEmpty()) {
        return;
    }

    // 保存永久区域（PetWindow-local，不受 proxy 裁剪影响）
    m_petInteractiveRegion = petRegion;
    m_petInteractiveRegionValid = true;

    // ── Step 3: 派生 proxy-local 临时 mask ──
    //    m_petInteractiveRegion 是 PetWindow-local，
    //    平移 (-m_geoOffsetX, -m_geoOffsetY) 转为 proxy-local，
    //    再 intersected 到 proxy 边界
    QRegion proxyMask = m_petInteractiveRegion.translated(-m_geoOffsetX, -m_geoOffsetY);
    proxyMask = proxyMask.intersected(QRect(0, 0, width(), height()));

    if (proxyMask.isEmpty()) {
        // 裁剪后为空 → 保留上一次有效的 setMask（不清除 mask）
        // 但 m_proxyMaskRegionValid 不变
        return;
    }

    // ── Step 4: 全窗口 → 清除 mask ──
    if (proxyMask == QRegion(QRect(0, 0, width(), height()))) {
        if (m_proxyMaskRegionValid) {
            clearMask();
            m_proxyMaskRegionValid = false;
        }
        return;
    }

    // ── Step 5: 防重复 ──
    if (m_proxyMaskRegionValid && m_proxyMaskRegion == proxyMask) {
        return;
    }

    // ── Step 6: 应用 mask ──
    setMask(proxyMask);
    m_proxyMaskRegion = proxyMask;
    m_proxyMaskRegionValid = true;

#ifndef QT_NO_DEBUG
    qDebug() << "[InputProxy] updateInputMask"
             << "drawableCount=" << drawableRects.size()
             << "proxySize=" << size()
             << "geoOffset=" << m_geoOffsetX << m_geoOffsetY
             << "firstDrawableRect=" << drawableRects.first()
             << "petRegionBB=" << m_petInteractiveRegion.boundingRect()
             << "proxyMaskBB=" << m_proxyMaskRegion.boundingRect();
#endif
}

// ============================================================================
// moveEvent / resizeEvent — 检测 Windows 对 Proxy geometry 的隐式调整
// ============================================================================

void InputProxyWindow::moveEvent(QMoveEvent *event)
{
    // 只在非拖拽、非 syncGeometry 重入时纠正 offset
    // setGeometry 内部的 moveEvent 由 m_syncingGeometry 守卫跳过，
    // 拖拽期间的 moveEvent 由 m_dragging 守卫跳过，
    // 防止递归修改 offset 或触发 syncGeometry。
    if (m_petWindow && !m_dragging && !m_syncingGeometry) {
        const QPoint actualTopLeft = geometry().topLeft();
        const QPoint petTopLeft = m_petWindow->geometry().topLeft();
        const int newOffX = actualTopLeft.x() - petTopLeft.x();
        const int newOffY = actualTopLeft.y() - petTopLeft.y();
        if (newOffX != m_geoOffsetX || newOffY != m_geoOffsetY) {
            m_geoOffsetX = newOffX;
            m_geoOffsetY = newOffY;
            // offset 变化 → mask 平移变化 → 刷新
            if (m_petInteractiveRegionValid)
                ensureMaskRefresh();
        }
    }
    QWidget::moveEvent(event);
}

void InputProxyWindow::resizeEvent(QResizeEvent *event)
{
    // 尺寸变化 → mask 裁剪边界变化 → 刷新
    if (!m_dragging && m_petInteractiveRegionValid)
        ensureMaskRefresh();
    QWidget::resizeEvent(event);
}

// ============================================================================
// 鼠标事件
// ============================================================================

void InputProxyWindow::mousePressEvent(QMouseEvent *event)
{
    // 只处理左键
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    // 统一命中检测 — eventFilter 已拦截非交互点，此 checks 为兜底
    if (!isInteractivePoint(event->position())) {
        event->ignore();
        return;
    }

    // Hit-area 细分（头部/身体）
    // proxy-local → PetWindow-local（加上 geoOffset）
    const float proxyPx = static_cast<float>(event->position().x());
    const float proxyPy = static_cast<float>(event->position().y());
    const float petPx = proxyPx + static_cast<float>(m_geoOffsetX);
    const float petPy = proxyPy + static_cast<float>(m_geoOffsetY);

    bool isHead = false;
    m_live2DView->hitTestAt(petPx, petPy, isHead);
    bool alphaOk = m_live2DView->alphaTestAt(petPx, petPy);
    m_pressInHeadArea = alphaOk && isHead;
    m_pressInBodyArea = alphaOk && !isHead;

    // 记录拖拽起始偏移（PetWindow 本地坐标，一次拖动期间不变）
    {
        m_dragAnchorInPet = QPoint(qRound(petPx), qRound(petPy));
        m_dragGeoOffsetAtPress = QPoint(m_geoOffsetX, m_geoOffsetY);
#ifndef QT_NO_DEBUG
        qDebug() << "[InputProxy] mousePress"
                 << "petGeo=" << (m_petWindow ? m_petWindow->geometry() : QRect())
                 << "proxyGeo=" << geometry()
                 << "proxyLocal=" << event->position().toPoint()
                 << "petLocal=" << m_dragAnchorInPet
                 << "geoOffset=" << m_geoOffsetX << m_geoOffsetY;
#endif
    }
    m_pressStartPos = event->pos();
}

void InputProxyWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_dragging) {
        // 尚未进入拖拽 — 检查是否超过拖拽阈值
        if ((m_pressInHeadArea || m_pressInBodyArea)) {
            const int dist = (event->pos() - m_pressStartPos).manhattanLength();
            if (dist > kDragThreshold) {
                // 超过阈值 → 启动拖拽
                m_dragging = true;
                m_dragGeoOffsetAtPress = QPoint(m_geoOffsetX, m_geoOffsetY);
                grabMouse();

                // 已移动超过阈值，不再作为点击处理
                m_pressInHeadArea = false;
                m_pressInBodyArea = false;

                handleDragMove(event);
            }
        }
        // 未超阈值：不处理 move，点击资格保留
    } else {
        handleDragMove(event);
    }
}

void InputProxyWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        // ── 双击后第二次 release：跳过，只清除标志 ──
        if (m_suppressNextReleaseSingle) {
            m_suppressNextReleaseSingle = false;
            m_pressInHeadArea = false;
            m_pressInBodyArea = false;
            return;
        }

        if (m_dragging) {
            // 拖拽结束
            cleanupDrag();
            emit dragFinished();
            // ── 释放后重新同步 Proxy geometry、offset 和 mask ──
            //    此时 PetWindow 已移动到最终位置，Proxy 冻结在按下时的位置，
            //    需要重新裁剪、对齐并刷新 mask。
            syncGeometry();
        } else if (m_pressInHeadArea) {
            // 头部点击 → 摸摸头
            m_pressInHeadArea = false;
            if (m_petWindow) m_petWindow->triggerPet();
        } else if (m_pressInBodyArea) {
            // 身体点击 → 随机动作
            m_pressInBodyArea = false;
            if (m_live2DView) m_live2DView->playTapMotion();
        } else {
        }
    }

    // 确保 press 状态清零
    m_pressInHeadArea = false;
    m_pressInBodyArea = false;
}

void InputProxyWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    // 使用本次双击的坐标重新做命中检测，不沿用第一次 press 的旧状态
    if (!isInteractivePoint(event->position())) {
        // 未命中（透明区域双击）→ 不触发任何桌宠交互，不设置抑制
        cleanupDrag();
        return;
    }

    // ── 命中实体区域 ──
    // 设置抑制标志：双击第二次 release 不应再触发一次普通点击交互
    m_suppressNextReleaseSingle = true;

    // 允许 InteractionManager 处理双击语义（取消 pending single click + 打开聊天）
    QWidget::mouseDoubleClickEvent(event);
}

void InputProxyWindow::cleanupDrag()
{
    m_dragging = false;
    m_geometrySyncPending = false;
    m_pressInHeadArea = false;
    m_pressInBodyArea = false;
    m_suppressNextReleaseSingle = false;
    if (QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
}

bool InputProxyWindow::isInteractivePoint(const QPointF &proxyLocalPos)
{
    const QPoint pt = proxyLocalPos.toPoint();

    // ── proxy-local → PetWindow-local ──
    const QPoint petPt(pt.x() + m_geoOffsetX, pt.y() + m_geoOffsetY);

    // (1) PetWindow-local 永久交互区域
    if (!m_petInteractiveRegionValid || !m_petInteractiveRegion.contains(petPt))
        return false;

    // (2) 模型内容：必须命中交互区域（HitArea / tight bounds）
    const float petPx = static_cast<float>(petPt.x());
    const float petPy = static_cast<float>(petPt.y());
    bool isHead = false;
    if (!m_live2DView || !m_live2DView->hitTestAt(petPx, petPy, isHead))
        return false;

    // (3) 像素透明度：排除 HitArea AABB 内的透明间隙
    if (!m_live2DView->alphaTestAt(petPx, petPy))
        return false;

    return true;
}

void InputProxyWindow::handleDragMove(QMouseEvent *event)
{
    const QPoint newPetTopLeft = ComputeNewPosition(
        event->globalPosition().toPoint(), m_dragAnchorInPet);

#ifndef QT_NO_DEBUG
    qDebug() << "[InputProxy] dragMove"
             << "globalMouse=" << event->globalPosition().toPoint()
             << "anchorInPet=" << m_dragAnchorInPet
             << "newTopLeft=" << newPetTopLeft
             << "oldTopLeft=" << (m_petWindow ? m_petWindow->pos() : pos());
#endif

    if (m_petWindow) {
        // ── 只移动 PetWindow，不改变 Proxy geometry ──
        //    grabMouse() 确保 Proxy 在 PetWindow 移开后继续接收鼠标事件。
        //    Proxy geometry 和 offset 在整个拖动期间冻结，
        //    释放后由 syncGeometry() 重新对齐。
        m_petWindow->move(newPetTopLeft);
    } else {
        move(newPetTopLeft);
    }
}

// ============================================================================
// 右键菜单
// ============================================================================

void InputProxyWindow::contextMenuEvent(QContextMenuEvent *event)
{
    // 使用本次 ContextMenu 的坐标重新做命中检测
    // eventFilter 已 return true 拦截非交互右键，此 checks 为兜底
    if (!isInteractivePoint(QPointF(event->pos()))) {
        event->ignore();
        return;
    }

    // 转发到 PetWindow 显示右键菜单（好感度/喂食/问候等）
    if (m_petWindow) {
        QCoreApplication::sendEvent(m_petWindow, event);
    }
}

// ============================================================================
// hideEvent — 清理拖拽和 grab 状态
// ============================================================================

void InputProxyWindow::hideEvent(QHideEvent *event)
{
    cleanupDrag();
    QWidget::hideEvent(event);
}

// ============================================================================
// 绘制：仅在 m_proxyMaskRegion 内绘制 alpha=1 输入表面
// ============================================================================

void InputProxyWindow::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);

    // 整窗填充 alpha=0（确保透明）
    painter.fillRect(rect(), QColor(0, 0, 0, 0));

    // 在 m_proxyMaskRegion 内绘制 alpha=1 输入表面（OS 级鼠标穿透用）
    if (m_proxyMaskRegionValid) {
        painter.setClipRegion(m_proxyMaskRegion);
        painter.fillRect(m_proxyMaskRegion.boundingRect(), QColor(0, 0, 0, 1));
    }
}
