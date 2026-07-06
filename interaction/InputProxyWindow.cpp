#include "InputProxyWindow.h"

#include "../pet/PetWindow.h"
#include "../pet/Live2DView.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QHideEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
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

    // 初始 mask
    updateInputMask();
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
            syncGeometry();
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
#ifndef QT_NO_DEBUG
        qDebug() << "[InputProxy::eventFilter] WindowDeactivate — cleanupDrag"
                 << "m_dragging=" << m_dragging
                 << "grabber=" << QWidget::mouseGrabber();
#endif
        cleanupDrag();
    }

    // ── (C2) UngrabMouse — 拖动期间意外失去 capture 时重新获取 ──
    if (event->type() == QEvent::UngrabMouse) {
#ifndef QT_NO_DEBUG
        qDebug() << "[InputProxy::eventFilter] UngrabMouse"
                 << "m_dragging=" << m_dragging
                 << "buttons=" << (QGuiApplication::mouseButtons() & Qt::LeftButton ? "LEFT" : "none");
#endif
        if (m_dragging && (QGuiApplication::mouseButtons() & Qt::LeftButton)) {
            // ★ 必须同时检查物理左键状态：
            //   用户释放按钮时 Qt 也会先发 UngrabMouse（早于 MouseButtonRelease），
            //   此时 m_dragging 仍为 true，但左键已松开，不应 re-grab。
            //   只有物理左键仍按下时的 grab 丢失（如 setGeometry 移动窗口）才需要重取。
            grabMouse();
            return true;
        }
    }

    // ── (D) 命中测试守卫 — 统一通过 isInteractivePetPoint 拦截非交互点击 ──
    //  只有 preciseRegion.contains + hitTestAt + alphaTestAt 全部通过才放行。
    //  return true 只阻止 Qt 事件循环内继续派发（不涉及跨进程穿透），
    //  跨进程穿透已由 OS 级 setMask(preciseRegion) 处理。
    //  MASK_FALSE_POSITIVE：点在 preciseRegion 内但 hitTest 未命中，
    //  说明该坐标在 Drawable AABB 的透明间隙内。
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::ContextMenu:
    {
        QPoint globalPos;
        if (event->type() == QEvent::ContextMenu) {
            auto *ce = static_cast<QContextMenuEvent *>(event);
            globalPos = ce->globalPos();
        } else {
            auto *me = static_cast<QMouseEvent *>(event);
            globalPos = me->globalPosition().toPoint();
        }

        const QPoint petLocal = m_petWindow->mapFromGlobal(globalPos);
        if (!isInteractivePetPoint(petLocal)) {
            // 未命中交互点 → 清理 pending 状态并阻止事件派发
#ifndef QT_NO_DEBUG
            const bool isRelease = (event->type() == QEvent::MouseButtonRelease);
            qDebug() << "[InputProxy::eventFilter] 非交互点击被拦截"
                     << "type=" << event->type()
                     << "globalPos=" << globalPos
                     << "petLocal=" << petLocal
                     << "m_dragging=" << m_dragging
                     << (isRelease ? "[RELEASE swallowed — cleanupDrag in filter]" : "")
                     << "grabber=" << QWidget::mouseGrabber();
#endif
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
// Geometry 同步
// ============================================================================

void InputProxyWindow::syncGeometry()
{
    // ── 重入保护 ──
    if (m_syncingGeometry) return;
    QScopedValueRollback<bool> guard(m_syncingGeometry, true);

    if (!m_petWindow) return;

    const QRect petGeo = m_petWindow->geometry();
    const QSize oldSize = size();
    const QRect oldGeo = geometry();

    // ── Proxy geometry 始终与 PetWindow 完全一致 ──
    // 允许 x/y 为负数，允许 right/bottom 超出屏幕。
    // 不根据 screenAt / availableGeometry / screenGeometry 裁剪或移动 Proxy。
    // 超出屏幕的部分由 OS 自然裁剪输入可见性，不影响模型可见区域的输入覆盖。
    setGeometry(petGeo);

    const QRect actualProxyGeo = geometry();

#ifndef QT_NO_DEBUG
    if (oldGeo != petGeo || actualProxyGeo != petGeo) {
        qDebug() << "[InputProxy::syncGeometry]"
                 << "petGeo=" << petGeo
                 << "oldProxy=" << oldGeo
                 << "requested=" << petGeo
                 << "actualProxy=" << actualProxyGeo
                 << "adjusted=" << (actualProxyGeo != petGeo ? "YES Qt adjusted" : "no")
                 << "oldSize=" << oldSize
                 << "m_dragging=" << m_dragging;
    }
#endif

    // ── 若之前因离屏隐藏，现在重新显示 ──
    if (!isVisible()) {
        show();
        raise();
    }

    // ── geometry 变化 → 刷新 mask ──
    if (oldSize != size()) {
        updateInputMask();
    }
}

// ============================================================================
// Mask 更新 — 使用逐 Drawable AABB + 4px padding 构建多矩形 QRegion
// ============================================================================

void InputProxyWindow::updateInputMask()
{
    if (!m_live2DView || !m_petWindow) {
        return;
    }

    // 1. 获取所有可见 Drawable 的紧贴包围盒（PetWindow 本地像素坐标）
    QVector<QRect> drawableRects;
    m_live2DView->getVisibleDrawableRects(drawableRects);

    if (drawableRects.isEmpty()) {
        return;
    }

    // 2. 构建规范 PetWindow-local region（不被裁剪、不被平移）
    QRegion canonicalPetRegion;
    for (const QRect &rect : drawableRects) {
        QRect r = rect.adjusted(-kMaskPaddingPx, -kMaskPaddingPx,
                                 kMaskPaddingPx, kMaskPaddingPx);
        canonicalPetRegion |= QRegion(r);
    }

    // 3. 读取实时 geometry（每次调用重新读取）
    const QRect petGeo = m_petWindow->geometry();
    const QRect actualProxyGeo = geometry();
    const bool isClipped = (petGeo != actualProxyGeo);

#ifndef QT_NO_DEBUG
    if (isClipped) {
        qDebug() << "[InputProxy::updateInputMask]"
                 << "isClipped=" << (isClipped ? "YES" : "no")
                 << "petGeo=" << petGeo
                 << "proxyGeo=" << actualProxyGeo;
    }
#endif

    // 4. 从 canonical region 推导 proxy-local mask
    QRegion proxyMask = DeriveProxyMask(canonicalPetRegion, petGeo, actualProxyGeo);

    if (proxyMask.isEmpty()) {
        return;
    }

    // 5. 全窗口 → 清除 mask
    if (proxyMask == QRegion(QRect(QPoint(0, 0), actualProxyGeo.size()))) {
        if (m_preciseRegionValid) {
            clearMask();
            m_preciseRegionValid = false;
        }
        return;
    }

    // 6. 防重复
    if (m_preciseRegionValid && m_preciseRegion == proxyMask) {
        return;
    }

    // 7. 应用 mask
    setMask(proxyMask);
    m_preciseRegion = proxyMask;
    m_preciseRegionValid = true;

#ifndef QT_NO_DEBUG
    qDebug() << "[InputProxy] updateInputMask"
             << "drawableCount=" << drawableRects.size()
             << "petGeo=" << petGeo
             << "proxyGeo=" << actualProxyGeo
             << "maskBB=" << proxyMask.boundingRect();
#endif
}

// ============================================================================
// 鼠标事件
// ============================================================================

void InputProxyWindow::mousePressEvent(QMouseEvent *event)
{
    const QPoint globalPos = event->globalPosition().toPoint();

    // 只处理左键
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    // ── 统一坐标：global → PetWindow-local ──
    const QPoint petLocal = m_petWindow->mapFromGlobal(globalPos);

    // 统一命中检测 — eventFilter 已拦截非交互点，此 checks 为兜底
    if (!isInteractivePetPoint(petLocal)) {
        event->ignore();
        return;
    }

    // Hit-area 细分（头部/身体），使用 PetWindow-local 坐标
    const float px = static_cast<float>(petLocal.x());
    const float py = static_cast<float>(petLocal.y());
    bool isHead = false;
    m_live2DView->hitTestAt(px, py, isHead);
    bool alphaOk = m_live2DView->alphaTestAt(px, py);
    m_pressInHeadArea = alphaOk && isHead;
    m_pressInBodyArea = alphaOk && !isHead;

    // ── 拖动锚点：PetWindow-local，一次拖动期间不变 ──
    m_dragAnchorInPet = petLocal;
    m_pressGlobalStartPos = globalPos;

#ifndef QT_NO_DEBUG
    qDebug() << "[InputProxy] mousePress"
             << "petGeo=" << (m_petWindow ? m_petWindow->geometry() : QRect())
             << "proxyGeo=" << geometry()
             << "globalPos=" << globalPos
             << "petLocal=" << petLocal
             << "anchorInPet=" << m_dragAnchorInPet;
#endif
}

void InputProxyWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    if (!m_dragging) {
        // 尚未进入拖拽 — 检查是否超过拖拽阈值（使用全局坐标）
        if ((m_pressInHeadArea || m_pressInBodyArea)) {
            const int dist = (event->globalPosition().toPoint() - m_pressGlobalStartPos).manhattanLength();
            if (dist > kDragThreshold) {
                // 超过阈值 → 启动拖拽
                m_dragging = true;
                grabMouse();
#ifndef QT_NO_DEBUG
                qDebug() << "[InputProxy] drag START"
                         << "grabber=" << QWidget::mouseGrabber()
                         << "petGeo=" << (m_petWindow ? m_petWindow->geometry() : QRect())
                         << "proxyGeo=" << geometry();
#endif

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
    // ── (1) 左键且正在拖动 → 无条件结束拖动，优先于双击抑制 ──
    if (event->button() == Qt::LeftButton && m_dragging) {
        cleanupDrag();
        syncGeometry();
        emit dragFinished();
        return;
    }

    // ── (2) 左键之外的按键 → 基类处理 ──
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    // ── (3) 双击抑制：第二次 release 不触发普通单击 ──
    if (m_suppressNextReleaseSingle) {
#ifndef QT_NO_DEBUG
        qDebug() << "[InputProxy::mouseRelease RETURN] reason=suppressNextReleaseSingle";
#endif
        m_suppressNextReleaseSingle = false;
        m_pressInHeadArea = false;
        m_pressInBodyArea = false;
        return;
    }

    // ── (4) 非拖动的普通单击释放 ──
    if (m_pressInHeadArea) {
        m_pressInHeadArea = false;
        if (m_petWindow) m_petWindow->triggerPet();
    } else if (m_pressInBodyArea) {
        m_pressInBodyArea = false;
        if (m_live2DView) m_live2DView->playTapMotion();
    }

    // 确保 press 状态清零
    m_pressInHeadArea = false;
    m_pressInBodyArea = false;
}

void InputProxyWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    // 使用本次双击的坐标重新做命中检测（PetWindow-local）
    const QPoint petLocal = m_petWindow->mapFromGlobal(event->globalPosition().toPoint());
    if (!isInteractivePetPoint(petLocal)) {
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
#ifndef QT_NO_DEBUG
    qDebug() << "[InputProxy] cleanupDrag"
             << "m_dragging=" << m_dragging
             << "grabber=" << QWidget::mouseGrabber()
             << "mouseButtons=" << QGuiApplication::mouseButtons();
#endif
    m_dragging = false;
    m_pressInHeadArea = false;
    m_pressInBodyArea = false;
    m_suppressNextReleaseSingle = false;
    if (QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
}

bool InputProxyWindow::isInteractivePetPoint(const QPoint &petLocalPos) const
{
    // (1) OS-level mask：需将 PetWindow-local 点转换到 proxy-local 检测
    //     proxy-local = petLocal + petTopLeft - proxyTopLeft
    if (m_preciseRegionValid && m_petWindow) {
        const QRect petGeo = m_petWindow->geometry();
        const QRect proxyGeo = geometry();
        const QPoint proxyLocal = petLocalPos + (petGeo.topLeft() - proxyGeo.topLeft());
        if (!m_preciseRegion.contains(proxyLocal))
            return false;
    }

    // (2) 模型交互区域：直接使用 PetWindow-local 坐标
    const float petPx = static_cast<float>(petLocalPos.x());
    const float petPy = static_cast<float>(petLocalPos.y());
    bool isHead = false;
    if (!m_live2DView || !m_live2DView->hitTestAt(petPx, petPy, isHead))
        return false;

    // (3) 像素透明度
    if (!m_live2DView->alphaTestAt(petPx, petPy))
        return false;

    return true;
}

void InputProxyWindow::handleDragMove(QMouseEvent *event)
{
    const QPoint newPetTopLeft = ComputeNewPosition(
        event->globalPosition().toPoint(), m_dragAnchorInPet);

    if (m_petWindow) {
        // 第一帧：globalMouse 未变 → newPetTopLeft == 当前 PetWindow pos
        m_petWindow->move(newPetTopLeft);
        // Proxy 由 PetWindow Move eventFilter → syncGeometry 自动跟随
        // 不再显式调用 setGeometry — 避免与 syncGeometry 屏幕裁剪冲突
    } else {
        move(newPetTopLeft);
    }
}

// ============================================================================
// 右键菜单
// ============================================================================

void InputProxyWindow::contextMenuEvent(QContextMenuEvent *event)
{
    // 使用本次 ContextMenu 的坐标重新做命中检测（PetWindow-local）
    // eventFilter 已 return true 拦截非交互右键，此 checks 为兜底
    const QPoint petLocal = m_petWindow->mapFromGlobal(event->globalPos());
    if (!isInteractivePetPoint(petLocal)) {
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
// moveEvent / resizeEvent — 跟踪实际 geometry
// ============================================================================

void InputProxyWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
}

void InputProxyWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

// ============================================================================
// 绘制：仅在多矩形 preciseRegion 内绘制 alpha=1 输入表面
// ============================================================================

void InputProxyWindow::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);

    // 整窗填充 alpha=0（确保透明）
    painter.fillRect(rect(), QColor(0, 0, 0, 0));

    // 在 preciseRegion 内绘制 alpha=1 输入表面（视觉不可见，确保 WS_EX_LAYERED 接收鼠标事件）
    if (m_preciseRegionValid) {
        painter.setClipRegion(m_preciseRegion);
        painter.fillRect(m_preciseRegion.boundingRect(), QColor(0, 0, 0, 1));
    }
}
