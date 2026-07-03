#include "InputProxyWindow.h"

#include "../pet/PetWindow.h"
#include "../pet/Live2DView.h"

#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QHideEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QDebug>
#include <QVector>

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
    // ── (A) PetWindow move 跟踪 — 自动同步 geometry ──
    if (watched == m_petWindow) {
        if (event->type() == QEvent::Move) {
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
        cleanupDrag();
    }

    // ── (D) 命中测试守卫 — 统一通过 isInteractivePoint 拦截非交互点击 ──
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
// Geometry 同步
// ============================================================================

void InputProxyWindow::syncGeometry()
{
    if (!m_petWindow) return;

    const QRect petGeo = m_petWindow->geometry();

    // 默认：InputProxyWindow geometry = PetWindow geometry
    QRect proxyGeo = petGeo;

    // 如果 PetWindow 超出显示器可用区域，裁剪到可用区域并修正 mask 坐标偏移
    {
        QScreen *screen = QGuiApplication::screenAt(petGeo.center());
        if (screen) {
            const QRect available = screen->availableGeometry();
            const QRect intersected = petGeo.intersected(available);
            if (intersected.isValid() && intersected != petGeo) {
                // 计算偏移量：PetWindow origin → 裁剪后 origin
                m_geoOffsetX = intersected.x() - petGeo.x();
                m_geoOffsetY = intersected.y() - petGeo.y();
                proxyGeo = intersected;
            } else {
                m_geoOffsetX = 0;
                m_geoOffsetY = 0;
            }
        }
    }

    // 多显示器支持：如果窗口中心不在任何屏幕上，尝试 fallback 到主屏幕
    if (!QGuiApplication::screenAt(proxyGeo.center())) {
        QScreen *primary = QGuiApplication::primaryScreen();
        if (primary) {
            const QRect primaryGeo = primary->availableGeometry();
            int cx = qBound(primaryGeo.left(), proxyGeo.left(),
                            primaryGeo.right() - qMin(proxyGeo.width(), primaryGeo.width()));
            int cy = qBound(primaryGeo.top(), proxyGeo.top(),
                            primaryGeo.bottom() - qMin(proxyGeo.height(), primaryGeo.height()));
            m_geoOffsetX = cx - petGeo.x();
            m_geoOffsetY = cy - petGeo.y();
            proxyGeo.moveTopLeft(QPoint(cx, cy));
        }
    }

    // ── 无有效屏幕交集 → 暂时隐藏 proxy ──
    if (!QGuiApplication::screenAt(proxyGeo.center())) {
        if (isVisible())
            hide();
        return;
    }

    // ── geometry 有效 → 应用 ──
    setGeometry(proxyGeo);

    // geometry 恢复有效 → 重新显示 proxy
    if (!isVisible()) {
        show();
        raise();
    }
}

// ============================================================================
// Mask 更新 — 使用逐 Drawable AABB + 4px padding 构建多矩形 QRegion
// ============================================================================

void InputProxyWindow::updateInputMask()
{
    if (!m_live2DView) {
        // 无模型 → 保留上一次有效的 preciseRegion，不清除 mask
        return;
    }

    // 1. 获取所有可见 Drawable 的紧贴包围盒（PetWindow 本地像素坐标）
    QVector<QRect> drawableRects;
    m_live2DView->getVisibleDrawableRects(drawableRects);

    if (drawableRects.isEmpty()) {
        // 无可见 drawable → 保留上一次有效的 preciseRegion
        return;
    }

    // 2. 构建多矩形 QRegion：每个 Drawable AABB + 4px padding
    //    坐标从 PetWindow 本地 → InputProxyWindow 本地（考虑几何裁剪偏移）
    QRegion preciseRegion;
    for (const QRect &rect : drawableRects) {
        QRect r = rect.adjusted(-kMaskPaddingPx, -kMaskPaddingPx,
                                 kMaskPaddingPx, kMaskPaddingPx);
        r.translate(-m_geoOffsetX, -m_geoOffsetY);   // PetWindow local → proxy local
        preciseRegion |= QRegion(r);
    }

    // 3. 约束到 InputProxyWindow 边界
    preciseRegion = preciseRegion.intersected(QRect(0, 0, width(), height()));

    if (preciseRegion.isEmpty()) {
        // 裁剪后为空 → 保留上一次有效的 preciseRegion
        return;
    }

    // 4. 全窗口 → 清除 mask（罕见情况）
    if (preciseRegion == QRegion(QRect(0, 0, width(), height()))) {
        if (m_preciseRegionValid) {
            clearMask();
            m_preciseRegionValid = false;
        }
        return;
    }

    // 5. 防重复：相同 region 跳过
    if (m_preciseRegionValid && m_preciseRegion == preciseRegion) {
        return;
    }

    // 6. 应用 mask
    setMask(preciseRegion);
    m_preciseRegion = preciseRegion;
    m_preciseRegionValid = true;
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
    const float px = static_cast<float>(event->position().x());
    const float py = static_cast<float>(event->position().y());
    const float petPx = px + m_geoOffsetX;
    const float petPy = py + m_geoOffsetY;
    bool isHead = false;
    m_live2DView->hitTestAt(petPx, petPy, isHead);
    bool alphaOk = m_live2DView->alphaTestAt(petPx, petPy);
    m_pressInHeadArea = alphaOk && isHead;
    m_pressInBodyArea = alphaOk && !isHead;

    // 记录拖拽起始偏移 — 不立即设 m_dragging，需 mouseMove 超阈值后才拖拽
    m_dragStartOffset = event->pos();
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
        if (m_dragging) {
            // 拖拽结束
            cleanupDrag();
            emit dragFinished();
        } else if (m_pressInHeadArea) {
            // 头部点击 → 摸摸头
            m_pressInHeadArea = false;
            qInfo() << "[InputProxy] headClicked → triggerPet";
            if (m_petWindow) m_petWindow->triggerPet();
        } else if (m_pressInBodyArea) {
            // 身体点击 → 随机动作
            m_pressInBodyArea = false;
            qInfo() << "[InputProxy] bodyClicked → playTapMotion";
            if (m_live2DView) m_live2DView->playTapMotion();
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
        // 未命中 → 不调 InteractionManager，不播放动作，不触发任何桌宠交互
        // （eventFilter 已 return true 阻止了事件派发，此 checks 为兜底）
        cleanupDrag();
        return;
    }

    // 命中交互点 → 允许 InteractionManager 正常处理双击语义
    QWidget::mouseDoubleClickEvent(event);
}

void InputProxyWindow::cleanupDrag()
{
    m_dragging = false;
    m_pressInHeadArea = false;
    m_pressInBodyArea = false;
    if (QWidget::mouseGrabber() == this) {
        releaseMouse();
    }
}

bool InputProxyWindow::isInteractivePoint(const QPointF &proxyLocalPos)
{
    const QPoint pt = proxyLocalPos.toPoint();

    // (1) OS-level mask：必须在 preciseRegion 内
    if (!m_preciseRegionValid || !m_preciseRegion.contains(pt))
        return false;

    // (2) 模型内容：必须命中交互区域（HitArea / tight bounds）
    const float petPx = static_cast<float>(pt.x()) + static_cast<float>(m_geoOffsetX);
    const float petPy = static_cast<float>(pt.y()) + static_cast<float>(m_geoOffsetY);
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
    const QPoint newGlobal = event->globalPosition().toPoint() - m_dragStartOffset;

    // PetWindow 优先移动 — InputProxyWindow 始终跟随 PetWindow，不自发移动
    if (m_petWindow) {
        m_petWindow->move(newGlobal);
        setGeometry(m_petWindow->geometry());
    } else {
        move(newGlobal);
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
