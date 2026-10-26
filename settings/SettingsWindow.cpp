#include "SettingsWindow.h"
#include "../core/BedtimeHelper.h"
#include "../ai/PromptManager.h"

#include <QLineEdit>
#include <QPlainTextEdit>
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
#include <QListWidget>
#include <QProgressBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDateTime>
#include <QDateEdit>

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
    m_sizeSlider->setRange(30, 400);      // 30%~400%（值/100=倍率），上限受 PetWindow::kMaxModelScale=4.0 约束
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

    // ──── 深夜就寝提醒 ────
    auto *bedtimeGroup = new QGroupBox(QStringLiteral("深夜就寝提醒"), this);
    auto *bedtimeLayout = new QVBoxLayout(bedtimeGroup);

    m_bedtimeEnableCheckBox = new QCheckBox(QStringLiteral("启用深夜就寝提醒"), this);
    m_bedtimeEnableCheckBox->setChecked(true);

    m_bedtimeRandomCheckBox = new QCheckBox(QStringLiteral("随机使用本地提醒文案"), this);
    m_bedtimeRandomCheckBox->setChecked(true);
    // 随机模式开启时，自定义文本编辑区不可用
    connect(m_bedtimeRandomCheckBox, &QCheckBox::toggled, this, [this](bool random) {
        m_bedtimeEdit->setEnabled(!random);
        m_bedtimeResetBtn->setEnabled(!random);
    });

    auto *msgRow = new QHBoxLayout();
    m_bedtimeEdit = new QLineEdit(this);
    m_bedtimeEdit->setPlaceholderText(QStringLiteral("主人，已经很晚啦，早点休息吧喵～"));
    m_bedtimeEdit->setMaxLength(200);
    m_bedtimeResetBtn = new QPushButton(QStringLiteral("恢复默认"), this);
    msgRow->addWidget(m_bedtimeEdit, 1);
    msgRow->addWidget(m_bedtimeResetBtn);

    auto *hintLabel = new QLabel(QStringLiteral("深夜 0:00～6:00 内，每 60 分钟最多提醒一次。"), this);
    hintLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));

    bedtimeLayout->addWidget(m_bedtimeEnableCheckBox);
    bedtimeLayout->addWidget(m_bedtimeRandomCheckBox);
    bedtimeLayout->addLayout(msgRow);
    bedtimeLayout->addWidget(hintLabel);
    bedtimeGroup->setLayout(bedtimeLayout);

    connect(m_bedtimeResetBtn, &QPushButton::clicked, this, &SettingsWindow::onBedtimeResetClicked);

    // ──── AI 说话风格 ────
    auto *styleGroup = new QGroupBox(QStringLiteral("AI 说话风格"), this);
    auto *styleLayout = new QVBoxLayout(styleGroup);

    m_stylePromptEdit = new QPlainTextEdit(this);
    m_stylePromptEdit->setPlaceholderText(
        QStringLiteral("例如：活泼可爱，称呼用户为主人，喜欢用颜文字，每句话结尾加上喵~"));
    m_stylePromptEdit->setFixedHeight(80);
    m_stylePromptEdit->setTabChangesFocus(true);

    // 字数限制：最多 2000 字符
    connect(m_stylePromptEdit, &QPlainTextEdit::textChanged, this, [this]() {
        QString text = m_stylePromptEdit->toPlainText();
        if (text.length() > 2000) {
            text = text.left(2000);
            // 阻塞信号以避免递归
            QSignalBlocker blocker(m_stylePromptEdit);
            m_stylePromptEdit->setPlainText(text);
            // 把光标移到尾部
            QTextCursor cursor = m_stylePromptEdit->textCursor();
            cursor.movePosition(QTextCursor::End);
            m_stylePromptEdit->setTextCursor(cursor);
        }
        m_stylePromptCountLabel->setText(
            QStringLiteral("%1 / 2000 字").arg(text.length()));
    });

    m_stylePromptCountLabel = new QLabel(QStringLiteral("0 / 2000 字"), this);
    m_stylePromptCountLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));

    auto *styleBtnRow = new QHBoxLayout();
    m_styleResetBtn = new QPushButton(QStringLiteral("恢复默认"), this);
    styleBtnRow->addWidget(m_styleResetBtn);
    styleBtnRow->addStretch();

    auto *styleHint = new QLabel(
        QStringLiteral("用于控制 AI 的语气、称呼和表达方式，不影响 API 地址和模型设置。"),
        this);
    styleHint->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    styleHint->setWordWrap(true);

    styleLayout->addWidget(m_stylePromptEdit);
    styleLayout->addWidget(m_stylePromptCountLabel);
    styleLayout->addLayout(styleBtnRow);
    styleLayout->addWidget(styleHint);
    styleGroup->setLayout(styleLayout);

    connect(m_styleResetBtn, &QPushButton::clicked, this, &SettingsWindow::onStyleResetClicked);

    // ──── 长期记忆 ────
    auto *memoryGroup = new QGroupBox(QStringLiteral("长期记忆"), this);
    auto *memoryLayout = new QVBoxLayout(memoryGroup);

    m_memoryEnableCheckBox = new QCheckBox(
        QStringLiteral("启用长期记忆（跨重启记住主人说过的事）"), this);
    m_memoryEnableCheckBox->setChecked(true);

    m_memoryAutoExtractCheckBox = new QCheckBox(
        QStringLiteral("自动学习（每 6 轮对话或闲置 3 分钟提炼一次）"), this);
    m_memoryAutoExtractCheckBox->setChecked(true);
    connect(m_memoryEnableCheckBox, &QCheckBox::toggled, this, [this](bool enabled) {
        m_memoryAutoExtractCheckBox->setEnabled(enabled);
    });
    connect(m_memoryEnableCheckBox, &QCheckBox::toggled,
            this, &SettingsWindow::longTermMemoryToggled);
    connect(m_memoryAutoExtractCheckBox, &QCheckBox::toggled,
            this, &SettingsWindow::autoMemoryExtractToggled);

    auto *memorySearchRow = new QHBoxLayout();
    m_memorySearchEdit = new QLineEdit(this);
    m_memorySearchEdit->setPlaceholderText(QStringLiteral("搜索内容 / 关键词 / 对象"));
    m_memorySearchEdit->setClearButtonEnabled(true);
    auto *memorySearchBtn = new QPushButton(QStringLiteral("搜索"), this);
    auto *memoryRefreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    memorySearchRow->addWidget(m_memorySearchEdit, 1);
    memorySearchRow->addWidget(memorySearchBtn);
    memorySearchRow->addWidget(memoryRefreshBtn);

    m_memoryList = new QListWidget(this);
    m_memoryList->setMinimumHeight(150);
    m_memoryList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_memoryList->setAlternatingRowColors(true);
    m_memoryList->setWordWrap(true);

    m_memoryStatusLabel = new QLabel(QStringLiteral("共 0 条卡片"), this);
    m_memoryStatusLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));

    auto *memoryBtnRow1 = new QHBoxLayout();
    m_memoryEditBtn = new QPushButton(QStringLiteral("编辑"), this);
    m_memoryDeleteBtn = new QPushButton(QStringLiteral("删除"), this);
    m_memoryPinBtn = new QPushButton(QStringLiteral("置顶/取消"), this);
    memoryBtnRow1->addWidget(m_memoryEditBtn);
    memoryBtnRow1->addWidget(m_memoryDeleteBtn);
    memoryBtnRow1->addWidget(m_memoryPinBtn);
    memoryBtnRow1->addStretch();

    auto *memoryBtnRow2 = new QHBoxLayout();
    m_memoryClearScopeBtn = new QPushButton(QStringLiteral("清空本角色专属记忆"), this);
    auto *memoryClearAllBtn = new QPushButton(QStringLiteral("清空全部"), this);
    m_memoryBackfillBtn = new QPushButton(QStringLiteral("从历史回填"), this);
    memoryBtnRow2->addWidget(m_memoryClearScopeBtn);
    memoryBtnRow2->addWidget(memoryClearAllBtn);
    memoryBtnRow2->addWidget(m_memoryBackfillBtn);

    // 回填时间范围：[起, 止)；起始停留在最小值时显示“最早”，表示不限下界
    auto *memoryBackfillRangeRow = new QHBoxLayout();
    auto *backfillRangeLabel = new QLabel(QStringLiteral("回填范围"), this);
    m_memoryBackfillFrom = new QDateEdit(this);
    m_memoryBackfillFrom->setCalendarPopup(true);
    m_memoryBackfillFrom->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_memoryBackfillFrom->setMinimumDate(QDate(2000, 1, 1));
    m_memoryBackfillFrom->setMaximumDate(QDate(2100, 12, 31));
    m_memoryBackfillFrom->setSpecialValueText(QStringLiteral("最早"));
    m_memoryBackfillFrom->setDate(m_memoryBackfillFrom->minimumDate());
    m_memoryBackfillFrom->setToolTip(QStringLiteral("留作「最早」表示从第一条消息开始；否则只回填这一天及之后"));
    m_memoryBackfillTo = new QDateEdit(this);
    m_memoryBackfillTo->setCalendarPopup(true);
    m_memoryBackfillTo->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_memoryBackfillTo->setMinimumDate(QDate(2000, 1, 1));
    m_memoryBackfillTo->setMaximumDate(QDate(2100, 12, 31));
    m_memoryBackfillTo->setDate(QDate(2026, 9, 1));
    m_memoryBackfillTo->setToolTip(QStringLiteral("只回填这一天之前（不含当天）的消息"));
    memoryBackfillRangeRow->addWidget(backfillRangeLabel);
    memoryBackfillRangeRow->addWidget(m_memoryBackfillFrom, 1);
    memoryBackfillRangeRow->addWidget(new QLabel(QStringLiteral("至"), this));
    memoryBackfillRangeRow->addWidget(m_memoryBackfillTo, 1);

    m_memoryBackfillLabel = new QLabel(this);
    m_memoryBackfillLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    m_memoryBackfillLabel->setWordWrap(true);
    m_memoryBackfillBar = new QProgressBar(this);
    m_memoryBackfillBar->setRange(0, 100);
    m_memoryBackfillBar->setVisible(false);

    auto *memoryHint = new QLabel(
        QStringLiteral("对话里可以用 /记住 内容 和 /忘掉 关键词 手动管理。"
                       "共享画像（标记为「共享」）在切换角色后依然保留。"),
        this);
    memoryHint->setStyleSheet(QStringLiteral("font-size: 11px; color: #888;"));
    memoryHint->setWordWrap(true);

    memoryLayout->addWidget(m_memoryEnableCheckBox);
    memoryLayout->addWidget(m_memoryAutoExtractCheckBox);
    memoryLayout->addLayout(memorySearchRow);
    memoryLayout->addWidget(m_memoryList, 1);
    memoryLayout->addWidget(m_memoryStatusLabel);
    memoryLayout->addLayout(memoryBtnRow1);
    memoryLayout->addLayout(memoryBtnRow2);
    memoryLayout->addLayout(memoryBackfillRangeRow);
    memoryLayout->addWidget(m_memoryBackfillBar);
    memoryLayout->addWidget(m_memoryBackfillLabel);
    memoryLayout->addWidget(memoryHint);
    memoryGroup->setLayout(memoryLayout);

    connect(memorySearchBtn, &QPushButton::clicked, this, &SettingsWindow::onMemorySearchClicked);
    connect(m_memorySearchEdit, &QLineEdit::returnPressed,
            this, &SettingsWindow::onMemorySearchClicked);
    connect(memoryRefreshBtn, &QPushButton::clicked, this, &SettingsWindow::onMemoryRefreshClicked);
    connect(m_memoryEditBtn, &QPushButton::clicked, this, &SettingsWindow::onMemoryEditClicked);
    connect(m_memoryDeleteBtn, &QPushButton::clicked, this, &SettingsWindow::onMemoryDeleteClicked);
    connect(m_memoryPinBtn, &QPushButton::clicked, this, &SettingsWindow::onMemoryTogglePinClicked);
    connect(m_memoryClearScopeBtn, &QPushButton::clicked,
            this, &SettingsWindow::onMemoryClearScopeClicked);
    connect(memoryClearAllBtn, &QPushButton::clicked,
            this, &SettingsWindow::onMemoryClearAllClicked);
    connect(m_memoryBackfillBtn, &QPushButton::clicked,
            this, &SettingsWindow::onMemoryBackfillClicked);
    connect(m_memoryList, &QListWidget::itemDoubleClicked,
            this, &SettingsWindow::onMemoryItemDoubleClicked);

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
    contentLayout->addWidget(bedtimeGroup);
    contentLayout->addWidget(styleGroup);
    contentLayout->addWidget(memoryGroup);
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

void SettingsWindow::refreshCurrentVoiceList()
{
    m_voiceComboBox->clear();
    const bool isEdge = (m_backendCombo->currentData().toInt() == 0);
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

        // 确保自定义语音（可能不在预置列表中）不丢失
        if (!commonVoices.contains(m_edgeVoice) && !m_edgeVoice.isEmpty()) {
            m_voiceComboBox->addItem(m_edgeVoice);
        }

        m_voiceComboBox->setCurrentText(m_edgeVoice);
        // 启用测试按钮，恢复就绪状态
        m_edgeTestBtn->setEnabled(true);
        m_edgeTestStatusLabel->setText(QStringLiteral("就绪"));
    } else {
        // VoiceVox: selection combo, show placeholder
        m_voiceComboBox->addItem(QStringLiteral("（切换后端后请保存以加载语音）"), QString());
    }
}

void SettingsWindow::onBackendChanged(int)
{
    updateBackendVisibility();
    refreshCurrentVoiceList();
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

    // ★ 深夜就寝提醒
    emit bedtimeReminderToggled(m_bedtimeEnableCheckBox->isChecked());
    emit bedtimeRandomToggled(m_bedtimeRandomCheckBox->isChecked());
    {
        // 始终经过清洗，空/空白 → 默认文本，清洗后回填到输入框
        const QString cleanMsg = BedtimeHelper::sanitizeBedtimeMessage(m_bedtimeEdit->text());
        m_bedtimeEdit->setText(cleanMsg);
        emit bedtimeMessageChanged(cleanMsg);
    }

    // ──── AI 说话风格 ────
    {
        QString style = m_stylePromptEdit->toPlainText().trimmed();
        if (style.length() > 2000) {
            style = style.left(2000);
        }
        // 回填清洗后的文本到输入框
        QSignalBlocker blocker(m_stylePromptEdit);
        m_stylePromptEdit->setPlainText(style);
        m_stylePromptCountLabel->setText(
            QStringLiteral("%1 / 2000 字").arg(style.length()));
        emit aiStylePromptChanged(style);
    }

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
    {
        QSignalBlocker blocker(m_backendCombo);
        for (int i = 0; i < m_backendCombo->count(); ++i) {
            if (m_backendCombo->itemData(i).toInt() == backend) {
                m_backendCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    // QSignalBlocker 出作用域后撤销阻塞，但下面两行手动完成初始化，
    // 不需要依赖信号触发 onBackendChanged。
    updateBackendVisibility();
    refreshCurrentVoiceList();
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
    const int percent = qBound(30, qRound(scale * 100.0), 400);
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

// ──── 深夜就寝提醒 ────────────────────────────────────────────────────────────

void SettingsWindow::onBedtimeResetClicked()
{
    m_bedtimeEdit->setText(QStringLiteral("主人，已经很晚啦，早点休息吧喵～"));
}

void SettingsWindow::onStyleResetClicked()
{
    // 恢复默认：文本框显示实际默认文本，但保存空值作为"使用默认"的标记
    const QString defaultText = PromptManager::defaultAiStylePrompt();
    {
        QSignalBlocker blocker(m_stylePromptEdit);
        m_stylePromptEdit->setPlainText(defaultText);
        m_stylePromptCountLabel->setText(
            QStringLiteral("%1 / 2000 字").arg(defaultText.length()));
    }
    // 保存空值 = 使用默认风格（不在 QSettings 中存大段文本）
    emit aiStylePromptChanged(QString());
}

void SettingsWindow::setAiStylePrompt(const QString &prompt)
{
    QString text = prompt.trimmed();
    if (text.length() > 2000) {
        text = text.left(2000);
    }
    QSignalBlocker blocker(m_stylePromptEdit);
    m_stylePromptEdit->setPlainText(text);
    m_stylePromptCountLabel->setText(
        QStringLiteral("%1 / 2000 字").arg(text.length()));
}

void SettingsWindow::setBedtimeReminderEnabled(bool enabled)
{
    m_bedtimeEnableCheckBox->setChecked(enabled);
}

void SettingsWindow::setBedtimeMessage(const QString &msg)
{
    m_bedtimeEdit->setText(msg);
}

void SettingsWindow::setBedtimeRandomEnabled(bool enabled)
{
    m_bedtimeRandomCheckBox->setChecked(enabled);
}

// ──── 长期记忆 ────────────────────────────────────────────────────────────────

void SettingsWindow::setLongTermMemoryEnabled(bool enabled)
{
    QSignalBlocker blocker(m_memoryEnableCheckBox);
    m_memoryEnableCheckBox->setChecked(enabled);
    m_memoryAutoExtractCheckBox->setEnabled(enabled);
}

void SettingsWindow::setAutoMemoryExtractEnabled(bool enabled)
{
    QSignalBlocker blocker(m_memoryAutoExtractCheckBox);
    m_memoryAutoExtractCheckBox->setChecked(enabled);
}

void SettingsWindow::setMemoryScope(const QString &characterName,
                                    const QString &displayName)
{
    m_memoryScopeName = characterName;
    m_memoryScopeDisplayName = displayName.isEmpty() ? characterName : displayName;
    m_memoryClearScopeBtn->setText(QStringLiteral("清空本角色专属记忆"));
    m_memoryClearScopeBtn->setToolTip(
        QStringLiteral("只删除「%1」自己的记忆卡片；共享画像和其他角色不受影响")
            .arg(m_memoryScopeDisplayName));
}

void SettingsWindow::setMemoryBackfillRange(const QDate &from, const QDate &to)
{
    if (m_memoryBackfillFrom) {
        const QDate clamped = from.isValid()
            ? qBound(m_memoryBackfillFrom->minimumDate(), from,
                     m_memoryBackfillFrom->maximumDate())
            : m_memoryBackfillFrom->minimumDate();
        m_memoryBackfillFrom->setDate(clamped);
    }
    if (m_memoryBackfillTo && to.isValid()) {
        m_memoryBackfillTo->setDate(
            qBound(m_memoryBackfillTo->minimumDate(), to,
                   m_memoryBackfillTo->maximumDate()));
    }
}

void SettingsWindow::setMemoryList(const QList<MemoryRecord> &memories, int total)
{
    m_memoryList->clear();

    for (const MemoryRecord &m : memories) {
        const QString pin = (m.status == QStringLiteral("pinned"))
            ? QStringLiteral("★ ") : QString();
        const QString scope = (m.characterName == QStringLiteral("*"))
            ? QStringLiteral("[共享] ") : QString();
        auto *item = new QListWidgetItem(
            QStringLiteral("%1%2[%3·%4] %5")
                .arg(pin, scope, m.kind, QString::number(m.importance), m.content),
            m_memoryList);

        item->setData(Qt::UserRole, m.id);
        item->setData(Qt::UserRole + 1, m.content);
        item->setData(Qt::UserRole + 2, m.kind);
        item->setData(Qt::UserRole + 3, m.importance);
        item->setData(Qt::UserRole + 4, m.status == QStringLiteral("pinned"));
        item->setData(Qt::UserRole + 5, m.subject);
        item->setToolTip(QStringLiteral("对象：%1\n关键词：%2\n创建：%3\n命中：%4 次")
                             .arg(m.subject, m.keywords, m.createdAt,
                                  QString::number(m.hitCount)));
    }

    m_memoryStatusLabel->setText(
        QStringLiteral("共 %1 条卡片（当前显示 %2 条）").arg(total).arg(memories.size()));
}

void SettingsWindow::setMemoryBackfillStatus(const QString &text, int progressPercent)
{
    m_memoryBackfillLabel->setText(text);
    if (progressPercent >= 0) {
        m_memoryBackfillBar->setVisible(true);
        m_memoryBackfillBar->setValue(qBound(0, progressPercent, 100));
    } else {
        m_memoryBackfillBar->setVisible(false);
    }
}

void SettingsWindow::onMemorySearchClicked()
{
    emit memoryRefreshRequested(m_memorySearchEdit->text().trimmed());
}

void SettingsWindow::onMemoryRefreshClicked()
{
    emit memoryRefreshRequested(m_memorySearchEdit->text().trimmed());
}

void SettingsWindow::onMemoryEditClicked()
{
    auto *item = m_memoryList->currentItem();
    if (!item) return;

    const int id = item->data(Qt::UserRole).toInt();
    const QString content = item->data(Qt::UserRole + 1).toString();
    const QString kind = item->data(Qt::UserRole + 2).toString();
    const int importance = item->data(Qt::UserRole + 3).toInt();
    const bool pinned = item->data(Qt::UserRole + 4).toBool();

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("编辑记忆"));
    auto *form = new QFormLayout(&dialog);

    auto *contentEdit = new QPlainTextEdit(&dialog);
    contentEdit->setPlainText(content);
    contentEdit->setFixedHeight(90);

    auto *kindCombo = new QComboBox(&dialog);
    kindCombo->addItems({ QStringLiteral("profile"), QStringLiteral("preference"),
                          QStringLiteral("event"), QStringLiteral("promise"),
                          QStringLiteral("relationship") });
    if (kindCombo->findText(kind) >= 0) kindCombo->setCurrentText(kind);

    auto *importanceSpin = new QSpinBox(&dialog);
    importanceSpin->setRange(1, 5);
    importanceSpin->setValue(importance);

    auto *pinnedCheck = new QCheckBox(QStringLiteral("置顶（每轮固定注入）"), &dialog);
    pinnedCheck->setChecked(pinned);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    form->addRow(QStringLiteral("内容："), contentEdit);
    form->addRow(QStringLiteral("类型："), kindCombo);
    form->addRow(QStringLiteral("重要度："), importanceSpin);
    form->addRow(pinnedCheck);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    const QString newContent = contentEdit->toPlainText().trimmed();
    if (newContent.isEmpty()) return;
    emit memoryUpdateRequested(id, newContent, kindCombo->currentText(),
                               importanceSpin->value(), pinnedCheck->isChecked());
}

void SettingsWindow::onMemoryDeleteClicked()
{
    auto *item = m_memoryList->currentItem();
    if (!item) return;

    const int id = item->data(Qt::UserRole).toInt();
    const QString content = item->data(Qt::UserRole + 1).toString();
    const auto answer = QMessageBox::question(
        this, QStringLiteral("删除记忆"),
        QStringLiteral("确定删除这条记忆吗？\n\n%1").arg(content),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        emit memoryDeleteRequested(id);
    }
}

void SettingsWindow::onMemoryTogglePinClicked()
{
    auto *item = m_memoryList->currentItem();
    if (!item) return;

    const int id = item->data(Qt::UserRole).toInt();
    const QString content = item->data(Qt::UserRole + 1).toString();
    const QString kind = item->data(Qt::UserRole + 2).toString();
    const int importance = item->data(Qt::UserRole + 3).toInt();
    const bool pinned = item->data(Qt::UserRole + 4).toBool();
    emit memoryUpdateRequested(id, content, kind, importance, !pinned);
}

void SettingsWindow::onMemoryClearScopeClicked()
{
    const auto answer = QMessageBox::question(
        this, QStringLiteral("清空本角色专属记忆"),
        QStringLiteral("确定清空「%1」的专属记忆吗？\n"
                       "共享画像（[共享] 标记）和其他角色的记忆不受影响。")
            .arg(m_memoryScopeDisplayName),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        emit memoryClearRequested(m_memoryScopeName);
    }
}

void SettingsWindow::onMemoryClearAllClicked()
{
    const auto answer = QMessageBox::question(
        this, QStringLiteral("清空全部长期记忆"),
        QStringLiteral("确定清空所有角色（含共享画像）的长期记忆吗？此操作不可撤销。"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer == QMessageBox::Yes) {
        emit memoryClearAllRequested();
    }
}

void SettingsWindow::onMemoryBackfillClicked()
{
    const bool noLower =
        (m_memoryBackfillFrom->date() == m_memoryBackfillFrom->minimumDate());
    const QString fromText = noLower
        ? QStringLiteral("最早")
        : m_memoryBackfillFrom->date().toString(QStringLiteral("yyyy-MM-dd"));
    const QString toText =
        m_memoryBackfillTo->date().toString(QStringLiteral("yyyy-MM-dd"));

    const auto answer = QMessageBox::question(
        this, QStringLiteral("从历史回填"),
        QStringLiteral("回填范围：%1 至 %2（不含 %2 当天）\n"
                       "该范围内的历史对话会发送给 DeepSeek AI 提炼记忆。\n"
                       "只在你点击时执行，范围越大消耗 token 越多。\n\n"
                       "继续吗？")
            .arg(fromText, toText),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    const QString afterIso = noLower
        ? QString()
        : m_memoryBackfillFrom->date().toString(QStringLiteral("yyyy-MM-dd"))
              + QStringLiteral("T00:00:00");
    const QString beforeIso = toText + QStringLiteral("T00:00:00");
    emit memoryBackfillRequested(afterIso, beforeIso);
}

void SettingsWindow::onMemoryItemDoubleClicked(QListWidgetItem *)
{
    onMemoryEditClicked();
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
