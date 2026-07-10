#include "InputProxyWindow.h"
#include "InputRegionBuilder.h"

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

    // ★ 创建 mask 更新定时器
    ensureInputMaskTimer();

    // 初始 mask
    updateInputMask();
}

InputProxyWindow::~InputProxyWindow()
{
    cleanupDrag();
    delete m_inputMaskTimer;
    m_inputMaskTimer = nullptr;
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
    //  REGION_INCONSISTENT：点在 preciseRegion 外——仅表示事件路由/缓存不一致，
    //  不是 OS mask 穿透（因为 InputProxyWindow 收到事件即表示 OS mask 已放行）。
    //  HITTEST_MISS：点在 preciseRegion 内但 hitTest 未命中，
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
        requestInputMaskUpdate(QStringLiteral("resize"), 0);
    }
}

// ============================================================================
// Input Mask v2 — last-good-mask 事务发布
// ============================================================================

bool InputProxyWindow::ensureInputMaskTimer()
{
    if (m_inputMaskTimer)
        return true;
    m_inputMaskTimer = new QTimer(this);
    m_inputMaskTimer->setSingleShot(true);
    connect(m_inputMaskTimer, &QTimer::timeout, this, [this]() {
        m_inputMaskUpdatePending = false;
        performInputMaskUpdate(QStringLiteral("timer"));
    });
    return true;
}

void InputProxyWindow::requestInputMaskUpdate(const QString &reason, int delayMs)
{
    if (!ensureInputMaskTimer())
        return;

    m_inputMaskUpdatePending = true;
    m_inputMaskTimer->start(delayMs);
}

void InputProxyWindow::updateInputMask()
{
    // 立即执行更新（无延迟）
    performInputMaskUpdate(QStringLiteral("direct"));
}

void InputProxyWindow::performInputMaskUpdate(const QString &reason)
{
    if (!m_live2DView || !m_petWindow) {
        return;
    }

    // ── 构建精确 candidate region（PetWindow-local 坐标） ──
    // buildPreciseInputRegion 使用 Live2DView 的宽高（= PetWindow client rect）
    // 得到的是 PetWindow-local 坐标系的 region
    const QRegion candidatePrecise = m_live2DView->buildPreciseInputRegion(m_inputMaskCellSize);

    // ── 坐标转换：PetWindow-local → Proxy-local ──
    const QRect petGeo = m_petWindow->geometry();
    const QRect actualProxyGeo = geometry();
    const QRegion candidate = DeriveProxyMask(candidatePrecise, petGeo, actualProxyGeo);

    const QSize currentProxySize = actualProxyGeo.size();
    const QString modelName = m_live2DView->modelName();
    const int cellSize = m_inputMaskCellSize;

    // ── 信任检查 → 发布或保留 ──

    // 情况 1：empty candidate
    if (candidate.isEmpty()) {
        return;
    }

    // 情况 2：suspicious candidate（non-empty 但未通过 trust check）
    if (InputRegionBuilder::isSuspiciousInputRegion(candidate, currentProxySize)) {
        return;
    }

    // 情况 3：trusted region → 发布
    publishInputRegion(candidate, reason);
}

void InputProxyWindow::publishInputRegion(const QRegion &region, const QString &reason)
{
    if (region.isEmpty())
        return;

    const QRect bb = region.boundingRect();
    const QSize currentSize = geometry().size();
    const QString modelName = m_live2DView ? m_live2DView->modelName() : QString();

    // ── 防重复 ──
    if (m_preciseRegionValid && m_preciseRegion == region) {
        // region 没变，但确保 last good 已记录
        if (!m_hasLastGoodInputRegion) {
            m_lastGoodInputRegion = region;
            m_hasLastGoodInputRegion = true;
            m_lastGoodProxySize = currentSize;
        }
        return;
    }

    // ── 全窗口 → clearMask ──
    // 理论上精确 grid 不会产生全窗口结果，但保留防御性检查
    if (region == QRegion(QRect(QPoint(0, 0), currentSize))) {
        if (m_preciseRegionValid) {
            clearMask();
        }
        m_preciseRegionValid = false;
        m_preciseRegion = QRegion();
        // 不覆盖 last good（clearMask 是临时状态）
        return;
    }

    // ── 应用 OS mask ──
    setMask(region);
    m_preciseRegion = region;
    m_preciseRegionValid = true;

    // ── 更新 last good ──
    m_lastGoodInputRegion = region;
    m_hasLastGoodInputRegion = true;
    m_lastGoodProxySize = currentSize;
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

    // ── Hit-area 细分（头部/身体），使用 PetWindow-local 坐标 ──
    const float px = static_cast<float>(petLocal.x());
    const float py = static_cast<float>(petLocal.y());
    bool isHead = false;
    bool hitHit = m_live2DView->hitTestAt(px, py, isHead);
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
        // 拖拽释放后请求重新构建 mask（滞后，不阻塞 release）
        requestInputMaskUpdate(QStringLiteral("drag-release"), 50);
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
