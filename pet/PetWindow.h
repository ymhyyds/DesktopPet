#pragma once

#include <QWidget>
#include <QAbstractNativeEventFilter>
#include <QPoint>
#include <QString>
#include <QColor>
#include <QDate>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
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

    void setFullscreenOverlayEnabled(bool enabled);
    bool fullscreenOverlayEnabled() const { return m_fullscreenOverlayEnabled; }

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

    // 动画安全边距比率 — 被 PetWindow/ModelController/Live2DView 共享使用
    static constexpr double kAnimMarginRatio = 0.01;

signals:
    void dragFinished();
    void modelSizeLoaded();
    void modelScaleChanged(double scale);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
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
    void triggerPet();

private:
    Live2DView       *m_live2DView       = nullptr;
    SpeechBubble     *m_speechBubble     = nullptr;
    CharacterConfig   m_currentConfig;

    bool m_dragging = false;
    QPoint m_dragStartOffset;

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

    // ──── 全屏游戏检测 ────
    void setupFullscreenDetection();
    bool checkForegroundFullscreen();
    void applyFullscreenOverlay();
    void removeFullscreenOverlay();
    void onFullscreenCheckTimer();

    bool    m_fullscreenOverlayEnabled   = true;
    bool    m_fullscreenGameActive       = false;
    bool    m_fullscreenOverlayApplied   = false;
    int     m_fullscreenConfirmCount     = 0;
    QTimer *m_fullscreenCheckTimer       = nullptr;

    // ──── 智能穿透 mask 管理 ────
    void updateModelMask();
    void markMaskDirty();
    void clearMaskAndCache();
    void applyMaskRegion(const QRegion &region, int drawableCount);
    QRegion dilateRegion(const QRegion &region, int px);
    void onMaskTimer();

    int     m_maskGeneration = 0;
    bool    m_maskDirty      = false;
    QTimer *m_maskTimer      = nullptr;

    // 稳定运动包络（缓存 + 扩张即时 + 收缩延迟）
    QRegion m_cachedMask;           // 最后成功设置的 mask（防重复 setMask）
    QRect   m_cachedMaskBounds;     // 快速比较用
    bool    m_hasCachedMask = false;

    QRegion m_envelopeRegion;       // 扩张包络（当前 ∪ 最近若干帧 dilated）
    QRect   m_envelopeBounds;       // 包络边界框
    int     m_envelopeStableTicks = 0;
    bool    m_maskActive = false;   // true=动作期间，启用 5Hz 轮询

    static constexpr int kMaskDilationPx     = 25;
    static constexpr int kMaskPollIntervalMs = 200;   // 5Hz
    static constexpr int kMaskStableThreshold = 10;    // 200ms × 10 = 2秒
};
