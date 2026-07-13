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
#include <QDateTime>
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

    // ── 🔬 Phase 2: Alpha 捕获完成 → 重新构建输入 mask ──
    //    使用 QueuedConnection 避免 paintGL 内同步调用 setMask 导致递归 repaint。
    //    信号触发时 Alpha 缓冲区已就绪，performInputMaskUpdate 从中构建 Alpha Region 并交集。
    //    纳入 generation 去重：仅当捕获的 generation 与当前一致且尚未完成构建才执行。
    if (m_live2DView) {
        connect(m_live2DView, &Live2DView::alphaCaptureReady, this, [this]() {
            if (!m_live2DView) return;
            const uint64_t captureGen = m_live2DView->alphaCaptureGeneration();

            // ── 只接受当前 generation 的有效快照 ──
            if (captureGen != m_updateGeneration) {
                return;  // 旧 generation 的快照 → 丢弃
            }
            if (m_lastCompletedGeneration == m_updateGeneration) {
                return;  // 该 generation 已完成 → 忽略
            }
            if (!m_live2DView->isAlphaCaptureValid()) {
                return;  // 快照尺寸/DPR 不匹配 → 等待 watchdog 兜底
            }

            // ── 有效 → 取消定时器和 watchdog ──
            m_pendingTimerGeneration = 0;
            m_inputMaskTimer->stop();
            m_alphaCapturePendingGen = 0;
            m_watchdogActive = false;

            // ── 发布 intersection ──
            performInputMaskUpdate(QStringLiteral("alpha-capture"));

            // ── 发布成功后标记 generation 完成 ──
            m_lastCompletedGeneration = m_updateGeneration;
        }, Qt::QueuedConnection);
    }


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
        const bool interactive = isInteractivePetPoint(petLocal);


        if (!interactive) {
            // 未命中交互点 → 清理 pending 状态并阻止事件派发
            cleanupDrag();
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease:
    {
        // Release also needs guard check, but no separate diagnostics
        QPoint globalPos = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const QPoint petLocal = m_petWindow->mapFromGlobal(globalPos);
        if (!isInteractivePetPoint(petLocal)) {
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

    // ── geometry 变化 → 失效旧 region 并创建新 generation ──
    if (oldSize != size()) {
        // ── 旧精确 Region 已不匹配新尺寸，立即失效 ──
        m_preciseRegionValid = false;
        m_preciseRegion = QRegion();

        // ── 延迟精确构建（0ms 定时器，下一轮事件循环执行）──
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

        const uint64_t currentGen = m_updateGeneration;
        const uint64_t timerGen = m_pendingTimerGeneration;

        // ── Generation 去重 ──
        if (timerGen != currentGen) {
            // 旧 generation 的定时器 → 忽略
            return;
        }
        if (m_lastCompletedGeneration == currentGen) {
            // 该 generation 已由 alphaCaptureReady 完成构建 → 忽略
            return;
        }

        // 检查 Alpha capture 是否已就绪（属于当前 generation）
        const bool alphaReady = m_live2DView
            && m_live2DView->isAlphaCaptureValid()
            && m_live2DView->alphaCaptureGeneration() == currentGen;

        if (alphaReady) {
            // ── Alpha 就绪 → 发布 intersection，标记 generation 完成 ──
            m_pendingTimerGeneration = 0;
            m_watchdogActive = false;
            performInputMaskUpdate(QStringLiteral("timer"));
            m_lastCompletedGeneration = currentGen;
            return;
        }

        // ════════════════════════════════════════════════════════════════
        //  Alpha 尚未就绪 → 区分"捕获仍在重试"和"已明确失败"
        // ════════════════════════════════════════════════════════════════
        //  不发布 geometric-only，不标记 generation 完成。
        //  保持当前已有 Mask，等待 alphaCaptureReady 到达。
        //  使用有限 watchdog 处理真正的捕获失败。
        // ════════════════════════════════════════════════════════════════
        const bool capturePending = m_live2DView && m_live2DView->alphaCapturePending();

        if (capturePending) {
            // ── 捕获仍在重试 → 启动/继续 watchdog ──
            if (m_watchdogActive && m_watchdogGeneration == currentGen) {
                // Watchdog 已激活 → 检查是否超时
                m_watchdogCheckCount++;
                if (m_watchdogCheckCount >= kWatchdogMaxCheckCount) {
                    // Watchdog 超时 → 兜底 geometric-only fallback
                    m_pendingTimerGeneration = 0;
                    m_watchdogActive = false;
                    performInputMaskUpdate(QStringLiteral("watchdog-fallback"));
                    m_lastCompletedGeneration = currentGen;
                    return;
                }
                // 继续等待
                m_pendingTimerGeneration = currentGen;
                m_inputMaskTimer->start(kWatchdogCheckIntervalMs);
            } else {
                // 首次遇到 Alpha 未就绪 → 启动 watchdog
                m_watchdogActive = true;
                m_watchdogGeneration = currentGen;
                m_watchdogStartTick = QDateTime::currentMSecsSinceEpoch();
                m_watchdogCheckCount = 1;
                m_pendingTimerGeneration = currentGen;
                m_inputMaskTimer->start(kWatchdogCheckIntervalMs);
            }
        } else {
            // ── 捕获已明确失败（重试耗尽或无可用的 pending 请求）──
            //  不等待 watchdog 超时，立即 geometric-only fallback。
            //  对应场景：glReadPixels 连续失败超过 kAlphaCaptureMaxRetries，
            //  或 requestAlphaCapture 被 m_glReady=false 屏蔽后恢复失败。
            m_pendingTimerGeneration = 0;
            m_watchdogActive = false;
            performInputMaskUpdate(QStringLiteral("capture-failed"));
            m_lastCompletedGeneration = currentGen;
            return;
        }
    });
    return true;
}

void InputProxyWindow::requestInputMaskUpdate(const QString &reason, int delayMs)
{
    if (!ensureInputMaskTimer())
        return;

    // ── Generation 去重与合并 ──
    //  缩放相关请求 (resize, scale) 在定时器活跃时合并到同一 generation。
    //  不活跃时每个原因产生独立 generation。
    static const QStringList mergeableReasons {
        QStringLiteral("resize"),
        QStringLiteral("scale"),
        QStringLiteral("stable-bounds"),
        QStringLiteral("model-loaded"),
    };

    const bool isMergeable = mergeableReasons.contains(reason);
    const bool timerActive = m_pendingTimerGeneration > 0;

    if (isMergeable && timerActive && m_lastCompletedGeneration < m_updateGeneration) {
        // ── 合并到当前 pending generation ──
        //  不递增 generation，仅重启定时器并确保 Alpha capture 已请求。
        if (m_live2DView && !m_live2DView->isAlphaCaptureValid()) {
            m_alphaCapturePendingGen = m_updateGeneration;
            m_live2DView->requestAlphaCapture(m_updateGeneration);
            // Alpha 被外部（resizeGL/模型切换）失效，重新请求捕获 → 重置 watchdog
            resetWatchdog();
        }
        m_pendingTimerGeneration = m_updateGeneration;
        m_inputMaskTimer->start(delayMs);
        return;
    }

    // ── 新 generation ──
    m_updateGeneration++;
    m_lastCompletedGeneration = 0;   // 新 generation 尚未完成
    m_inputMaskUpdatePending = true;

    // ── 清零旧 generation 的残留状态 ──
    m_pendingTimerGeneration = 0;
    m_alphaCapturePendingGen = 0;
    resetWatchdog();

    // ★ 新 generation 始终请求该 generation 的 Alpha 捕获。
    //   旧快照即使尺寸匹配也不代表当前模型状态/动画内容。
    //   requestAlphaCapture 内部有同 gen 去重，不会重复排队。
    m_alphaCapturePendingGen = m_updateGeneration;
    if (m_live2DView) {
        m_live2DView->requestAlphaCapture(m_updateGeneration);
    }

    m_pendingTimerGeneration = m_updateGeneration;
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

    // ── 构建精确 geometric region（PetWindow-local 坐标） ──
    const QRegion candidatePrecise = m_live2DView->buildPreciseInputRegion(m_inputMaskCellSize);

    // ── 构建 Alpha Region 并取交集 ──
    QRegion alphaRegion;
    QRegion finalRegion;

    if (m_live2DView->isAlphaCaptureValid()) {
        alphaRegion = m_live2DView->buildAlphaRegion(m_inputMaskCellSize);

        if (!alphaRegion.isEmpty() && !candidatePrecise.isEmpty()) {
            // finalRegion = geometricRegion & alphaRegion（几何区域内的 Alpha 透明区被排除）
            finalRegion = candidatePrecise & alphaRegion;
        } else if (!candidatePrecise.isEmpty()) {
            finalRegion = candidatePrecise;
        }
    } else {
        finalRegion = candidatePrecise;
    }

    if (finalRegion.isEmpty()) {
        return;
    }

    // ── 坐标转换：PetWindow-local → Proxy-local ──
    const QRect petGeo = m_petWindow->geometry();
    const QRect actualProxyGeo = geometry();
    const QRegion candidate = DeriveProxyMask(finalRegion, petGeo, actualProxyGeo);

    const QSize currentProxySize = actualProxyGeo.size();

    // ── 信任检查 → 发布或保留 ──

    // 情况 1：empty candidate
    if (candidate.isEmpty()) {
        return;
    }

    // 情况 2：suspicious candidate（non-empty 但未通过 trust check）
    if (InputRegionBuilder::isSuspiciousInputRegion(candidate, currentProxySize)) {
        return;
    }

    // ── 发布 ──
    publishInputRegion(candidate, reason);
}

void InputProxyWindow::publishInputRegion(const QRegion &region, const QString &reason)
{
    if (region.isEmpty())
        return;

    const QSize currentSize = geometry().size();

    // ── 防重复 ──
    if (m_preciseRegionValid && m_preciseRegion == region) {
        if (!m_hasLastGoodInputRegion) {
            m_lastGoodInputRegion = region;
            m_hasLastGoodInputRegion = true;
            m_lastGoodProxySize = currentSize;
            update();
        } else if (m_lastGoodProxySize != currentSize) {
            // region 相同但 widget size 变化 → 刷新 alpha backing store
            m_lastGoodProxySize = currentSize;
            update();
        }
        return;
    }

    // ── 应用 OS mask ──
    //  不使用 clearMask：clearMask 完全移除 OS 级别裁剪，缩放期间旧 Mask
    //  不匹配新窗口尺寸时整个窗口无保护。setMask(newRegion) 原子替换。
    setMask(region);
    m_preciseRegion = region;
    m_preciseRegionValid = true;

    // ── 更新 last good ──
    m_lastGoodInputRegion = region;
    m_hasLastGoodInputRegion = true;
    m_lastGoodProxySize = currentSize;

    // ── 请求重绘以刷新 alpha backing store ──
    update();
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
        // ★ 拖动释放不重建 Mask：窗口仅移动，局部轮廓不变，
        //    原 Mask 随窗口自动移动，无需重新构建。
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

void InputProxyWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);

    // 清除旧尺寸、旧 region 遗留的非零 alpha。
    painter.fillRect(rect(), QColor(0, 0, 0, 0));

    // 只允许 precise region 内 alpha 非零
    if (m_preciseRegionValid && !m_preciseRegion.isEmpty()) {
        painter.setClipRegion(m_preciseRegion);
        painter.fillRect(rect(), QColor(0, 0, 0, 1));
    }
}

// ============================================================================
// 🔬 Phase 2: Alpha Capture Watchdog
// ============================================================================

void InputProxyWindow::resetWatchdog()
{
    m_watchdogActive = false;
    m_watchdogGeneration = 0;
    m_watchdogStartTick = 0;
    m_watchdogCheckCount = 0;
}
