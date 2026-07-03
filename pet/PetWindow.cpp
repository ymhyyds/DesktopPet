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
#include <QTimer>
#include <QDateTime>
#include <QTime>
#include <QDir>
#include <QRandomGenerator>
#include <QDebug>
#include <QSettings>
#include <QApplication>
#include <QAbstractNativeEventFilter>

#include <algorithm>

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
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);

    // 安装应用级原生事件过滤器（仅返回 HTCLIENT，穿透由 SetWindowRgn 独立负责）
    // 只过滤 HWND 是否为 PetWindow 层级，不检查模型区域——mask 以外的区域 Windows 不递送消息
#if defined(Q_OS_WIN)
    qApp->installNativeEventFilter(this);
#endif

    // mask 更新定时器：200ms（5Hz），仅 m_maskActive 或 m_maskDirty 时执行
    // 稳定运动包络：扩张即时（>3px），收缩延迟 2 秒，25px 额外边距
    m_maskTimer = new QTimer(this);
    m_maskTimer->setInterval(kMaskPollIntervalMs);  // 200ms
    connect(m_maskTimer, &QTimer::timeout, this, &PetWindow::onMaskTimer);
    m_maskTimer->start();  // 一直运行，onMaskTimer 中根据 m_maskActive 选择是否执行

    m_live2DView = new Live2DView(this);
    m_live2DView->setGeometry(rect());

    connect(m_live2DView, &Live2DView::modelLoaded, this, [this](double cw, double ch,
                                                                  double tightW, double tightH) {
        m_tightModelWidth  = tightW;
        m_tightModelHeight = tightH;
        m_canvasWidth = cw;
        m_canvasHeight = ch;
        applyTightSize();
        emit modelSizeLoaded();
        // mask: 模型加载 → 立即刷新包络 + 激活轮询
        m_hasCachedMask = false;
        m_maskActive = true;
        updateModelMask();
    });

    // idle 稳定后的包围盒更接近视觉大小，覆盖初始姿态的值
    connect(m_live2DView, &Live2DView::stableBoundsReady, this, [this](double tightW, double tightH) {
        m_tightModelWidth  = tightW;
        m_tightModelHeight = tightH;
        applyTightSize();
        m_hasCachedMask = false;
        m_maskActive = true;
        updateModelMask();
    });

    // 好感度变化 在 contextMenuEvent 中与操作文字合并显示，避免重叠

    // 点击模型头部 → 摸摸头（动画时启动物理跟随 → mask 需刷新）
    connect(m_live2DView, &Live2DView::headClicked, this, [this]() {
        markMaskDirty();
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

    // 全屏游戏检测
    setupFullscreenDetection();
}

PetWindow::~PetWindow()
{
    if (m_fullscreenCheckTimer) {
        m_fullscreenCheckTimer->stop();
    }
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

    m_live2DView->loadModel(modelAbsPath);
    m_live2DView->setModelScale(config.modelScale);
    setWindowTitle(config.name);
}

void PetWindow::setModelScale(double scale)
{
    scale = qBound(0.3, scale, 10.0);
    m_currentModelScale = scale;
    m_live2DView->setModelScale(scale);

    if (m_tightModelWidth > 0.0 && m_tightModelHeight > 0.0) {
        const double oldW = width();
        const double oldH = height();
        applyTightSize();
        const double centerX = x() + oldW / 2.0;
        const double centerY = y() + oldH / 2.0;
        move(static_cast<int>(centerX - width() / 2.0),
             static_cast<int>(centerY - height() / 2.0));
    }

    markMaskDirty();

    saveState();

    emit modelScaleChanged(scale);
}

void PetWindow::applyTightSize()
{
    if (m_tightModelWidth <= 0.0 || m_tightModelHeight <= 0.0) return;

    const double aspect = m_tightModelWidth / m_tightModelHeight;
    const int basePx = qRound(kBaseSize * m_currentModelScale);

    // ★ 不跨模型归一化：每个模型用自己 idle 稳定后的 tight bounds
    //   modelH = basePx，modelW 跟随 tight 宽高比，各模型独立视觉大小
    int modelH = basePx;
    int modelW = qRound(static_cast<double>(modelH) * aspect);

    const int padPixels = (kPaddingRatio > 0.0)
        ? qMax(1, qRound(qMin(static_cast<double>(modelW), static_cast<double>(modelH)) * kPaddingRatio))
        : 0;
    int padAnimW = qRound(modelW * kAnimMarginRatio);
    int padAnimH = qRound(modelH * kAnimMarginRatio);
    int newW = qMax(kMinWindowSize, modelW + qMax(padPixels * 2, padAnimW * 2));
    int newH = qMax(kMinWindowSize, modelH + qMax(padPixels * 2, padAnimH * 2));

    const int oldW = width(), oldH = height();
    resize(newW, newH);
    const int actualW = width(), actualH = height();

    if (m_live2DView) {
        m_live2DView->setGeometry(rect());
    }

    // ★ 记录实际 resize 结果，用于排查宽度卡住
    if (actualW != newW || actualH != newH) {
        QFile f(QStringLiteral("live2d_debug_log.txt"));
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream s(&f);
            s << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
              << " [resize] scale=" << m_currentModelScale
              << " 期望=" << newW << "x" << newH
              << " 实际=" << actualW << "x" << actualH
              << " 旧=" << oldW << "x" << oldH
              << " tight=" << m_tightModelWidth << "x" << m_tightModelHeight
              << " basePx=" << basePx << " aspect=" << QString::number(aspect, 'f', 4)
              << " modelW=" << modelW << " modelH=" << modelH
              << "\n";
        }
    } else {
        QFile f(QStringLiteral("live2d_debug_log.txt"));
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream s(&f);
            s << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
              << " [resize] scale=" << m_currentModelScale
              << " " << actualW << "x" << actualH
              << " basePx=" << basePx << " aspect=" << QString::number(aspect, 'f', 4) << "\n";
        }
    }
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
}

void PetWindow::setKeepOnTop(bool keepOnTop)
{
    m_keepOnTop = keepOnTop;
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(effectiveWinId());
    SetWindowPos(hwnd,
                 keepOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#else
    setWindowFlag(Qt::WindowStaysOnTopHint, keepOnTop);
    show();
#endif
    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"))
        .setValue(QStringLiteral("window/keepOnTop"), m_keepOnTop);

    qInfo() << "[PetWindow] 置顶" << (keepOnTop ? "开启" : "关闭");
}

// ---------------------------------------------------------------------------
// 全屏游戏覆盖：仅在前台窗口真正全屏时强制置顶
// ---------------------------------------------------------------------------

void PetWindow::setFullscreenOverlayEnabled(bool enabled)
{
    m_fullscreenOverlayEnabled = enabled;

    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"))
        .setValue(QStringLiteral("fullscreenOverlay/enabled"), m_fullscreenOverlayEnabled);

    qInfo() << "[FullscreenOverlay] 游戏全屏覆盖" << (enabled ? "启用" : "禁用");

    // 立即响应状态变化
    onFullscreenCheckTimer();
}

void PetWindow::setupFullscreenDetection()
{
    m_fullscreenCheckTimer = new QTimer(this);
    m_fullscreenCheckTimer->setInterval(200);          // 200ms 检测间隔
    connect(m_fullscreenCheckTimer, &QTimer::timeout,
            this, &PetWindow::onFullscreenCheckTimer);
    m_fullscreenCheckTimer->start();
}

void PetWindow::onFullscreenCheckTimer()
{
    // 功能未启用 → 清除覆盖状态
    if (!m_fullscreenOverlayEnabled) {
        if (m_fullscreenOverlayApplied) {
            removeFullscreenOverlay();
        }
        m_fullscreenConfirmCount = 0;
        m_fullscreenGameActive = false;
        return;
    }

    const bool isFs = checkForegroundFullscreen();

    // 仅状态变化时记录日志，避免刷屏
    static bool lastIsFs = false;
    if (isFs != lastIsFs) {
        qInfo() << "[FullscreenOverlay] 检测到全屏状态变化:"
                << (isFs ? "全屏" : "非全屏")
                << "count=" << m_fullscreenConfirmCount
                << "applied=" << m_fullscreenOverlayApplied
                << "active=" << m_fullscreenGameActive;
        lastIsFs = isFs;
    }

    if (isFs) {
        // 防抖：连续确认 3 次（600ms）后才应用
        if (m_fullscreenConfirmCount < 3)
            m_fullscreenConfirmCount++;

        if (m_fullscreenConfirmCount >= 3 && !m_fullscreenOverlayApplied) {
            applyFullscreenOverlay();
        }
    } else {
        // 连续确认不满足后清除
        if (m_fullscreenConfirmCount > 0)
            m_fullscreenConfirmCount--;

        if (m_fullscreenConfirmCount <= 0 && m_fullscreenOverlayApplied) {
            removeFullscreenOverlay();
        }
    }
}

void PetWindow::applyFullscreenOverlay()
{
    if (m_fullscreenOverlayApplied)
        return;

    qInfo() << "[FullscreenOverlay] 全屏游戏检测到 (+"
            << m_fullscreenConfirmCount << "次确认)，应用覆盖 (HWND="
            << Qt::hex << static_cast<quintptr>(effectiveWinId()) << Qt::dec << ")";

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(effectiveWinId());
    // 只使用 SetWindowPos(HWND_TOPMOST) 修改 Z-order，不调用 raise()
    // raise() 会经 requestActivate() 调用 SetForegroundWindow，抢走游戏焦点
    // → 下一帧检测认为游戏不是前台窗口 → 覆盖被撤销 → 桌宠掉回游戏下方
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif

    m_fullscreenOverlayApplied = true;
    m_fullscreenGameActive = true;
}

void PetWindow::removeFullscreenOverlay()
{
    if (!m_fullscreenOverlayApplied)
        return;

    qInfo() << "[FullscreenOverlay] 全屏游戏退出，恢复状态 (HWND="
            << Qt::hex << static_cast<quintptr>(effectiveWinId()) << Qt::dec
            << ", m_keepOnTop=" << m_keepOnTop << ")";

    m_fullscreenOverlayApplied = false;
    m_fullscreenGameActive = false;

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(effectiveWinId());
    // 恢复到用户的基础置顶偏好（不主动隐藏窗口，不抢焦点）
    SetWindowPos(hwnd,
                 m_keepOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
}

bool PetWindow::checkForegroundFullscreen()
{
#ifdef Q_OS_WIN
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || !IsWindowVisible(hwnd) || IsIconic(hwnd))
        return false;

    // 排除桌宠自身窗口
    if (hwnd == reinterpret_cast<HWND>(effectiveWinId()))
        return false;

    // 排除桌面/Shell 窗口
    if (hwnd == GetShellWindow())
        return false;

    wchar_t className[256] = {};
    if (GetClassNameW(hwnd, className, 256)) {
        if (wcscmp(className, L"Shell_TrayWnd") == 0 ||
            wcscmp(className, L"WorkerW") == 0 ||
            wcscmp(className, L"Progman") == 0)
            return false;
    }

    // 获取前台窗口所在显示器
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(MONITORINFO) };
    if (!GetMonitorInfoW(monitor, &mi))
        return false;

    // 使用 rcMonitor（完整显示器区域，含任务栏），不用 rcWork
    const RECT &mon = mi.rcMonitor;

    // 优先 DwmGetWindowAttribute 获取真实窗口边界（含 DWM 扩展框架）
    RECT winRect = {};
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS,
                                       &winRect, sizeof(winRect));
    if (FAILED(hr)) {
        // Fallback 到 GetWindowRect
        if (!GetWindowRect(hwnd, &winRect))
            return false;
    }

    // 容差 3 物理像素（处理 DPI 和边框误差）
    constexpr int tolerance = 3;
    return (winRect.left   <= mon.left   + tolerance &&
            winRect.top    <= mon.top    + tolerance &&
            winRect.right  >= mon.right  - tolerance &&
            winRect.bottom >= mon.bottom - tolerance);
#else
    return false;
#endif
}

#if defined(Q_OS_WIN)
bool PetWindow::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCHITTEST) {
        // 只确认 HWND 属于 PetWindow 层级（不含其他非子窗口）
        if (msg->hwnd != reinterpret_cast<HWND>(winId())
            && !::IsChild(reinterpret_cast<HWND>(winId()), msg->hwnd)) {
            return false;
        }

        // ◾ mask 外 → 由 SetWindowRgn 在 OS 级直接穿透，Windows 内核不递送消息
        // ◾ mask 内 → 统一返回 HTCLIENT（不检查模型区域，不返回 HTTRANSPARENT）
#ifdef QT_NO_DEBUG
        // Release 构建：正常情况下不会看到 mask 外的 WM_NCHITTEST，
        // 但某些 DWM 合成场景可能导致短暂的未屏蔽消息，直接放行。
        // 不检查模型区域，避免与 setMask 的 OS 级穿透产生竞态。
#endif
        *result = HTCLIENT;
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
// 窗口 mask：稳定运动包络
//
// ◾ mask = 当前帧 Drawable 区域 ∪ 历史扩张包络 + 25px 额外边距
// ◾ 扩张（模型超出当前 mask）→ 即时更新 | 收缩（模型变小）→ 延迟 2 秒
// ◾ 动作期间 5Hz 轮询 (200ms)，超过 3px 变化才更新
// ◾ 缓存上一帧 QRegion，未变化时禁止重复调用 setMask（避免 DWM 重排）
// ============================================================================

void PetWindow::updateModelMask()
{
    if (!m_live2DView) { clearMaskAndCache(); return; }

    // ── 1. 获取当前帧所有可见 Drawable 包围盒 ──
    QElapsedTimer tTotal; tTotal.start();

    QVector<QRect> drawableRects;
    m_live2DView->getVisibleDrawableRects(drawableRects);

    const qint64 tDrawUs = tTotal.nsecsElapsed() / 1000;

    if (drawableRects.isEmpty()) { clearMaskAndCache(); return; }

    // ── 2. 构建紧贴 QRegion + 25px 扩张 ──
    QRegion tightRegion;
    constexpr int kDrawablePad = 2;
    for (const QRect &r : drawableRects) {
        tightRegion = tightRegion.united(
            QRegion(r.adjusted(-kDrawablePad, -kDrawablePad,
                               +kDrawablePad, +kDrawablePad)));
    }
    tightRegion = tightRegion.intersected(QRect(0, 0, width(), height()));
    if (tightRegion.isEmpty()) { clearMaskAndCache(); return; }

    QRegion frameRegion = dilateRegion(tightRegion, kMaskDilationPx);
    frameRegion = frameRegion.intersected(QRect(0, 0, width(), height()));
    if (frameRegion.isEmpty()) { clearMaskAndCache(); return; }

    QRect frameBounds = frameRegion.boundingRect();
    const qint64 tBuildUs = tTotal.nsecsElapsed() / 1000 - tDrawUs;

    // ── 3. 包络管理 ──
    if (!m_hasCachedMask) {
        // 首次 / 重置 → 直接用帧区域
        applyMaskRegion(frameRegion, drawableRects.size());
        m_envelopeRegion = frameRegion;
        m_envelopeBounds = frameBounds;
        m_envelopeStableTicks = 0;
        m_maskActive = true;

        qInfo().nospace()
            << "[MaskTiming] first tDraw=" << tDrawUs << "us"
            << " tBuild=" << tBuildUs << "us"
            << " tSet=<first>"
            << " drawable=" << drawableRects.size();
        return;
    }

    // ── 3a. 扩张检测：新区域是否超出包络 >3px ──
    const bool needsExpansion =
        (frameBounds.left()   < m_envelopeBounds.left()   - 3) ||
        (frameBounds.top()    < m_envelopeBounds.top()    - 3) ||
        (frameBounds.right()  > m_envelopeBounds.right()  + 3) ||
        (frameBounds.bottom() > m_envelopeBounds.bottom() + 3);

    if (needsExpansion) {
        // 合并到包络 → 即时更新 mask
        QElapsedTimer tUnion; tUnion.start();
        m_envelopeRegion = m_envelopeRegion.united(frameRegion);
        m_envelopeBounds = m_envelopeRegion.boundingRect();
        m_envelopeStableTicks = 0;
        m_maskActive = true;
        const qint64 tUnionUs = tUnion.nsecsElapsed() / 1000;

        QElapsedTimer tSet; tSet.start();
        applyMaskRegion(m_envelopeRegion, drawableRects.size());
        const qint64 tSetUs = tSet.nsecsElapsed() / 1000;

        qInfo().nospace()
            << "[MaskTiming] expand tDraw=" << tDrawUs << "us"
            << " tBuild=" << tBuildUs << "us"
            << " tUnion=" << tUnionUs << "us"
            << " tSet=" << tSetUs << "us"
            << " env-rects=" << m_envelopeRegion.rectCount()
            << " drawable=" << drawableRects.size();
        return;
    }

    // ── 3b. 收缩检测：新区域是否明显小于包络 ──
    const bool isSmaller =
        (frameBounds.left()   > m_envelopeBounds.left()   + 3) ||
        (frameBounds.top()    > m_envelopeBounds.top()    + 3) ||
        (frameBounds.right()  < m_envelopeBounds.right()  - 3) ||
        (frameBounds.bottom() < m_envelopeBounds.bottom() - 3);

    if (isSmaller) {
        m_envelopeStableTicks++;
        if (m_envelopeStableTicks >= kMaskStableThreshold) {
            // 稳定 2 秒 → 收缩到当前帧区域
            QElapsedTimer tSet; tSet.start();
            m_envelopeRegion = frameRegion;
            m_envelopeBounds = frameBounds;
            m_envelopeStableTicks = 0;
            applyMaskRegion(frameRegion, drawableRects.size());
            const qint64 tSetUs = tSet.nsecsElapsed() / 1000;

            qInfo().nospace()
                << "[MaskTiming] contract tDraw=" << tDrawUs << "us"
                << " tBuild=" << tBuildUs << "us"
                << " tSet=" << tSetUs << "us"
                << " drawable=" << drawableRects.size();
        }
        // 收缩等待期不更新 mask
        return;
    }

    // ── 3c. 无显著变化 ──
    m_envelopeStableTicks++;
    if (m_envelopeStableTicks >= kMaskStableThreshold * 2) {
        // 4 秒完全稳定 → 停止轮询
        m_maskActive = false;
    }
    // 不更新 mask
}

void PetWindow::clearMaskAndCache()
{
    clearMask();
    m_cachedMask = QRegion();
    m_cachedMaskBounds = QRect();
    m_hasCachedMask = false;
}

void PetWindow::applyMaskRegion(const QRegion &region, int drawableCount)
{
    if (region.isEmpty()) { clearMaskAndCache(); return; }

    // 防重复：区域完全相同时跳过（SetWindowRgn 代价高）
    if (m_hasCachedMask && m_cachedMask == region) {
        return;
    }

    if (region == QRegion(QRect(0, 0, width(), height()))) {
        clearMask();
        m_cachedMask = region;
        m_cachedMaskBounds = region.boundingRect();
        m_hasCachedMask = true;
        return;
    }

    setMask(region);

    m_maskGeneration++;
    m_cachedMask = region;
    m_cachedMaskBounds = region.boundingRect();
    m_hasCachedMask = true;

#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(effectiveWinId());
    qInfo().nospace()
        << "[Mask] v" << m_maskGeneration
        << " HWND=" << Qt::hex << reinterpret_cast<quintptr>(hwnd) << Qt::dec
        << " rects=" << region.rectCount()
        << " bounds=" << m_cachedMaskBounds.x() << ","
        << m_cachedMaskBounds.y() << " "
        << m_cachedMaskBounds.width() << "x"
        << m_cachedMaskBounds.height();
#endif
    // ⚠ setMask 内部调 SetWindowRgn → HRGN 所有权移交给 Windows，
    //   禁止后续 DeleteObject(HRGN)
}

QRegion PetWindow::dilateRegion(const QRegion &region, int px)
{
    if (region.isEmpty()) return region;
    QRegion result;
    for (const QRect &r : region) {
        result = result.united(QRegion(r.adjusted(-px, -px, px, px)));
    }
    return result;
}

void PetWindow::onMaskTimer()
{
    // m_maskActive=false 且无强制 dirty 标记 → 跳过
    if (!m_maskActive && !m_maskDirty) return;
    m_maskDirty = false;
    updateModelMask();
}

void PetWindow::markMaskDirty()
{
    m_maskDirty = true;
    m_maskActive = true;
}

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

    settings.setValue(QStringLiteral("fullscreenOverlay/enabled"), m_fullscreenOverlayEnabled);

    settings.setValue(QStringLiteral("background/path"), m_backgroundImagePath);
    settings.setValue(QStringLiteral("background/sourcePath"), m_backgroundSourcePath);
    settings.setValue(QStringLiteral("background/opacity"), m_backgroundOpacity);
}

void PetWindow::loadState()
{
    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("PetWindow"));

    // 全屏覆盖设置：从新键读取，兼容迁移旧的 window/keepOnTop
    m_fullscreenOverlayEnabled = settings.value(
        QStringLiteral("fullscreenOverlay/enabled"),
        settings.value(QStringLiteral("window/keepOnTop"), true).toBool()
    ).toBool();

    m_keepOnTop = settings.value(QStringLiteral("window/keepOnTop"), true).toBool();

    // 恢复用户上次手动调整的缩放值；首次启动 / 配置异常时 fallback 1.0
    {
        const double saved = settings.value(QStringLiteral("window/modelScale"), -1.0).toDouble();
        m_currentModelScale = (saved > 0.0) ? qBound(0.3, saved, 10.0) : 1.0;
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
// 鼠标事件
// ---------------------------------------------------------------------------
void PetWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragStartOffset = event->pos();
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton)) {
        const QPoint newTopLeft = event->globalPosition().toPoint() - m_dragStartOffset;
        move(newTopLeft);
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        emit dragFinished();
    }
    QWidget::mouseReleaseEvent(event);
}

// ---------------------------------------------------------------------------
// 右键上下文菜单
// ---------------------------------------------------------------------------
void PetWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);

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
    m_hasCachedMask = false;
    m_maskActive = true;
    updateModelMask();
    markMaskDirty();
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
