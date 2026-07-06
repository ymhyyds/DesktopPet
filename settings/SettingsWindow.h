#pragma once

#include <QWidget>
#include <QCloseEvent>
#include <QColor>

class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QSlider;
class QLabel;
class QPushButton;
class QSpinBox;
class Live2DView;

/**
 * @brief SettingsWindow — 集中管理桌宠的各项设置。
 */
class SettingsWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWindow(QWidget *parent = nullptr);

    void setApiKeyConfigured(bool configured);
    void setSnapThresholdPx(int px);
    void setIdleTimeoutMinutes(int minutes);
    void setModelScale(double scale);
    void setHeadTrackingEnabled(bool enabled);
    void setTtsEnabled(bool enabled);
    void setKeepOnTop(bool keepOnTop);
    void setChatBackgroundImagePath(const QString &path);
    void setChatBackgroundOpacity(double opacity);
    void setChatCropButtonEnabled(bool enabled);
    void setScreenshotPath(const QString &path);
    void setAvailableVoices(const QStringList &voices, const QString &currentVoice);
    void setTtsBackend(int backend);
    void setVoiceVoxUrl(const QString &url);
    void setVoiceVoxLoading(bool loading);

    /// Edge TTS 设置
    void setEdgeExecutablePath(const QString &path);
    void setEdgeVoice(const QString &voice);
    void setEdgeRatePercent(int percent);
    void setEdgeVolumePercent(int percent);
    void setEdgePitchHz(int hz);
    void setEdgeDetectionText(const QString &text);
    void setEdgeTestEnabled(bool enabled);
    void setEdgeTestStatusText(const QString &text);

    /// 🎨 设置说话气泡颜色预览
    void setBubbleBgColor(const QColor &color);
    void setBubbleBorderColor(const QColor &color);
    void setBubbleTextColor(const QColor &color);

    /// 深夜就寝提醒
    void setBedtimeReminderEnabled(bool enabled);
    void setBedtimeMessage(const QString &msg);
    void setBedtimeRandomEnabled(bool enabled);

public slots:
    void showAndRaise();

signals:
    void closed();  // 窗口关闭时发射，供 App 清理 auxiliaryHost 等
    void apiKeySubmitted(const QString &apiKey);
    void snapThresholdChanged(int px);
    void idleTimeoutMinutesChanged(int minutes);
    void modelScaleChanged(double scale);
    void headTrackingToggled(bool enabled);
    void ttsToggled(bool enabled);
    void ttsVoiceChanged(const QString &voiceName);
    void ttsBackendChanged(int backend);
    void voiceVoxUrlChanged(const QString &url);
    void keepOnTopToggled(bool keepOnTop);
    void chatBackgroundImageSelected(const QString &filePath);
    void chatBackgroundOpacityChanged(double opacity);
    void chatBackgroundClearRequested();
    void chatBackgroundCropRequested(const QString &currentPath);
    void screenshotPathChanged(const QString &path);

    /// Edge TTS 保存
    void edgeExecutablePathChanged(const QString &path);
    void edgeVoiceChanged(const QString &voiceName);
    void edgeRateChanged(int percent);
    void edgeVolumeChanged(int percent);
    void edgePitchChanged(int hz);
    /// 点击"测试语音"按钮
    void edgeTestSpeechRequested(const QString &text);

    /// 🎨 说话气泡颜色
    void bubbleBgColorChanged(const QColor &color);
    void bubbleBorderColorChanged(const QColor &color);
    void bubbleTextColorChanged(const QColor &color);

    /// 深夜就寝提醒
    void bedtimeReminderToggled(bool enabled);
    void bedtimeMessageChanged(const QString &msg);
    void bedtimeRandomToggled(bool enabled);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onSaveClicked();
    void onBackendChanged(int index);
    void onScreenshotBrowseClicked();
    void onChatBackgroundBrowseClicked();
    void onChatBackgroundClearClicked();
    void onChatBackgroundCropClicked();
    void onBubbleBgColorClicked();
    void onBubbleBorderColorClicked();
    void onBubbleTextColorClicked();
    void onEdgeBrowseClicked();
    void onEdgeTestSpeechClicked();
    void onBedtimeResetClicked();

private:
    void setupUi();
    void updateBackendVisibility();
    void refreshCurrentVoiceList();

private:
    QLineEdit *m_apiKeyEdit = nullptr;
    QLabel *m_apiKeyStatusLabel = nullptr;
    QCheckBox *m_headTrackingCheckBox = nullptr;
    QCheckBox *m_ttsCheckBox = nullptr;
    QCheckBox *m_keepOnTopCheckBox = nullptr;
    QComboBox *m_backendCombo = nullptr;
    QComboBox *m_voiceComboBox = nullptr;
    QLineEdit *m_vvUrlEdit = nullptr;
    QLabel    *m_vvUrlLabel = nullptr;
    QLabel    *m_vvStatusLabel = nullptr;
    QSpinBox *m_snapThresholdSpinBox = nullptr;
    QSpinBox *m_idleTimeoutSpinBox = nullptr;
    QSlider  *m_sizeSlider = nullptr;
    QLabel   *m_sizeLabel  = nullptr;
    // --- 聊天背景 ---
    QLabel     *m_chatBgPathLabel = nullptr;
    QPushButton *m_chatBgBrowseButton = nullptr;
    QPushButton *m_chatBgCropButton = nullptr;
    QPushButton *m_chatBgClearButton = nullptr;
    QSlider    *m_chatBgOpacitySlider = nullptr;
    QLabel     *m_chatBgOpacityLabel = nullptr;

    QPushButton *m_saveButton = nullptr;
    QLineEdit   *m_screenshotPathEdit = nullptr;
    QPushButton *m_screenshotBrowseBtn = nullptr;

    // ──── 🎨 说话气泡颜色 ────
    QPushButton *m_bubbleBgBtn     = nullptr;
    QPushButton *m_bubbleBorderBtn = nullptr;
    QPushButton *m_bubbleTextBtn   = nullptr;
    QColor m_bubbleBgColor     = QColor(255, 255, 255);
    QColor m_bubbleBorderColor = QColor(200, 200, 200);
    QColor m_bubbleTextColor   = QColor(60, 60, 60);

    // ──── Edge TTS ────
    QLineEdit   *m_edgeExecPathEdit  = nullptr;
    QPushButton *m_edgeBrowseBtn     = nullptr;
    QSpinBox    *m_edgeRateSpinBox   = nullptr;
    QSpinBox    *m_edgeVolumeSpinBox = nullptr;
    QSpinBox    *m_edgePitchSpinBox  = nullptr;
    QLabel      *m_edgeDetectionLabel = nullptr;
    QPushButton *m_edgeTestBtn       = nullptr;
    QLabel      *m_edgeTestStatusLabel = nullptr;
    QString      m_edgeVoice = QStringLiteral("zh-CN-XiaoyiNeural");

    // ──── 深夜就寝提醒 ────
    QCheckBox   *m_bedtimeEnableCheckBox = nullptr;
    QCheckBox   *m_bedtimeRandomCheckBox = nullptr;
    QLineEdit   *m_bedtimeEdit          = nullptr;
    QPushButton *m_bedtimeResetBtn      = nullptr;
};
