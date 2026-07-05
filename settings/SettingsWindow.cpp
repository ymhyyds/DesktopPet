#include "SettingsWindow.h"

#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QColorDialog>
#include <QScrollArea>
#include <QFileDialog>

SettingsWindow::SettingsWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint
                   | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);
    setupUi();
}

void SettingsWindow::setupUi()
{
    setWindowTitle(QStringLiteral("设置"));
    resize(420, 600);

    auto *mainLayout = new QVBoxLayout(this);

    // --- API Key ---
    auto *apiGroup = new QGroupBox(QStringLiteral("DeepSeek API Key"), this);
    auto *apiLayout = new QVBoxLayout(apiGroup);
    m_apiKeyStatusLabel = new QLabel(this);
    m_apiKeyStatusLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setPlaceholderText(QStringLiteral("留空则不修改已保存的 Key"));
    apiLayout->addWidget(m_apiKeyStatusLabel);
    apiLayout->addWidget(m_apiKeyEdit);
    apiGroup->setLayout(apiLayout);

    // --- 模型外观 ---
    auto *appearanceGroup = new QGroupBox(QStringLiteral("模型外观"), this);
    auto *appearanceLayout = new QVBoxLayout(appearanceGroup);
    auto *sizeRow = new QHBoxLayout();
    m_sizeSlider = new QSlider(Qt::Horizontal, this);
    m_sizeSlider->setRange(30, 1000);
    m_sizeSlider->setValue(100);
    m_sizeLabel = new QLabel(QStringLiteral("100%"), this);
    m_sizeLabel->setFixedWidth(44);
    m_sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sizeRow->addWidget(m_sizeSlider);
    sizeRow->addWidget(m_sizeLabel);
    appearanceLayout->addLayout(sizeRow);
    appearanceGroup->setLayout(appearanceLayout);

    connect(m_sizeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sizeLabel->setText(QStringLiteral("%1%").arg(value));
    });

    // --- 交互设置（含 TTS） ---
    auto *interactionGroup = new QGroupBox(QStringLiteral("交互设置"), this);
    auto *formLayout = new QFormLayout(interactionGroup);

    m_headTrackingCheckBox = new QCheckBox(QStringLiteral("头部跟随鼠标（关闭眼动追踪）"), this);
    m_ttsCheckBox = new QCheckBox(QStringLiteral("开启语音朗读"), this);
    m_ttsCheckBox->setChecked(true);

    m_backendCombo = new QComboBox(this);
    m_backendCombo->addItem(QStringLiteral("Edge TTS"), 0);
    m_backendCombo->addItem(QStringLiteral("VoiceVox（二次元语音）"), 1);

    auto *voiceRow = new QHBoxLayout();
    m_voiceComboBox = new QComboBox(this);
    m_voiceComboBox->setMinimumWidth(200);
    voiceRow->addWidget(m_voiceComboBox);
    voiceRow->addStretch();

    // ── Edge TTS 特有设置（后端=Edge 时可见） ──
    m_edgeExecPathEdit = new QLineEdit(this);
    m_edgeExecPathEdit->setPlaceholderText(QStringLiteral("留空则自动检测"));
    m_edgeBrowseBtn = new QPushButton(QStringLiteral("浏览..."), this);
    auto *edgePathRow = new QHBoxLayout();
    edgePathRow->addWidget(m_edgeExecPathEdit, 1);
    edgePathRow->addWidget(m_edgeBrowseBtn);

    m_edgeDetectionLabel = new QLabel(this);
    m_edgeDetectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    m_edgeDetectionLabel->setWordWrap(true);

    m_edgeRateSpinBox = new QSpinBox(this);
    m_edgeRateSpinBox->setRange(-100, 100);
    m_edgeRateSpinBox->setValue(5);
    m_edgeRateSpinBox->setSuffix(QStringLiteral("%"));

    m_edgeVolumeSpinBox = new QSpinBox(this);
    m_edgeVolumeSpinBox->setRange(-100, 100);
    m_edgeVolumeSpinBox->setValue(0);
    m_edgeVolumeSpinBox->setSuffix(QStringLiteral("%"));

    m_edgePitchSpinBox = new QSpinBox(this);
    m_edgePitchSpinBox->setRange(-100, 100);
    m_edgePitchSpinBox->setValue(15);
    m_edgePitchSpinBox->setSuffix(QStringLiteral("Hz"));

    // 测试语音按钮行
    auto *edgeTestRow = new QHBoxLayout();
    m_edgeTestBtn = new QPushButton(QStringLiteral("测试语音"), this);
    m_edgeTestBtn->setEnabled(false);
    m_edgeTestStatusLabel = new QLabel(QStringLiteral("就绪"), this);
    m_edgeTestStatusLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    edgeTestRow->addWidget(m_edgeTestBtn);
    edgeTestRow->addWidget(m_edgeTestStatusLabel);
    edgeTestRow->addStretch();

    connect(m_edgeBrowseBtn, &QPushButton::clicked, this, &SettingsWindow::onEdgeBrowseClicked);
    connect(m_edgeTestBtn, &QPushButton::clicked, this, &SettingsWindow::onEdgeTestSpeechClicked);

    // 插入 Edge TTS 表单项（语音选择行 98 之后，VoiceVox 行之前）
    formLayout->addRow(QStringLiteral("可执行文件路径："), edgePathRow);
    formLayout->addRow(m_edgeDetectionLabel);
    formLayout->addRow(QStringLiteral("语速："), m_edgeRateSpinBox);
    formLayout->addRow(QStringLiteral("音量："), m_edgeVolumeSpinBox);
    formLayout->addRow(QStringLiteral("音高："), m_edgePitchSpinBox);
    formLayout->addRow(edgeTestRow);

    m_vvUrlLabel = new QLabel(QStringLiteral("VoiceVox 地址："), this);
    m_vvUrlEdit = new QLineEdit(this);
    m_vvUrlEdit->setText(QStringLiteral("http://127.0.0.1:50021"));
    m_vvUrlEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:50021"));

    m_vvStatusLabel = new QLabel(this);
    m_vvStatusLabel->setStyleSheet(QStringLiteral("font-size: 11px;"));
    m_vvStatusLabel->setVisible(false);

    formLayout->addRow(m_headTrackingCheckBox);
    formLayout->addRow(m_ttsCheckBox);
    m_keepOnTopCheckBox = new QCheckBox(QStringLiteral("游戏全屏时始终显示桌宠"), this);
    m_keepOnTopCheckBox->setChecked(true);
    formLayout->addRow(m_keepOnTopCheckBox);
    formLayout->addRow(QStringLiteral("语音后端："), m_backendCombo);
    formLayout->addRow(QStringLiteral("语音选择："), voiceRow);
    formLayout->addRow(m_vvUrlLabel, m_vvUrlEdit);
    formLayout->addRow(m_vvStatusLabel);

    formLayout->addRow(QStringLiteral("贴边吸附距离："), m_snapThresholdSpinBox = new QSpinBox(this));
    m_snapThresholdSpinBox->setRange(0, 200);
    m_snapThresholdSpinBox->setSuffix(QStringLiteral(" px"));

    formLayout->addRow(QStringLiteral("挂机主动说话超时："), m_idleTimeoutSpinBox = new QSpinBox(this));
    m_idleTimeoutSpinBox->setRange(1, 180);
    m_idleTimeoutSpinBox->setSuffix(QStringLiteral(" 分钟"));

    auto *screenshotRow = new QHBoxLayout();
    m_screenshotPathEdit = new QLineEdit(this);
    m_screenshotPathEdit->setPlaceholderText(QStringLiteral("（默认可执行文件目录下的 screenshots/）"));
    m_screenshotBrowseBtn = new QPushButton(QStringLiteral("浏览..."), this);
    screenshotRow->addWidget(m_screenshotPathEdit, 1);
    screenshotRow->addWidget(m_screenshotBrowseBtn);
    formLayout->addRow(QStringLiteral("截图保存路径："), screenshotRow);
    connect(m_screenshotBrowseBtn, &QPushButton::clicked, this, &SettingsWindow::onScreenshotBrowseClicked);

    interactionGroup->setLayout(formLayout);

    // --- 聊天背景 ---
    auto *chatBgGroup = new QGroupBox(QStringLiteral("聊天背景"), this);
    auto *chatBgLayout = new QVBoxLayout(chatBgGroup);

    m_chatBgPathLabel = new QLabel(QStringLiteral("（未选择）"), this);
    m_chatBgPathLabel->setMinimumHeight(20);
    m_chatBgPathLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_chatBgPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *chatBgFileRow = new QHBoxLayout();
    m_chatBgBrowseButton = new QPushButton(QStringLiteral("选择图片..."), this);
    m_chatBgCropButton = new QPushButton(QStringLiteral("裁剪"), this);
    m_chatBgCropButton->setEnabled(false);
    m_chatBgClearButton = new QPushButton(QStringLiteral("清除"), this);
    chatBgFileRow->addWidget(m_chatBgBrowseButton);
    chatBgFileRow->addWidget(m_chatBgCropButton);
    chatBgFileRow->addWidget(m_chatBgClearButton);
    chatBgFileRow->addStretch();

    auto *chatBgOpacityRow = new QHBoxLayout();
    m_chatBgOpacitySlider = new QSlider(Qt::Horizontal, this);
    m_chatBgOpacitySlider->setRange(1, 100);
    m_chatBgOpacitySlider->setValue(30);
    m_chatBgOpacityLabel = new QLabel(QStringLiteral("30%"), this);
    m_chatBgOpacityLabel->setFixedWidth(40);
    m_chatBgOpacityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    chatBgOpacityRow->addWidget(new QLabel(QStringLiteral("透明度："), this));
    chatBgOpacityRow->addWidget(m_chatBgOpacitySlider, 1);
    chatBgOpacityRow->addWidget(m_chatBgOpacityLabel);

    chatBgLayout->addWidget(m_chatBgPathLabel);
    chatBgLayout->addLayout(chatBgFileRow);
    chatBgLayout->addLayout(chatBgOpacityRow);
    chatBgGroup->setLayout(chatBgLayout);

    connect(m_chatBgOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_chatBgOpacityLabel->setText(QStringLiteral("%1%").arg(value));
    });
    connect(m_chatBgBrowseButton, &QPushButton::clicked, this, &SettingsWindow::onChatBackgroundBrowseClicked);
    connect(m_chatBgCropButton, &QPushButton::clicked, this, &SettingsWindow::onChatBackgroundCropClicked);
    connect(m_chatBgClearButton, &QPushButton::clicked, this, &SettingsWindow::onChatBackgroundClearClicked);

    // --- 🎨 说话气泡颜色 ---
    auto *bubbleGroup = new QGroupBox(QStringLiteral("说话气泡颜色"), this);
    auto *bubbleOuterLayout = new QVBoxLayout(bubbleGroup);

    auto makeColorBtn = [](const QColor &color, QWidget *parent) -> QPushButton* {
        auto *btn = new QPushButton(parent);
        btn->setFixedSize(40, 28);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; border: 2px solid #999; border-radius: 4px; }"
            "QPushButton:hover { border-color: #333; }"
        ).arg(color.name()));
        return btn;
    };

    m_bubbleBgBtn = makeColorBtn(m_bubbleBgColor, this);
    m_bubbleBorderBtn = makeColorBtn(m_bubbleBorderColor, this);
    m_bubbleTextBtn = makeColorBtn(m_bubbleTextColor, this);

    // 横向并排：色块 + 标签
    auto *bubbleRow = new QHBoxLayout();
    bubbleRow->setSpacing(16);

    auto makeColorColumn = [](QPushButton *btn, const QString &label, QWidget *parent) -> QWidget* {
        auto *col = new QWidget(parent);
        auto *colLayout = new QVBoxLayout(col);
        colLayout->setContentsMargins(0, 0, 0, 0);
        colLayout->setSpacing(4);
        btn->setFixedSize(44, 32);
        auto *lbl = new QLabel(label, parent);
        lbl->setAlignment(Qt::AlignCenter);
        colLayout->addWidget(btn, 0, Qt::AlignCenter);
        colLayout->addWidget(lbl);
        return col;
    };

    bubbleRow->addWidget(makeColorColumn(m_bubbleBgBtn,     QStringLiteral("背景色"), this));
    bubbleRow->addWidget(makeColorColumn(m_bubbleBorderBtn, QStringLiteral("边框色"), this));
    bubbleRow->addWidget(makeColorColumn(m_bubbleTextBtn,   QStringLiteral("文字色"), this));
    bubbleRow->addStretch();

    bubbleOuterLayout->addLayout(bubbleRow);
    bubbleOuterLayout->addStretch();

    connect(m_bubbleBgBtn, &QPushButton::clicked, this, &SettingsWindow::onBubbleBgColorClicked);
    connect(m_bubbleBorderBtn, &QPushButton::clicked, this, &SettingsWindow::onBubbleBorderColorClicked);
    connect(m_bubbleTextBtn, &QPushButton::clicked, this, &SettingsWindow::onBubbleTextColorClicked);

    // --- 保存按钮 ---
    m_saveButton = new QPushButton(QStringLiteral("保存"), this);
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsWindow::onSaveClicked);

    connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsWindow::onBackendChanged);

    // ── 把所有分组放入滚动区域 ──
    auto *scrollContent = new QWidget(this);
    auto *contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(apiGroup);
    contentLayout->addWidget(appearanceGroup);
    contentLayout->addWidget(chatBgGroup);
    contentLayout->addWidget(bubbleGroup);
    contentLayout->addWidget(interactionGroup);
    contentLayout->addStretch();

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(scrollContent);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    mainLayout->addWidget(scrollArea, 1);
    mainLayout->addWidget(m_saveButton);
    setLayout(mainLayout);

    setMinimumSize(320, 300);

    updateBackendVisibility();
}

void SettingsWindow::updateBackendVisibility()
{
    const bool isVv = (m_backendCombo->currentData().toInt() == 1);
    const bool isEdge = !isVv;

    // VoiceVox 控件
    m_vvUrlLabel->setVisible(isVv);
    m_vvUrlEdit->setVisible(isVv);
    m_vvStatusLabel->setVisible(isVv);

    // Edge TTS 控件
    m_edgeExecPathEdit->setVisible(isEdge);
    m_edgeBrowseBtn->setVisible(isEdge);
    m_edgeDetectionLabel->setVisible(isEdge);
    m_edgeRateSpinBox->setVisible(isEdge);
    m_edgeVolumeSpinBox->setVisible(isEdge);
    m_edgePitchSpinBox->setVisible(isEdge);
    m_edgeTestBtn->setVisible(isEdge);
    m_edgeTestStatusLabel->setVisible(isEdge);
}

void SettingsWindow::onBackendChanged(int)
{
    const bool isEdge = (m_backendCombo->currentData().toInt() == 0);
    updateBackendVisibility();
    m_voiceComboBox->clear();
    m_voiceComboBox->setEditable(isEdge);

    if (isEdge) {
        // Edge: editable combo, pre-populate common Chinese voices
        const QStringList commonVoices = {
            QStringLiteral("zh-CN-XiaoxiaoNeural"),
            QStringLiteral("zh-CN-XiaoyiNeural"),
            QStringLiteral("zh-CN-YunjianNeural"),
            QStringLiteral("zh-CN-YunxiNeural"),
            QStringLiteral("zh-CN-YunyangNeural"),
            QStringLiteral("zh-CN-XiaochenNeural"),
            QStringLiteral("zh-CN-XiaohanNeural"),
            QStringLiteral("zh-CN-XiaomengNeural"),
            QStringLiteral("zh-CN-XiaomoNeural"),
            QStringLiteral("zh-CN-XiaoruiNeural"),
            QStringLiteral("zh-CN-XiaoshuangNeural"),
            QStringLiteral("zh-CN-XiaoxuanNeural"),
            QStringLiteral("zh-CN-XiaoyanNeural"),
            QStringLiteral("zh-CN-XiaozhenNeural"),
        };
        m_voiceComboBox->addItems(commonVoices);
        m_voiceComboBox->setCurrentText(m_edgeVoice);
        // 启用测试按钮，恢复就绪状态
        m_edgeTestBtn->setEnabled(true);
        m_edgeTestStatusLabel->setText(QStringLiteral("就绪"));
    } else {
        // VoiceVox: selection combo, show placeholder
        m_voiceComboBox->addItem(QStringLiteral("（切换后端后请保存以加载语音）"), QString());
    }
}

void SettingsWindow::onSaveClicked()
{
    const QString key = m_apiKeyEdit->text().trimmed();
    if (!key.isEmpty()) {
        emit apiKeySubmitted(key);
        m_apiKeyEdit->clear();
        setApiKeyConfigured(true);
    }

    emit headTrackingToggled(m_headTrackingCheckBox->isChecked());
    emit ttsToggled(m_ttsCheckBox->isChecked());
    emit keepOnTopToggled(m_keepOnTopCheckBox->isChecked());

    const int backend = m_backendCombo->currentData().toInt();
    emit ttsBackendChanged(backend);

    if (backend == 0) {
        // Edge TTS 设置
        const QString execPath = m_edgeExecPathEdit->text().trimmed();
        emit edgeExecutablePathChanged(execPath);

        const QString voice = m_voiceComboBox->currentText().trimmed();
        if (!voice.isEmpty()) {
            m_edgeVoice = voice;
            emit edgeVoiceChanged(voice);
        }
        emit edgeRateChanged(m_edgeRateSpinBox->value());
        emit edgeVolumeChanged(m_edgeVolumeSpinBox->value());
        emit edgePitchChanged(m_edgePitchSpinBox->value());
    } else {
        // VoiceVox 设置
        if (m_voiceComboBox->currentIndex() >= 0) {
            const QString voice = m_voiceComboBox->currentData().toString();
            if (!voice.isEmpty()) {
                emit ttsVoiceChanged(voice);
            }
        }
        const QString vvUrl = m_vvUrlEdit->text().trimmed();
        if (!vvUrl.isEmpty()) {
            emit voiceVoxUrlChanged(vvUrl);
        }
    }

    emit snapThresholdChanged(m_snapThresholdSpinBox->value());
    emit idleTimeoutMinutesChanged(m_idleTimeoutSpinBox->value());
    emit modelScaleChanged(m_sizeSlider->value() / 100.0);
    emit chatBackgroundOpacityChanged(m_chatBgOpacitySlider->value() / 100.0);

    const QString screenshotPath = m_screenshotPathEdit->text().trimmed();
    if (!screenshotPath.isEmpty()) {
        emit screenshotPathChanged(screenshotPath);
    }
}

void SettingsWindow::setApiKeyConfigured(bool configured)
{
    m_apiKeyStatusLabel->setText(configured
        ? QStringLiteral("当前状态：已配置 Key")
        : QStringLiteral("当前状态：未配置 Key，AI 聊天功能不可用"));
}

void SettingsWindow::setTtsEnabled(bool enabled)
{
    m_ttsCheckBox->setChecked(enabled);
}

void SettingsWindow::setKeepOnTop(bool keepOnTop)
{
    m_keepOnTopCheckBox->setChecked(keepOnTop);
}

void SettingsWindow::setTtsBackend(int backend)
{
    for (int i = 0; i < m_backendCombo->count(); ++i) {
        if (m_backendCombo->itemData(i).toInt() == backend) {
            m_backendCombo->setCurrentIndex(i);
            break;
        }
    }
    updateBackendVisibility();
}

void SettingsWindow::setAvailableVoices(const QStringList &voices, const QString &currentVoice)
{
    // 如果当前后端是 Edge TTS，VoiceVox 的语音列表不应覆盖 Edge 的选择
    if (m_backendCombo->currentData().toInt() == 0) return;

    m_voiceComboBox->clear();

    if (voices.isEmpty()) {
        m_voiceComboBox->addItem(QStringLiteral("（无可用语音）"), QString());
        return;
    }

    for (const QString &v : voices) {
        m_voiceComboBox->addItem(v, v);
    }

    if (currentVoice.isEmpty()) {
        m_voiceComboBox->setCurrentIndex(0);
    } else {
        const int idx = m_voiceComboBox->findData(currentVoice);
        if (idx >= 0) {
            m_voiceComboBox->setCurrentIndex(idx);
        }
    }
}

void SettingsWindow::setVoiceVoxUrl(const QString &url)
{
    m_vvUrlEdit->setText(url);
}

void SettingsWindow::setVoiceVoxLoading(bool loading)
{
    m_vvStatusLabel->setText(loading
        ? QStringLiteral("正在加载说话人列表...")
        : QStringLiteral(""));
    m_vvStatusLabel->setVisible(loading);
}

void SettingsWindow::setSnapThresholdPx(int px)
{
    m_snapThresholdSpinBox->setValue(px);
}

void SettingsWindow::setIdleTimeoutMinutes(int minutes)
{
    m_idleTimeoutSpinBox->setValue(minutes);
}

void SettingsWindow::setModelScale(double scale)
{
    const int percent = qBound(30, qRound(scale * 100.0), 1000);
    m_sizeSlider->setValue(percent);
    m_sizeLabel->setText(QStringLiteral("%1%").arg(percent));
}

void SettingsWindow::setHeadTrackingEnabled(bool enabled)
{
    m_headTrackingCheckBox->setChecked(enabled);
}

void SettingsWindow::setChatBackgroundImagePath(const QString &path)
{
    const auto hintColor = palette().placeholderText().color();
    if (path.isEmpty()) {
        m_chatBgPathLabel->setText(QStringLiteral("（未选择）"));
        m_chatBgPathLabel->setStyleSheet(QString());
        m_chatBgPathLabel->setToolTip(QString());
    } else {
        // 横向截断：超出标签宽度显示 …
        QFontMetrics fm(m_chatBgPathLabel->font());
        const int labelWidth = m_chatBgPathLabel->width() - 8;
        const QString elided = labelWidth > 0
            ? fm.elidedText(path, Qt::ElideMiddle, labelWidth)
            : path;
        m_chatBgPathLabel->setText(elided);
        m_chatBgPathLabel->setStyleSheet(QString());
        m_chatBgPathLabel->setToolTip(path);
    }
}

void SettingsWindow::setChatBackgroundOpacity(double opacity)
{
    const int percent = qBound(1, qRound(opacity * 100.0), 100);
    m_chatBgOpacitySlider->setValue(percent);
    m_chatBgOpacityLabel->setText(QStringLiteral("%1%").arg(percent));
}

void SettingsWindow::setChatCropButtonEnabled(bool enabled)
{
    m_chatBgCropButton->setEnabled(enabled);
}

void SettingsWindow::setScreenshotPath(const QString &path)
{
    m_screenshotPathEdit->setText(path);
}

// ──── Edge TTS 设置 ──────────────────────────────────────────────────────────

void SettingsWindow::setEdgeExecutablePath(const QString &path)
{
    m_edgeExecPathEdit->setText(path);
}

void SettingsWindow::setEdgeVoice(const QString &voice)
{
    m_edgeVoice = voice;
    // 如果当前后端是 Edge，更新可编辑组合框的文本
    if (m_backendCombo->currentData().toInt() == 0) {
        m_voiceComboBox->setCurrentText(voice);
    }
}

void SettingsWindow::setEdgeRatePercent(int percent)
{
    m_edgeRateSpinBox->setValue(percent);
}

void SettingsWindow::setEdgeVolumePercent(int percent)
{
    m_edgeVolumeSpinBox->setValue(percent);
}

void SettingsWindow::setEdgePitchHz(int hz)
{
    m_edgePitchSpinBox->setValue(hz);
}

void SettingsWindow::setEdgeDetectionText(const QString &text)
{
    m_edgeDetectionLabel->setText(text);
}

void SettingsWindow::setEdgeTestEnabled(bool enabled)
{
    m_edgeTestBtn->setEnabled(enabled);
}

void SettingsWindow::setEdgeTestStatusText(const QString &text)
{
    m_edgeTestStatusLabel->setText(text);
}

void SettingsWindow::onChatBackgroundBrowseClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择聊天背景图片"),
        QString(),
        QStringLiteral("图片文件 (*.png *.jpg *.jpeg *.bmp *.gif *.webp)")
    );
    if (filePath.isEmpty()) return;
    emit chatBackgroundImageSelected(filePath);
}

void SettingsWindow::onChatBackgroundClearClicked()
{
    emit chatBackgroundClearRequested();
}

void SettingsWindow::onChatBackgroundCropClicked()
{
    emit chatBackgroundCropRequested(m_chatBgPathLabel->text());
}

void SettingsWindow::onScreenshotBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择截图保存目录"),
        m_screenshotPathEdit->text().isEmpty()
            ? QDir::homePath()
            : m_screenshotPathEdit->text()
    );
    if (dir.isEmpty()) return;
    m_screenshotPathEdit->setText(dir);
}

// ──── 🎨 说话气泡颜色选择 ────

void SettingsWindow::onBubbleBgColorClicked()
{
    QColor c = QColorDialog::getColor(m_bubbleBgColor, this, QStringLiteral("选择气泡背景色"));
    if (c.isValid()) {
        m_bubbleBgColor = c;
        m_bubbleBgBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; border: 2px solid #999; border-radius: 4px; }"
            "QPushButton:hover { border-color: #333; }"
        ).arg(c.name()));
        emit bubbleBgColorChanged(c);
    }
}

void SettingsWindow::onBubbleBorderColorClicked()
{
    QColor c = QColorDialog::getColor(m_bubbleBorderColor, this, QStringLiteral("选择气泡边框色"));
    if (c.isValid()) {
        m_bubbleBorderColor = c;
        m_bubbleBorderBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; border: 2px solid #999; border-radius: 4px; }"
            "QPushButton:hover { border-color: #333; }"
        ).arg(c.name()));
        emit bubbleBorderColorChanged(c);
    }
}

void SettingsWindow::onBubbleTextColorClicked()
{
    QColor c = QColorDialog::getColor(m_bubbleTextColor, this, QStringLiteral("选择气泡文字色"));
    if (c.isValid()) {
        m_bubbleTextColor = c;
        m_bubbleTextBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; border: 2px solid #999; border-radius: 4px; }"
            "QPushButton:hover { border-color: #333; }"
        ).arg(c.name()));
        emit bubbleTextColorChanged(c);
    }
}

void SettingsWindow::setBubbleBgColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_bubbleBgColor = color;
    if (m_bubbleBgBtn) {
        m_bubbleBgBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; border: 2px solid #999; border-radius: 4px; }"
            "QPushButton:hover { border-color: #333; }"
        ).arg(color.name()));
    }
}

void SettingsWindow::setBubbleBorderColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_bubbleBorderColor = color;
    if (m_bubbleBorderBtn) {
        m_bubbleBorderBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; border: 2px solid #999; border-radius: 4px; }"
            "QPushButton:hover { border-color: #333; }"
        ).arg(color.name()));
    }
}

void SettingsWindow::setBubbleTextColor(const QColor &color)
{
    if (!color.isValid()) return;
    m_bubbleTextColor = color;
    if (m_bubbleTextBtn) {
        m_bubbleTextBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background-color: %1; border: 2px solid #999; border-radius: 4px; }"
            "QPushButton:hover { border-color: #333; }"
        ).arg(color.name()));
    }
}

// ──── Edge TTS 浏览可执行文件 ────────────────────────────────────────────────

void SettingsWindow::onEdgeBrowseClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        QStringLiteral("选择 Edge TTS 可执行文件"),
        QString(),
        QStringLiteral("可执行文件 (*.exe);;所有文件 (*.*)")
    );
    if (path.isEmpty()) return;
    m_edgeExecPathEdit->setText(path);
}

// ──── Edge TTS 测试语音 ──────────────────────────────────────────────────────

void SettingsWindow::onEdgeTestSpeechClicked()
{
    // 禁用按钮，防止重复点击
    m_edgeTestBtn->setEnabled(false);
    m_edgeTestStatusLabel->setText(QStringLiteral("合成中..."));

    emit edgeTestSpeechRequested(QStringLiteral("这是一次语音测试喵~"));
}

void SettingsWindow::closeEvent(QCloseEvent *event)
{
    emit closed();
    QWidget::closeEvent(event);
}

void SettingsWindow::showAndRaise()
{
    // 1. 还原最小化状态（如果窗口被最小化了）
    if (isMinimized()) {
        showNormal();
    } else {
        show();
    }

    // 2. 提起到前台
    raise();
    activateWindow();

    // 3. 延迟激活：Windows 在托盘菜单事件处理期间会阻止 raise/activateWindow
    //    （焦点防窃取机制）。把激活操作推迟到事件处理完成后就能绕过此限制。
    QTimer::singleShot(0, this, [this]() {
        activateWindow();
        raise();
    });
}
