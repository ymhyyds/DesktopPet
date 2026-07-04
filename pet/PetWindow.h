#pragma once

#include <QWidget>
#include <QAbstractNativeEventFilter>
#include <QPoint>
#include <QString>
#include <QColor>
#include <QDate>
#include <QPointer>
#include <memory>

#include "CharacterLoader.h"
#include "PetAnimationState.h"
#include "AffectionSystem.h"

class Live2DView;
class SpeechBubble;

class PetWindow : public QWidget, protected QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit PetWindow(const CharacterConfig &initialConfig, QWidget *parent = nullptr);
    ~PetWindow() override;

    void applyCharacter(const CharacterConfig &config);

    void setModelScale(double scale);
    double modelScale() const { return m_currentModelScale; }

    void setEmotion(PetAnimationState emotion);

    void setKeepOnTop(bool keepOnTop);
    bool keepOnTop() const { return m_keepOnTop; }

    // ── 全屏检测纯函数（供 App 层调用，PetWindow 不参与任何状态管理） ──
#ifdef Q_OS_WIN
    bool isWindowFullscreen(HWND hwnd) const;
    bool isOurWindow(HWND hwnd) const;
    /// 判断 HWND 是否属于不应影响外部全屏状态的中立系统窗口
    /// （Shell_TrayWnd、Shell_SecondaryTrayWnd、Progman、WorkerW、桌面窗口等）
    static bool isNeutralWindow(HWND hwnd);
#endif

    void setHeadTrackingEnabled(bool enabled);
    bool headTrackingEnabled() const;

    AffectionSystem *affectionSystem() { return &m_affection; }
    Live2DView *live2DView() const { return m_live2DView; }

    void showFloatingText(const QString &text, const QColor &color = QColor(255, 255, 255));

    void showSpeechBubble(const QString &text);

    void setSpeechBubbleBgColor(const QColor &color);
    void setSpeechBubbleBorderColor(const QColor &color);
    void setSpeechBubbleTextColor(const QColor &color);

    void setBackgroundImage(const QString &filePath);
    void setBackgroundCropped(const QString &croppedPath, const QString &sourcePath);
    void clearBackground();
    void setBackgroundOpacity(double opacity);
    QString backgroundImagePath() const { return m_backgroundImagePath; }
    QString backgroundSourcePath() const { return m_backgroundSourcePath; }
    double backgroundOpacity() const { return m_backgroundOpacity; }

    void saveState();
    void loadState();

    /// 摸摸头动作 — InputProxyWindow 调用
    void triggerPet();

    // 动画安全边距比率 — 被 PetWindow/ModelController/Live2DView 共享使用
    static constexpr double kAnimMarginRatio = 0.01;

signals:
    void modelSizeLoaded();
    void modelScaleChanged(double scale);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

#if defined(Q_OS_WIN)
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private:
    void applyTightSize();
    void updateSpeechBubblePosition();

    enum FeedPeriod { Morning, Noon, Afternoon, Evening, Invalid };

    static FeedPeriod currentFeedPeriod();
    void allocateFeedValues();
    void resetFeedCycleIfNeeded();

private:
    Live2DView       *m_live2DView       = nullptr;
    SpeechBubble     *m_speechBubble     = nullptr;
    CharacterConfig   m_currentConfig;

    double m_tightModelWidth  = 0.0;
    double m_tightModelHeight = 0.0;
    double m_canvasWidth = 0.0;
    double m_canvasHeight = 0.0;
    double m_currentModelScale = 1.0;

    bool m_keepOnTop = true;

    AffectionSystem m_affection;

    int     m_feedFullness[4]  = {25, 25, 25, 25};
    bool    m_periodFed[4]     = {false, false, false, false};
    int     m_currentFullness  = 0;
    QDate   m_lastFeedDate;

    QString m_backgroundImagePath;
    QString m_backgroundSourcePath;
    double  m_backgroundOpacity = 0.3;

    static constexpr double kPaddingRatio = 0.0;
    static constexpr int    kBaseSize    = 200;
    static constexpr int    kMinWindowSize = 20;

    QDate m_dailyResetDate;
    bool  m_greetMorningDone = false;
    bool  m_greetEveningDone = false;
    bool  m_sleepDone        = false;
    int   m_petClickCount    = 0;

    // 当前浮动文字（FloatingTextWidget，自绘制无窗口移动）
    QPointer<QWidget> m_activeFloatingText;

    // ╔══════════════════════════════════════════════════════════════════════╗
    // ║  Phase 1 验证：所有 mask 方法已改为空实现                         ║
    // ║  setMask/SetWindowRgn 导致：切换模型闪烁 + 持续运动裁剪           ║
    // ║  移除后验证闪烁和裁剪是否消失，后续 InputProxyWindow 替代         ║
    // ╚══════════════════════════════════════════════════════════════════════╝
    void updateModelMask();
    void markMaskDirty();
    void clearMaskAndCache();
    void applyMaskRegion(const QRegion &region, int drawableCount);
    QRegion dilateRegion(const QRegion &region, int px);
    void onMaskTimer();
};
