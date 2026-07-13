#include "PetWindow.h"
#include "Live2DView.h"
#include "SpeechBubble.h"
#include "FloatingTextWidget.h"

#include <QMouseEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>
#include <QEasingCurve>
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QRandomGenerator>
#include <QDebug>
#include <QSettings>
#include <QApplication>
#include <QScreen>
#include <QAbstractNativeEventFilter>
#include <QTimer>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#endif

PetWindow::PetWindow(const CharacterConfig &initialConfig, QWidget *parent)
    : QWidget(parent)
    , m_lastFeedDate(QDate::currentDate())
    , m_dailyResetDate(QDate::currentDate())
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool
                   | Qt::WindowTransparentForInput);  // 永久输入穿透，只负责 Live2D 渲染
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);

    // nativeEventFilter → HTTRANSPARENT 实现全窗口穿透
#if defined(Q_OS_WIN)
    qApp->installNativeEventFilter(this);
#endif

    m_live2DView = new Live2DView(this);
    m_live2DView->setGeometry(rect());

    connect(m_live2DView, &Live2DView::modelLoaded, this, [this](double cw, double ch,
                                                                  double tightW, double tightH) {
        m_tightModelWidth  = tightW;
        m_tightModelHeight = tightH;
        m_canvasWidth = cw;
        m_canvasHeight = ch;
        computeAndApplyGeometry(false);
        emit modelSizeLoaded();

        // ── 每次模型加载时重新连接 stableBoundsReady，按值捕获当前 generation ──
        // Live2DView::setupModel() 在 emit modelLoaded 前已启动
        // QTimer::singleShot(500ms, ...)，旧 timer 未被取消，其 fire 回调如果发生
        // 在新模型加载之后，generation 检查会使其被安全跳过。
        if (m_stableBoundsConnection)
            QObject::disconnect(m_stableBoundsConnection);
        const int gen = m_modelGeneration;
        m_stableBoundsConnection = connect(
            m_live2DView, &Live2DView::stableBoundsReady, this,
            [this, gen](double tightW, double tightH) {
                // 1) 在任何状态写入前捕获旧锚点（model pixel rect 独立于 m_tightModel*）
                QPoint oldAnchor;
                bool haveOldAnchor = false;
                {
                    float mx = 0.0f, my = 0.0f, mw = 0.0f, mh = 0.0f;
                    if (m_live2DView->getModelPixelRect(mx, my, mw, mh)
                        && mw > 0.0f && mh > 0.0f
                        && std::isfinite(mx) && std::isfinite(my)
                        && std::isfinite(mw) && std::isfinite(mh))
                    {
                        oldAnchor = mapToGlobal(
                            QPoint(qRound(mx + mw * 0.5), qRound(my + mh)));
                        haveOldAnchor = true;
                    }
                }

                // 2) generation 校验 — 在此之前不得修改任何模型相关状态
                if (gen != m_modelGeneration)
                    return;

                // 3) 新 bounds 有效性检查
                if (tightW <= 0.0 || tightH <= 0.0
                    || !std::isfinite(tightW) || !std::isfinite(tightH))
                    return;

                // 4) 更新 tight bounds 缓存
                m_tightModelWidth  = tightW;
                m_tightModelHeight = tightH;

                // 5) 用预捕获的旧锚点计算新几何
                if (haveOldAnchor)
                    computeAndApplyGeometry(oldAnchor);
                else
                    computeAndApplyGeometry(true);
            });
    });

    // 好感度变化 在 contextMenuEvent 中与操作文字合并显示，避免重叠

    // 点击模型头部 → 摸摸头
    connect(m_live2DView, &Live2DView::headClicked, this, [this]() {
        triggerPet();
    });

    // 点击模型身体 → 播放随机动作（由 Live2DView 内部 playTapMotion 处理）

    // 初始化说话气泡
    m_speechBubble = new SpeechBubble();

    // 初始分配饱腹感值
    allocateFeedValues();

    // 从本地文件恢复持久化数据
    loadState();

    applyCharacter(initialConfig);

}

PetWindow::~PetWindow()
{
    delete m_speechBubble;
}

void PetWindow::applyCharacter(const CharacterConfig &config)
{
    if (!config.isValid()) {
        qWarning() << "[PetWindow] 收到无效角色配置，忽略本次切换";
        return;
    }

    m_currentConfig = config;

    // m_currentModelScale 已由 loadState() 从持久化恢复（首次启动或异常时默认 1.0），
    // 不再使用 config.json 的 modelScale（如 default 角色的 6.0）

    const int placeholder = static_cast<int>(kBaseSize * m_currentModelScale);
    resize(placeholder, placeholder);
    if (m_live2DView) {
        m_live2DView->setGeometry(rect());
    }

    const QString &modelRef = config.live2dModel;
    QString modelAbsPath;

    if (modelRef.isEmpty()) {
        qWarning() << "[PetWindow] config.live2dModel 为空，无法加载模型";
        return;
    }

    if (modelRef.contains('/') || modelRef.contains('\\') || modelRef.startsWith('.')) {
        modelAbsPath = QDir(QDir(config.rootDir).absoluteFilePath(modelRef)).absolutePath();
        if (!modelAbsPath.endsWith('/') && !modelAbsPath.endsWith('\\'))
            modelAbsPath += QStringLiteral("/");
    } else {
#ifdef LIVE2D_RESOURCES_PATH
        modelAbsPath = QStringLiteral(LIVE2D_RESOURCES_PATH) + modelRef + QStringLiteral("/");
#else
        modelAbsPath = QStringLiteral("Resources/") + modelRef + QStringLiteral("/");
#endif
    }

    // ── 递增 generation，使上一模型的任何待处理回调立即失效 ──
    // 同步调用链：loadModel → loadPendingModelIfNeeded → SetupModel → emit modelLoaded
    // generation 在此处递增，modelLoaded 中只捕获（不再重复递增）
    m_modelGeneration++;

    m_live2DView->loadModel(modelAbsPath);
    m_live2DView->setModelScale(config.modelScale);
    setWindowTitle(config.name);
}

void PetWindow::setModelScale(double scale)
{
    scale = qBound(0.3, scale, kMaxModelScale);
    m_currentModelScale = scale;
    m_live2DView->setModelScale(scale);

    if (m_tightModelWidth > 0.0 && m_tightModelHeight > 0.0) {
        computeAndApplyGeometry(true);
    }

    saveState();

    emit modelScaleChanged(scale);
}

void PetWindow::computeWindowSize(double scale, double &outW, double &outH) const
{
    if (m_tightModelHeight <= 0.0 || m_canvasWidth <= 0.0 || m_canvasHeight <= 0.0
        || !std::isfinite(scale)) {
        outW = outH = kBaseSize * (std::isfinite(scale) ? qBound(0.3, scale, kMaxModelScale) : 1.0);
        return;
    }

    // ── 安全回退：极端的 tight bounds 宽高比 → 使用 Canvas 高度 ──
    // tight bounds 极窄或极宽时 tightH 可能偏离实际展示比例，
    // 使用 canvasH 确保 visibleH 公式不产生过大或过小的窗口。
    double effectiveTightH = m_tightModelHeight;
    {
        const double tightAspect = m_tightModelWidth / m_tightModelHeight;
        constexpr double kMinAspect = 0.125;
        constexpr double kMaxAspect = 8.0;
        if (tightAspect < kMinAspect || tightAspect > kMaxAspect) {
            effectiveTightH = m_canvasHeight;
        }
    }

    constexpr double comp = 1.0 / (1.0 + 2.0 * kAnimMarginRatio);
    const double divisor = effectiveTightH * comp;

    // ── 数值安全：接近零或非有限除数 → fallback ──
    if (!std::isfinite(divisor) || divisor < 1e-9) {
        outW = outH = kBaseSize * qBound(0.3, scale, kMaxModelScale);
        return;
    }

    // ★ 统一可见高度公式
    //    visibleH = tightHeight / canvasWidth * widgetWidth * comp = kBaseSize * scale
    //    → widgetWidth = kBaseSize * scale * canvasWidth / (tightHeight * comp)
    //    窗口宽高比与 canvas 一致，使 canvas 投影在窗口中居中且边距比例相等。
    const double widgetW = kBaseSize * scale * m_canvasWidth / divisor;
    const double widgetH = widgetW * m_canvasHeight / m_canvasWidth;

    // ── 数值安全：非有限结果 → fallback ──
    constexpr double kMaxWindowDim = 10000.0;
    if (!std::isfinite(widgetW) || !std::isfinite(widgetH)) {
        outW = outH = static_cast<double>(kBaseSize);
        return;
    }

    outW = qBound(static_cast<double>(kMinWindowSize), widgetW, kMaxWindowDim);
    outH = qBound(static_cast<double>(kMinWindowSize), widgetH, kMaxWindowDim);
}

void PetWindow::computeAndApplyGeometry(bool preserveAnchor)
{
    if (m_tightModelWidth <= 0.0 || m_tightModelHeight <= 0.0) return;
    if (m_canvasWidth <= 0.0 || m_canvasHeight <= 0.0) return;

    double newW, newH;
    computeWindowSize(m_currentModelScale, newW, newH);
    const int newW_int = qRound(newW);
    const int newH_int = qRound(newH);

    if (preserveAnchor && m_live2DView) {
        float mx = 0.0f, my = 0.0f, mw = 0.0f, mh = 0.0f;
        if (m_live2DView->getModelPixelRect(mx, my, mw, mh)
            && mw > 0.0f && mh > 0.0f
            && std::isfinite(mx) && std::isfinite(my)
            && std::isfinite(mw) && std::isfinite(mh))
        {
            const QPoint anchor = mapToGlobal(
                QPoint(qRound(mx + mw * 0.5), qRound(my + mh)));
            computeAndApplyGeometry(anchor);  // delegate to QPoint overload
            return;
        }
        // fallthrough if getModelPixelRect failed — no anchor preservation
    }

    // ── 无锚点保持：保持当前位置，只 resize ──
    int newX = x(), newY = y();
    if (!std::isfinite(static_cast<double>(newX)) || !std::isfinite(static_cast<double>(newY)))
        newX = newY = 0;
    QRect newRect(newX, newY, newW_int, newH_int);

    // ── 屏幕安全 ──
    {
        QScreen *screen = QGuiApplication::screenAt(newRect.center());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect avail = screen->availableGeometry();
            constexpr int minVis = kMinWindowSize;
            QPoint tl = newRect.topLeft();
            tl.setX(qBound(avail.left() - newRect.width() + minVis,
                           tl.x(), avail.right() - minVis));
            tl.setY(qBound(avail.top() - newRect.height() + minVis,
                           tl.y(), avail.bottom() - minVis));
            newRect.moveTopLeft(tl);
        }
    }
    setGeometry(newRect);
}

void PetWindow::computeAndApplyGeometry(const QPoint &oldAnchorGlobal)
{
    if (m_tightModelWidth <= 0.0 || m_tightModelHeight <= 0.0) return;
    if (m_canvasWidth <= 0.0 || m_canvasHeight <= 0.0) return;

    double newW, newH;
    computeWindowSize(m_currentModelScale, newW, newH);
    const int newW_int = qRound(newW);
    const int newH_int = qRound(newH);

    // ── 锚点保持：使用预捕获的旧锚点（不调用 getModelPixelRect） ──
    constexpr double comp = 1.0 / (1.0 + 2.0 * kAnimMarginRatio);
    const double oldW = static_cast<double>(width());
    const double oldCanvasW_px = oldW * comp;
    const double oldCanvasX0 = (oldW - oldCanvasW_px) * 0.5;
    const double oldCanvasH_px = oldCanvasW_px * m_canvasHeight / m_canvasWidth;
    const double oldCanvasY0 = (static_cast<double>(height()) - oldCanvasH_px) * 0.5;

    const QPoint localOld = mapFromGlobal(oldAnchorGlobal);
    const double lx = static_cast<double>(localOld.x());
    const double ly = static_cast<double>(localOld.y());
    const double nX = (oldCanvasW_px > 0.0)
        ? (lx - oldCanvasX0) / oldCanvasW_px : 0.5;
    const double nY = (oldCanvasH_px > 0.0)
        ? (ly - oldCanvasY0) / oldCanvasH_px : 0.9;

    const double newCanvasW_px = static_cast<double>(newW_int) * comp;
    const double newCanvasX0 = (static_cast<double>(newW_int) - newCanvasW_px) * 0.5;
    const double newCanvasH_px = newCanvasW_px * m_canvasHeight / m_canvasWidth;
    const double newCanvasY0 = (static_cast<double>(newH_int) - newCanvasH_px) * 0.5;

    const double newLX = newCanvasX0 + nX * newCanvasW_px;
    const double newLY = newCanvasY0 + nY * newCanvasH_px;

    int newX = oldAnchorGlobal.x() - (std::isfinite(newLX) ? qRound(newLX) : 0);
    int newY = oldAnchorGlobal.y() - (std::isfinite(newLY) ? qRound(newLY) : 0);

    // ── 数值安全 ──
    if (!std::isfinite(static_cast<double>(newX)) || !std::isfinite(static_cast<double>(newY))) {
        newX = x();
        newY = y();
    }

    QRect newRect(newX, newY, newW_int, newH_int);

    // ── 屏幕安全 ──
    {
        QScreen *screen = QGuiApplication::screenAt(newRect.center());
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect avail = screen->availableGeometry();
            constexpr int minVis = kMinWindowSize;
            QPoint tl = newRect.topLeft();
            tl.setX(qBound(avail.left() - newRect.width() + minVis,
                           tl.x(), avail.right() - minVis));
            tl.setY(qBound(avail.top() - newRect.height() + minVis,
                           tl.y(), avail.bottom() - minVis));
            newRect.moveTopLeft(tl);
        }
    }
    setGeometry(newRect);
}

void PetWindow::setEmotion(PetAnimationState emotion)
{
    if (m_live2DView) {
        m_live2DView->setEmotion(emotion);
    }
}

// ---------------------------------------------------------------------------
// 摸摸头动作
// ---------------------------------------------------------------------------
void PetWindow::triggerPet()
{
    qInfo() << "[PetWindow] triggerPet 触发";

    const QDate today = QDate::currentDate();
    if (today != m_dailyResetDate) {
        m_dailyResetDate = today;
        m_greetMorningDone = false;
        m_greetEveningDone = false;
        m_sleepDone = false;
        m_petClickCount = 0;
    }

    m_petClickCount++;

    QString floatText;
    QColor  floatColor;

    if (m_petClickCount <= 10) {
        floatText  = QStringLiteral("\xe4\xb8\xbb\xe4\xba\xba\x7e\x7e\n\xe2\x9d\xa4\xef\xb8\x8f +3");
        floatColor = QColor(255, 200, 200);
        m_affection.add(3);
    } else {
        floatText  = QStringLiteral("\xe4\xb8\xbb\xe4\xba\xba\x7e\x7e");
        floatColor = QColor(180, 180, 180);
    }

    showFloatingText(floatText, floatColor);
    saveState();

    // 🎭 头部点击时随机播放一个预制表情（如果有）
    if (m_live2DView) {
        m_live2DView->playRandomExpression();
    }
}

void PetWindow::onSpeakingStarted()
{
    if (m_live2DView) {
        m_live2DView->onSpeakingStarted();
    }
}

void PetWindow::onSpeakingFinished()
{
    if (m_live2DView) {
        m_live2DView->onSpeakingFinished();
    }
}

void PetWindow::onSpeakingInterrupted()
{
    if (m_live2DView) {
        m_live2DView->onSpeakingInterrupted();
    }
}

void PetWindow::setKeepOnTop(bool keepOnTop)
{
    m_keepOnTop = keepOnTop;
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"))
        .setValue(QStringLiteral("window/keepOnTop"), m_keepOnTop);
    // Z-order 由 App::applyPairedWindowPolicy() 统一管理
}

// ============================================================================
// 全屏检测纯函数 — PetWindow 不维护任何全屏状态，仅提供几何判定
// ============================================================================

#ifdef Q_OS_WIN
bool PetWindow::isOurWindow(HWND hwnd) const
{
    DWORD ourPid = GetCurrentProcessId();
    DWORD winPid = 0;
    GetWindowThreadProcessId(hwnd, &winPid);
    return winPid == ourPid;
}

bool PetWindow::isWindowFullscreen(HWND hwnd) const
{
    // 1. 基本有效性检查
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd))
        return false;

    // 2. 排除 cloaked（被 DWM 隐藏的窗口）
    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;

    // 3. 排除桌宠自身
    if (hwnd == reinterpret_cast<HWND>(effectiveWinId()))
        return false;

    // 4. 排除桌面 / Shell / 任务栏
    if (hwnd == GetShellWindow())
        return false;
    wchar_t className[256] = {};
    if (GetClassNameW(hwnd, className, 256)) {
        if (wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Progman") == 0)
            return false;
    }

    // 5. 获取显示器信息
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(monitor, &mi))
        return false;
    const RECT &monRect = mi.rcMonitor;

    // 6. 客户区 rect → 屏幕坐标（核心判断依据）
    RECT cr = {};
    if (!GetClientRect(hwnd, &cr))
        return false;
    POINT crTL = { cr.left, cr.top };
    POINT crBR = { cr.right, cr.bottom };
    ClientToScreen(hwnd, &crTL);
    ClientToScreen(hwnd, &crBR);
    RECT clientScreenRect = { crTL.x, crTL.y, crBR.x, crBR.y };

    // 7. 窗口矩形
    RECT winRect = {};
    if (!GetWindowRect(hwnd, &winRect))
        return false;

    // 8. DWM 扩展边框
    RECT extBounds = {};
    bool extBoundsValid = SUCCEEDED(DwmGetWindowAttribute(
        hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &extBounds, sizeof(extBounds)));

    // ── 全屏判定 ──

    // (A) 客户区四条边必须接近 monitorRect（容差 6px = ~99% 覆盖率）
    constexpr int CT = 6;
    bool clientCovers =
        (clientScreenRect.left   <= monRect.left   + CT) &&
        (clientScreenRect.top    <= monRect.top    + CT) &&
        (clientScreenRect.right  >= monRect.right  - CT) &&
        (clientScreenRect.bottom >= monRect.bottom - CT);

    if (!clientCovers)
        return false;

    // (B) 外框或扩展边框也覆盖 monitorRect（容差 8px 处理 DWM 阴影偏移）
    constexpr int WT = 8;
    bool winCovers =
        (winRect.left   <= monRect.left   + WT) &&
        (winRect.top    <= monRect.top    + WT) &&
        (winRect.right  >= monRect.right  - WT) &&
        (winRect.bottom >= monRect.bottom - WT);

    bool extCovers = extBoundsValid &&
        (extBounds.left   <= monRect.left   + WT) &&
        (extBounds.top    <= monRect.top    + WT) &&
        (extBounds.right  >= monRect.right  - WT) &&
        (extBounds.bottom >= monRect.bottom - WT);

    if (!winCovers && !extCovers)
        return false;

    return true;
}

bool PetWindow::isNeutralWindow(HWND hwnd)
{
    if (!hwnd) return false;
    // 桌面窗口
    if (hwnd == GetShellWindow())
        return true;
    wchar_t className[256] = {};
    if (!GetClassNameW(hwnd, className, 256))
        return false;
    return wcscmp(className, L"Shell_TrayWnd") == 0 ||
           wcscmp(className, L"Shell_SecondaryTrayWnd") == 0 ||
           wcscmp(className, L"WorkerW") == 0 ||
           wcscmp(className, L"Progman") == 0;
}
#endif // Q_OS_WIN

#if defined(Q_OS_WIN)
bool PetWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCHITTEST) {
        if (msg->hwnd != reinterpret_cast<HWND>(winId())
            && !::IsChild(reinterpret_cast<HWND>(winId()), msg->hwnd)) {
            return false;
        }
        *result = HTTRANSPARENT;
        return true;
    }
    return false;
}
#endif

void PetWindow::setHeadTrackingEnabled(bool enabled)
{
    if (m_live2DView) {
        m_live2DView->setHeadTrackingEnabled(enabled);
    }
}

bool PetWindow::headTrackingEnabled() const
{
    return m_live2DView ? m_live2DView->headTrackingEnabled() : false;
}

// ---------------------------------------------------------------------------
// 背景图
// ---------------------------------------------------------------------------
void PetWindow::setBackgroundImage(const QString &filePath)
{
    m_backgroundImagePath = filePath;
    m_backgroundSourcePath = filePath;
    if (m_live2DView) {
        m_live2DView->setBackgroundImage(filePath);
    }
    qInfo() << "[PetWindow] 已设置背景图：" << filePath;
}

void PetWindow::setBackgroundCropped(const QString &croppedPath, const QString &sourcePath)
{
    m_backgroundImagePath = croppedPath;
    m_backgroundSourcePath = sourcePath;
    if (m_live2DView) {
        m_live2DView->setBackgroundImage(croppedPath);
    }
    qInfo() << "[PetWindow] 已应用裁剪背景，原始图：" << sourcePath;
}

void PetWindow::clearBackground()
{
    m_backgroundImagePath.clear();
    m_backgroundSourcePath.clear();
    if (m_live2DView) {
        m_live2DView->clearBackground();
    }
    qInfo() << "[PetWindow] 已清除背景图";
}

void PetWindow::setBackgroundOpacity(double opacity)
{
    m_backgroundOpacity = qBound(0.0, opacity, 1.0);
    if (m_live2DView) {
        m_live2DView->setBackgroundOpacity(m_backgroundOpacity);
    }
}

void PetWindow::showFloatingText(const QString &text, const QColor &color)
{
    qInfo() << "[PetWindow] showFloatingText:" << text.left(30);

    // 清理上一个浮动文字
    if (m_activeFloatingText) {
        m_activeFloatingText->deleteLater();
        m_activeFloatingText = nullptr;
    }

    // ── 创建自绘制浮动文字（不移动顶层窗口，内部 paint translate + opacity） ──
    auto *widget = new FloatingTextWidget(text, color);

    // ── 一次性计算锚点定位（窗口固定，不跟踪头部动画） ──
    float headCenterX = 0.0f, headTopY = 0.0f, modelPixelHeight = 0.0f;
    float offset = 0.0f;
    if (m_live2DView
        && m_live2DView->getHeadAnchorPoint(headCenterX, headTopY, modelPixelHeight)
        && modelPixelHeight > 0.0f)
    {
        offset = modelPixelHeight * 0.065f;
    } else {
        headCenterX = static_cast<float>(width()) * 0.5f;
        headTopY = 0.0f;
        offset = static_cast<float>(height()) * 0.05f;
    }

    QPoint globalAnchor = mapToGlobal(QPoint(qRound(headCenterX), qRound(headTopY)));
    int x = globalAnchor.x() - widget->width() / 2;
    int y = globalAnchor.y() - qRound(offset) - widget->height();
    widget->move(x, y);

    // ── 显示并启动内部动画 ──
    widget->show();
    widget->raise();       // 确保不被 PetWindow 层级覆盖
    widget->startAnimation();

    // ── 跟踪当前活跃实例 ──
    m_activeFloatingText = widget;
}

// ============================================================================
// 说话气泡
// ============================================================================

void PetWindow::showSpeechBubble(const QString &text)
{
    if (!m_speechBubble) return;

    qInfo() << "[PetWindow] showSpeechBubble:" << text.left(30);

    const QString displayText = text.length() > 80
        ? text.left(80) + QStringLiteral("\xe2\x80\xa6")
        : text;

    // showText 内部已调 show()，此处不再重复调
    m_speechBubble->showText(displayText);
    updateSpeechBubblePosition();
}

void PetWindow::updateSpeechBubblePosition()
{
    if (!m_speechBubble || !m_speechBubble->isVisible()) return;

    float headCenterX = 0.0f, headTopY = 0.0f, /*unused*/ modelPixelHeight = 0.0f;
    if (m_live2DView
        && m_live2DView->getHeadAnchorPoint(headCenterX, headTopY, modelPixelHeight))
    {
        // 基于模型头部区域锚点定位
        static constexpr int kMargin = 10;
        QPoint globalAnchor = mapToGlobal(QPoint(qRound(headCenterX), qRound(headTopY)));
        int x = globalAnchor.x() - m_speechBubble->width() / 2;
        int y = globalAnchor.y() - m_speechBubble->height() - kMargin;
        m_speechBubble->move(x, y);
    } else {
        // 兜底：窗口顶部居中
        QPoint globalTopCenter = mapToGlobal(QPoint(width() / 2, 0));
        int x = globalTopCenter.x() - m_speechBubble->width() / 2;
        int y = globalTopCenter.y() - m_speechBubble->height() - 8;
        m_speechBubble->move(x, y);
    }
}

// ============================================================================
// 空实现 — mask 由 InputProxyWindow 独立负责
// ============================================================================

void PetWindow::updateModelMask() {}
void PetWindow::clearMaskAndCache() {}
void PetWindow::applyMaskRegion(const QRegion &, int) {}
QRegion PetWindow::dilateRegion(const QRegion &region, int) { return region; }
void PetWindow::onMaskTimer() {}
void PetWindow::markMaskDirty() {}

void PetWindow::setSpeechBubbleBgColor(const QColor &color)
{
    if (m_speechBubble && color.isValid())
        m_speechBubble->setBackgroundColor(color);
}

void PetWindow::setSpeechBubbleBorderColor(const QColor &color)
{
    if (m_speechBubble && color.isValid())
        m_speechBubble->setBorderColor(color);
}

void PetWindow::setSpeechBubbleTextColor(const QColor &color)
{
    if (m_speechBubble && color.isValid())
        m_speechBubble->setTextColor(color);
}

// ---------------------------------------------------------------------------
// 持久化存储
// ---------------------------------------------------------------------------
void PetWindow::saveState()
{
    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"));
    settings.setValue(QStringLiteral("affection/value"), m_affection.current());
    settings.setValue(QStringLiteral("feeding/fullness0"), m_feedFullness[0]);
    settings.setValue(QStringLiteral("feeding/fullness1"), m_feedFullness[1]);
    settings.setValue(QStringLiteral("feeding/fullness2"), m_feedFullness[2]);
    settings.setValue(QStringLiteral("feeding/fullness3"), m_feedFullness[3]);
    settings.setValue(QStringLiteral("feeding/periodFed0"), m_periodFed[0]);
    settings.setValue(QStringLiteral("feeding/periodFed1"), m_periodFed[1]);
    settings.setValue(QStringLiteral("feeding/periodFed2"), m_periodFed[2]);
    settings.setValue(QStringLiteral("feeding/periodFed3"), m_periodFed[3]);
    settings.setValue(QStringLiteral("feeding/currentFullness"), m_currentFullness);
    settings.setValue(QStringLiteral("feeding/lastFeedDate"),
                      m_lastFeedDate.toString(Qt::ISODate));

    settings.setValue(QStringLiteral("daily/date"), m_dailyResetDate.toString(Qt::ISODate));
    settings.setValue(QStringLiteral("daily/greetMorning"), m_greetMorningDone);
    settings.setValue(QStringLiteral("daily/greetEvening"), m_greetEveningDone);
    settings.setValue(QStringLiteral("daily/sleepDone"), m_sleepDone);
    settings.setValue(QStringLiteral("daily/petClickCount"), m_petClickCount);

    settings.setValue(QStringLiteral("window/modelScale"), m_currentModelScale);

    // 全屏状态由 App 管理（m_fullscreenDisplayEnabled 在 App::setFullscreenDisplayEnabled 中持久化）

    settings.setValue(QStringLiteral("background/path"), m_backgroundImagePath);
    settings.setValue(QStringLiteral("background/sourcePath"), m_backgroundSourcePath);
    settings.setValue(QStringLiteral("background/opacity"), m_backgroundOpacity);
}

void PetWindow::loadState()
{
    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"));

    // 全屏覆盖设置在 App 中管理，PetWindow 不再读取 fullscreenOverlay/enabled
    m_keepOnTop = settings.value(QStringLiteral("window/keepOnTop"), true).toBool();

    // 恢复用户上次手动调整的缩放值；首次启动 / 配置异常时 fallback 1.0
    {
        const double saved = settings.value(QStringLiteral("window/modelScale"), -1.0).toDouble();
        m_currentModelScale = (saved > 0.0) ? qBound(0.3, saved, kMaxModelScale) : 1.0;
    }

    const int affValue = settings.value(QStringLiteral("affection/value"), 0).toInt();
    m_affection.setValue(affValue);

    m_feedFullness[0] = settings.value(QStringLiteral("feeding/fullness0"), 25).toInt();
    m_feedFullness[1] = settings.value(QStringLiteral("feeding/fullness1"), 25).toInt();
    m_feedFullness[2] = settings.value(QStringLiteral("feeding/fullness2"), 25).toInt();
    m_feedFullness[3] = settings.value(QStringLiteral("feeding/fullness3"), 25).toInt();
    m_periodFed[0] = settings.value(QStringLiteral("feeding/periodFed0"), false).toBool();
    m_periodFed[1] = settings.value(QStringLiteral("feeding/periodFed1"), false).toBool();
    m_periodFed[2] = settings.value(QStringLiteral("feeding/periodFed2"), false).toBool();
    m_periodFed[3] = settings.value(QStringLiteral("feeding/periodFed3"), false).toBool();
    m_currentFullness = settings.value(QStringLiteral("feeding/currentFullness"), 0).toInt();

    const QString feedDateStr = settings.value(QStringLiteral("feeding/lastFeedDate")).toString();
    if (!feedDateStr.isEmpty()) {
        m_lastFeedDate = QDate::fromString(feedDateStr, Qt::ISODate);
    }

    m_backgroundImagePath = settings.value(QStringLiteral("background/path")).toString();
    m_backgroundSourcePath = settings.value(QStringLiteral("background/sourcePath")).toString();
    if (m_backgroundSourcePath.isEmpty()) {
        m_backgroundSourcePath = m_backgroundImagePath;
    }
    m_backgroundOpacity = qBound(0.0,
        settings.value(QStringLiteral("background/opacity"), 0.3).toDouble(), 1.0);

    if (m_live2DView != nullptr) {
        if (!m_backgroundImagePath.isEmpty()) {
            m_live2DView->setBackgroundImage(m_backgroundImagePath);
        }
        m_live2DView->setBackgroundOpacity(m_backgroundOpacity);
    }

    const QString savedDate = settings.value(QStringLiteral("daily/date")).toString();
    const QDate savedDailyDate = QDate::fromString(savedDate, Qt::ISODate);
    if (savedDailyDate == QDate::currentDate()) {
        m_greetMorningDone = settings.value(QStringLiteral("daily/greetMorning"), false).toBool();
        m_greetEveningDone = settings.value(QStringLiteral("daily/greetEvening"), false).toBool();
        m_sleepDone        = settings.value(QStringLiteral("daily/sleepDone"), false).toBool();
        m_petClickCount    = settings.value(QStringLiteral("daily/petClickCount"), 0).toInt();
        m_dailyResetDate = savedDailyDate;
    }
}

// ---------------------------------------------------------------------------
// 右键上下文菜单（由 InputProxyWindow 转发）
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

#if defined(Q_OS_WIN)
    // ★ 菜单弹出时设为 TOPMOST，避免被同样 HWND_TOPMOST 的 InputProxyWindow 遮挡
    menu.setWindowFlags(menu.windowFlags() | Qt::WindowStaysOnTopHint);

    // ★ 跟踪菜单 HWND（在 exec() 事件循环中通过 QTimer 获取真正的弹出窗口 HWND）
    m_activePopupHwnd = nullptr;
    QTimer::singleShot(0, &menu, [this, &menu]() {
        m_activePopupHwnd = reinterpret_cast<HWND>(menu.winId());
        qInfo() << "[PetWindow] contextMenu popupHwnd=" << m_activePopupHwnd;
    });
#endif

    const QDate today = QDate::currentDate();
    if (today != m_dailyResetDate) {
        m_dailyResetDate = today;
        m_greetMorningDone = false;
        m_greetEveningDone = false;
        m_sleepDone = false;
        m_petClickCount = 0;
    }

    const int affValue = m_affection.current();
    const int affMax   = AffectionSystem::maxValue();
    QString affText = QStringLiteral("\xe2\x9d\xa4\xef\xb8\x8f \xe5\xa5\xbd\xe6\x84\x9f\xe5\xba\xa6\xef\xbc\x9a%1/%2").arg(affValue).arg(affMax);
    QString heartBar;
    if (affValue <= 0) {
        heartBar = QStringLiteral("\xf0\x9f\x92\x94");
    } else if (affValue < 50) {
        heartBar = QStringLiteral("\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d");
    } else if (affValue < 100) {
        heartBar = QStringLiteral("\xe2\x9d\xa4\xef\xb8\x8f\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d");
    } else if (affValue < 150) {
        heartBar = QStringLiteral("\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d");
    } else if (affValue < 200) {
        heartBar = QStringLiteral("\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xf0\x9f\xa4\x8d\xf0\x9f\xa4\x8d");
    } else if (affValue < 250) {
        heartBar = QStringLiteral("\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xf0\x9f\xa4\x8d");
    } else {
        heartBar = QStringLiteral("\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f\xe2\x9d\xa4\xef\xb8\x8f");
    }
    auto *affAction = menu.addAction(affText);
    affAction->setEnabled(false);
    auto *heartAction = menu.addAction(heartBar);
    heartAction->setEnabled(false);

    menu.addSeparator();

    const int hour = QTime::currentTime().hour();
    const double minute = QTime::currentTime().minute();
    const double timeDec = hour + minute / 60.0;

    bool greetAvail = false;
    bool isMorning  = false;
    if (timeDec >= 6.0 && timeDec <= 10.5) {
        greetAvail = true;
        isMorning  = true;
    } else if (hour >= 19) {
        greetAvail = true;
        isMorning  = false;
    }

    QAction *greetAction = nullptr;
    bool greetLocked = false;
    if (greetAvail) {
        greetLocked = isMorning ? m_greetMorningDone : m_greetEveningDone;
        const QString greetText = isMorning
            ? QStringLiteral("\xf0\x9f\x8c\x85 \xe6\x97\xa9\xe4\xb8\x8a\xe5\xa5\xbd")
            : QStringLiteral("\xf0\x9f\x8c\x87 \xe6\x99\x9a\xe4\xb8\x8a\xe5\xa5\xbd");
        greetAction = menu.addAction(greetText);
        if (greetLocked) greetAction->setEnabled(false);
    }

    auto *petAction = menu.addAction(QStringLiteral("\xf0\x9f\xa4\x97 \xe6\x91\xb8\xe6\x91\xb8\xe5\xa4\xb4"));

    resetFeedCycleIfNeeded();
    const FeedPeriod period = currentFeedPeriod();
    const bool alreadyFull = (m_currentFullness >= 100);
    const bool periodAlreadyFed = (period != Invalid && m_periodFed[static_cast<int>(period)]);

    QMenu *feedMenu = menu.addMenu(QStringLiteral("\xf0\x9f\x8d\xbd \xe5\x96\x82\xe9\xa3\x9f"));

    QAction *fishAction    = nullptr;
    QAction *salmonAction  = nullptr;
    QAction *canAction     = nullptr;
    QAction *chickenAction = nullptr;

    if (alreadyFull) {
        auto *hint = feedMenu->addAction(QStringLiteral("\xf0\x9f\x98\x8a \xe4\xb8\xbb\xe4\xba\xba\xef\xbc\x8cneko\xe5\xb7\xb2\xe7\xbb\x8f\xe9\xa5\xb1\xe4\xba\x86"));
        hint->setEnabled(false);
    } else if (periodAlreadyFed) {
        auto *hint = feedMenu->addAction(QStringLiteral("\xf0\x9f\x98\x8a \xe8\xbf\x99\xe4\xb8\xaa\xe6\x97\xb6\xe6\xae\xb5\xe5\xb7\xb2\xe7\xbb\x8f\xe5\x96\x82\xe8\xbf\x87\xe5\x95\xa6~"));
        hint->setEnabled(false);
    } else {
        fishAction    = feedMenu->addAction(QStringLiteral("\xf0\x9f\x90\x9f \xe5\xb0\x8f\xe9\xb1\xbc\xe5\xb9\xb2"));
        salmonAction  = feedMenu->addAction(QStringLiteral("\xf0\x9f\x8d\xa3 \xe4\xb8\x89\xe6\x96\x87\xe9\xb1\xbc"));
        canAction     = feedMenu->addAction(QStringLiteral("\xf0\x9f\xa5\xab \xe7\x8c\xab\xe7\xbd\x90\xe5\xa4\xb4"));
        chickenAction = feedMenu->addAction(QStringLiteral("\xf0\x9f\x8d\x97 \xe9\xb8\xa1\xe8\x83\xb8\xe8\x82\x89"));
    }

    feedMenu->addSeparator();
    auto *fullnessAction = feedMenu->addAction(
        QStringLiteral("\xf0\x9f\x93\x8a \xe9\xa5\xb1\xe8\x85\xb9\xe6\x84\x9f\xef\xbc\x9a%1/100").arg(m_currentFullness));
    fullnessAction->setEnabled(false);

    const bool canSleep = (hour >= 22 && !m_sleepDone);
    auto *sleepAction = menu.addAction(QStringLiteral("\xf0\x9f\x92\xa4 \xe6\x99\x9a\xe5\xae\x89"));
    if (m_sleepDone) sleepAction->setEnabled(false);

    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen) return;

    QString floatText;
    QColor  floatColor;

    if (chosen == greetAction) {
        if (greetLocked) return;
        if (isMorning) m_greetMorningDone = true;
        else           m_greetEveningDone = true;
        floatText  = isMorning
            ? QStringLiteral("\xe4\xb8\xbb\xe4\xba\xba\xef\xbc\x8c\xe6\x97\xa9\xe4\xb8\x8a\xe5\xa5\xbd\xe5\x96\xb5\n\xe2\x9d\xa4\xef\xb8\x8f +1")
            : QStringLiteral("\xe4\xb8\xbb\xe4\xba\xba\xef\xbc\x8c\xe6\x99\x9a\xe4\xb8\x8a\xe5\xa5\xbd\xe5\x96\xb5\n\xe2\x9d\xa4\xef\xb8\x8f +1");
        floatColor = QColor(180, 230, 255);
        m_affection.add(1);

    } else if (chosen == petAction) {
        triggerPet();
        saveState();
        return;

    } else if (chosen == fishAction) {
        m_currentFullness = qMin(100, m_currentFullness + m_feedFullness[0]);
        if (period != Invalid) m_periodFed[static_cast<int>(period)] = true;
        floatText  = QStringLiteral("\xf0\x9f\x90\x9f \xe5\x96\x82\xe9\xa3\x9f\xe5\xb0\x8f\xe9\xb1\xbc\xe5\xb9\xb2\xe6\x88\x90\xe5\x8a\x9f\xef\xbc\x81\n\xe2\x9d\xa4\xef\xb8\x8f +2");
        floatColor = QColor(255, 220, 150);
        m_affection.add(2);

    } else if (chosen == salmonAction) {
        m_currentFullness = qMin(100, m_currentFullness + m_feedFullness[1]);
        if (period != Invalid) m_periodFed[static_cast<int>(period)] = true;
        floatText  = QStringLiteral("\xf0\x9f\x8d\xa3 \xe5\x96\x82\xe9\xa3\x9f\xe4\xb8\x89\xe6\x96\x87\xe9\xb1\xbc\xe6\x88\x90\xe5\x8a\x9f\xef\xbc\x81\n\xe2\x9d\xa4\xef\xb8\x8f +2");
        floatColor = QColor(255, 180, 150);
        m_affection.add(2);

    } else if (chosen == canAction) {
        m_currentFullness = qMin(100, m_currentFullness + m_feedFullness[2]);
        if (period != Invalid) m_periodFed[static_cast<int>(period)] = true;
        floatText  = QStringLiteral("\xf0\x9f\xa5\xab \xe5\x96\x82\xe9\xa3\x9f\xe7\x8c\xab\xe7\xbd\x90\xe5\xa4\xb4\xe6\x88\x90\xe5\x8a\x9f\xef\xbc\x81\n\xe2\x9d\xa4\xef\xb8\x8f +2");
        floatColor = QColor(200, 220, 255);
        m_affection.add(2);
        m_petClickCount = 0;

    } else if (chosen == chickenAction) {
        m_currentFullness = qMin(100, m_currentFullness + m_feedFullness[3]);
        if (period != Invalid) m_periodFed[static_cast<int>(period)] = true;
        floatText  = QStringLiteral("\xf0\x9f\x8d\x97 \xe5\x96\x82\xe9\xa3\x9f\xe9\xb8\xa1\xe8\x83\xb8\xe8\x82\x89\xe6\x88\x90\xe5\x8a\x9f\xef\xbc\x81\n\xe2\x9d\xa4\xef\xb8\x8f +2");
        floatColor = QColor(255, 230, 180);
        m_affection.add(2);

    } else if (chosen == sleepAction) {
        if (hour < 22) {
            floatText  = QStringLiteral("\xe4\xb8\xbb\xe4\xba\xba\xef\xbc\x8c\xe7\x8e\xb0\xe5\x9c\xa8\xe8\xbf\x98\xe4\xb8\x8d\xe6\x98\xaf\xe6\x99\x9a\xe4\xb8\x8a\xe5\x96\xb5");
            floatColor = QColor(200, 200, 200);
        } else {
            m_sleepDone = true;
            floatText  = QStringLiteral("\xe4\xb8\xbb\xe4\xba\xba\xef\xbc\x8c\xe6\x99\x9a\xe5\xae\x89\xe5\x96\xb5\n\xe2\x9d\xa4\xef\xb8\x8f +1");
            floatColor = QColor(180, 180, 255);
            m_affection.add(1);
        }
    }

    if (!floatText.isEmpty()) {
        showFloatingText(floatText, floatColor);
    }

    saveState();

#if defined(Q_OS_WIN)
    // 菜单已关闭，清除跟踪的 HWND
    m_activePopupHwnd = nullptr;
#endif
}

// ---------------------------------------------------------------------------
// 喂食系统
// ---------------------------------------------------------------------------

PetWindow::FeedPeriod PetWindow::currentFeedPeriod()
{
    const int h = QTime::currentTime().hour();
    if (h >= 5  && h < 11) return Morning;
    if (h >= 11 && h < 14) return Noon;
    if (h >= 14 && h < 18) return Afternoon;
    return Evening;
}

void PetWindow::resetFeedCycleIfNeeded()
{
    const QDate today = QDate::currentDate();
    if (today != m_lastFeedDate) {
        m_lastFeedDate = today;
        for (bool &fed : m_periodFed) fed = false;
        m_currentFullness = 0;
        allocateFeedValues();
        qInfo() << "[Feeding] 新的一天，喂食周期已重置";
    }
}

void PetWindow::allocateFeedValues()
{
    int raw[4];
    int rawSum = 0;
    for (int &v : raw) {
        v = QRandomGenerator::global()->bounded(5, 51);
        rawSum += v;
    }

    const double scale = 100.0 / rawSum;
    int assigned = 0;
    for (int i = 0; i < 3; ++i) {
        m_feedFullness[i] = qMax(1, static_cast<int>(raw[i] * scale));
        assigned += m_feedFullness[i];
    }
    m_feedFullness[3] = qMax(1, 100 - assigned);

    int total = 0;
    for (int v : m_feedFullness) total += v;
    int diff = 100 - total;
    if (diff > 0) {
        int idx = 0;
        for (int i = 1; i < 4; ++i)
            if (m_feedFullness[i] < m_feedFullness[idx]) idx = i;
        m_feedFullness[idx] += diff;
    } else if (diff < 0) {
        int idx = 0;
        for (int i = 1; i < 4; ++i)
            if (m_feedFullness[i] > m_feedFullness[idx]) idx = i;
        m_feedFullness[idx] += diff;
    }

    qInfo() << "[Feeding] 饱腹感值分配：" << m_feedFullness[0] << m_feedFullness[1] << m_feedFullness[2] << m_feedFullness[3];
}

// ---------------------------------------------------------------------------
// moveEvent / resizeEvent / paintEvent
// ---------------------------------------------------------------------------
void PetWindow::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    updateSpeechBubblePosition();
}

void PetWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_live2DView) {
        m_live2DView->setGeometry(rect());
    }
}

void PetWindow::paintEvent(QPaintEvent *event)
{
    // 透明清屏
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    QWidget::paintEvent(event);
}
