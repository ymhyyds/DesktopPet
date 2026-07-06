#include "ChatWindow.h"
#include "../memory/MemoryDB.h"

#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QCloseEvent>
#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QFileDialog>
#include <QTextStream>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QMenu>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QToolTip>
#include <QSettings>
#include <QEvent>
#include <QMessageBox>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QTimer>

// ============================================================================
// 即时 tooltip：鼠标移上立刻显示，不等待系统延迟
// ============================================================================
namespace {
class InstantTooltipFilter : public QObject
{
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Enter) {
            if (auto *w = qobject_cast<QWidget *>(obj); w && !w->toolTip().isEmpty()) {
                QToolTip::showText(QCursor::pos(), w->toolTip(), w);
            }
        } else if (event->type() == QEvent::Leave || event->type() == QEvent::MouseButtonPress) {
            QToolTip::hideText();
        } else if (event->type() == QEvent::MouseMove) {
            if (auto *w = qobject_cast<QWidget *>(obj); w && QToolTip::isVisible()) {
                QToolTip::showText(QCursor::pos(), w->toolTip(), w);
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

void installInstantTooltip(QWidget *w)
{
    w->installEventFilter(new InstantTooltipFilter(w));
}

/// 辅助：自动调整 QTextEdit 高度以适应其文档内容
void autoResizeTextEdit(QTextEdit *te)
{
    if (!te) return;
    te->document()->setTextWidth(te->viewport()->width());
    int h = static_cast<int>(te->document()->size().height());
    te->setFixedHeight(qMax(h + 4, 24));
}
}

// ============================================================================
// 构造
// ============================================================================

ChatWindow::ChatWindow(QWidget *parent)
    : QWidget(parent)
{
    setupUi();

    // 🏞️ 恢复背景图设置
    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("ChatWindow"));
    m_backgroundImagePath = settings.value(QStringLiteral("background/path")).toString();
    m_backgroundSourcePath = settings.value(QStringLiteral("background/sourcePath")).toString();
    if (m_backgroundSourcePath.isEmpty()) {
        m_backgroundSourcePath = m_backgroundImagePath; // 兼容旧存档
    }
    m_backgroundOpacity = qBound(0.0,
        settings.value(QStringLiteral("background/opacity"), 0.3).toDouble(), 1.0);
    if (!m_backgroundImagePath.isEmpty()) {
        QPixmap pixmap(m_backgroundImagePath);
        if (!pixmap.isNull()) {
            m_backgroundImage = pixmap;
            qInfo() << "[ChatWindow] 已恢复背景图：" << m_backgroundImagePath;
        } else {
            m_backgroundImagePath.clear();
        }
    }
}

// ============================================================================
// setupUi — 完整 UI 搭建
// ============================================================================

void ChatWindow::setupUi()
{
    setWindowTitle(QStringLiteral("和我聊聊吧～"));
    resize(440, 560);
    setMinimumSize(320, 400);

    // 显式声明窗口标志，确保最小化/最大化/还原按钮正常工作
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint);

    // ========== 顶部工具栏 ==========
    auto *toolbarLayout = new QHBoxLayout();
    toolbarLayout->setContentsMargins(8, 4, 8, 0);
    toolbarLayout->setSpacing(4);

    m_searchToggleBtn = new QPushButton(QStringLiteral("搜索"), this);
    m_searchToggleBtn->setFixedHeight(28);
    m_searchToggleBtn->setCursor(Qt::PointingHandCursor);
    m_searchToggleBtn->setToolTip(QStringLiteral("搜索对话历史"));
    installInstantTooltip(m_searchToggleBtn);

    m_exportBtn = new QPushButton(QStringLiteral("导出"), this);
    m_exportBtn->setFixedHeight(28);
    m_exportBtn->setCursor(Qt::PointingHandCursor);
    m_exportBtn->setToolTip(QStringLiteral("导出聊天记录"));
    installInstantTooltip(m_exportBtn);

    m_clearBtn = new QPushButton(QStringLiteral("清除"), this);
    m_clearBtn->setFixedHeight(28);
    m_clearBtn->setCursor(Qt::PointingHandCursor);
    m_clearBtn->setToolTip(QStringLiteral("清除当前角色的所有聊天记录"));
    m_clearBtn->setStyleSheet(QStringLiteral(
        "QPushButton { color: #e74c3c; }"
        "QPushButton:hover { color: #c0392b; }"
    ));
    installInstantTooltip(m_clearBtn);

    toolbarLayout->addStretch();
    toolbarLayout->addWidget(m_searchToggleBtn);
    toolbarLayout->addWidget(m_exportBtn);
    toolbarLayout->addWidget(m_clearBtn);

    // ========== 搜索栏（默认隐藏） ==========
    m_searchBar = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(m_searchBar);
    searchLayout->setContentsMargins(8, 2, 8, 2);
    searchLayout->setSpacing(4);

    auto *searchIcon = new QLabel(QStringLiteral("🔍"), m_searchBar);
    searchIcon->setFixedWidth(20);
    searchIcon->setAlignment(Qt::AlignCenter);

    m_searchInput = new QLineEdit(m_searchBar);
    m_searchInput->setPlaceholderText(QStringLiteral("搜索聊天记录..."));
    m_searchInput->setFixedHeight(28);
    m_searchInput->setStyleSheet(QStringLiteral(
        "QLineEdit { border: 1px solid #ccc; border-radius: 4px; padding: 2px 6px; }"
        "QLineEdit:focus { border-color: #999; }"
    ));

    m_searchPrevBtn = new QPushButton(QStringLiteral("▲"), m_searchBar);
    m_searchPrevBtn->setFixedSize(26, 26);
    m_searchPrevBtn->setEnabled(false);

    m_searchNextBtn = new QPushButton(QStringLiteral("▼"), m_searchBar);
    m_searchNextBtn->setFixedSize(26, 26);
    m_searchNextBtn->setEnabled(false);

    m_searchCountLabel = new QLabel(QStringLiteral("0/0"), m_searchBar);
    m_searchCountLabel->setFixedWidth(50);
    m_searchCountLabel->setAlignment(Qt::AlignCenter);
    m_searchCountLabel->setStyleSheet(QStringLiteral("color: gray; font-size: 12px;"));

    auto *searchCloseBtn = new QPushButton(QStringLiteral("✕"), m_searchBar);
    searchCloseBtn->setFixedSize(22, 22);
    searchCloseBtn->setCursor(Qt::PointingHandCursor);
    searchCloseBtn->setStyleSheet(QStringLiteral("QPushButton { border: none; color: #999; font-size: 13px; }"
                                                  "QPushButton:hover { color: #333; }"));

    searchLayout->addWidget(searchIcon);
    searchLayout->addWidget(m_searchInput, 1);
    searchLayout->addWidget(m_searchPrevBtn);
    searchLayout->addWidget(m_searchNextBtn);
    searchLayout->addWidget(m_searchCountLabel);
    searchLayout->addWidget(searchCloseBtn);

    m_searchBar->setVisible(false);

    // ========== 消息区域（QStackedWidget） ==========
    m_messageStack = new QStackedWidget(this);
    m_messageStack->setStyleSheet(QStringLiteral("background: transparent;"));

    // ---- Page 0: 正常聊天会话（QScrollArea + 消息列表） ----
    m_scrollArea = new QScrollArea();
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
    ));
    // 让 viewport 背景透明，使窗口背景图（paintEvent）或系统主题可穿透
    m_scrollArea->viewport()->setAutoFillBackground(false);

    m_conversationContainer = new QWidget();
    m_conversationContainer->setStyleSheet(QStringLiteral("background: transparent;"));
    m_messageListLayout = new QVBoxLayout(m_conversationContainer);
    m_messageListLayout->setContentsMargins(0, 0, 0, 0);
    m_messageListLayout->setSpacing(0);
    // 底部伸缩项：当消息较少时让消息靠上排列
    m_messageListLayout->addStretch();

    m_scrollArea->setWidget(m_conversationContainer);
    m_messageStack->addWidget(m_scrollArea);   // index 0

    // ---- Page 1: 搜索结果视图（QTextEdit，复用旧搜索逻辑） ----
    m_searchResultView = new QTextEdit();
    m_searchResultView->setReadOnly(true);
    m_searchResultView->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; border: none; padding: 8px; }"
    ));
    m_searchResultView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_searchResultView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageStack->addWidget(m_searchResultView);  // index 1

    m_messageStack->setCurrentIndex(0);

    // ========== 输入区 ==========
    m_inputEdit = new QTextEdit(this);
    m_inputEdit->setPlaceholderText(QStringLiteral("说点什么吧... （Shift+Enter换行，Enter发送）"));
    m_inputEdit->setFixedHeight(36);
    m_inputEdit->setMaximumHeight(150);
    m_inputEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_inputEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inputEdit->setAcceptRichText(false);
    // 无自定义 stylesheet — 使用系统原生样式，自动适配浅色/深色主题
    // 安装事件过滤器以捕获 Enter 键发送
    m_inputEdit->installEventFilter(this);

    // 输入框自动增高
    connect(m_inputEdit, &QTextEdit::textChanged, this, &ChatWindow::adjustInputEditHeight);

    m_fileButton = new QPushButton(QStringLiteral("📎"), this);
    m_fileButton->setFixedSize(36, 36);
    m_fileButton->setCursor(Qt::PointingHandCursor);

    m_sendButton = new QPushButton(QStringLiteral("发送"), this);
    m_sendButton->setFixedHeight(36);
    m_sendButton->setCursor(Qt::PointingHandCursor);

    auto *inputLayout = new QHBoxLayout();
    inputLayout->setContentsMargins(8, 4, 8, 8);
    inputLayout->setSpacing(6);
    inputLayout->addWidget(m_inputEdit);
    inputLayout->addWidget(m_fileButton);
    inputLayout->addWidget(m_sendButton);

    // ========== 主布局 ==========
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addLayout(toolbarLayout);
    mainLayout->addWidget(m_searchBar);
    mainLayout->addWidget(m_messageStack, 1);
    mainLayout->addLayout(inputLayout);
    setLayout(mainLayout);

    // ========== 信号连接 ==========
    connect(m_sendButton, &QPushButton::clicked, this, &ChatWindow::onSendClicked);
    connect(m_fileButton, &QPushButton::clicked, this, &ChatWindow::onFileClicked);

    connect(m_searchToggleBtn, &QPushButton::clicked, this, &ChatWindow::onSearchToggled);
    connect(searchCloseBtn, &QPushButton::clicked, this, &ChatWindow::onSearchToggled);
    connect(m_searchInput, &QLineEdit::returnPressed, this, &ChatWindow::onSearchNext);
    connect(m_searchPrevBtn, &QPushButton::clicked, this, &ChatWindow::onSearchPrev);
    connect(m_searchNextBtn, &QPushButton::clicked, this, &ChatWindow::onSearchNext);

    connect(m_exportBtn, &QPushButton::clicked, this, &ChatWindow::onExportClicked);
    connect(m_clearBtn, &QPushButton::clicked, this, &ChatWindow::onClearClicked);
}

// ============================================================================
// 基本功能
// ============================================================================

void ChatWindow::setCharacterName(const QString &name)
{
    m_characterName = name;
    setWindowTitle(QStringLiteral("和 %1 聊天～").arg(name));
}

void ChatWindow::showAndFocus()
{
    if (isMinimized()) {
        showNormal();
    } else if (isHidden()) {
        show();
        if (isMaximized()) {
            showNormal();
        }
    }
    raise();
    activateWindow();
    m_inputEdit->setFocus();
}

void ChatWindow::closeEvent(QCloseEvent *event)
{
    if (!isMaximized() && !isFullScreen()) {
        m_normalGeometry = geometry();
        QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("ChatWindow"));
        settings.setValue(QStringLiteral("geometry/normal"), m_normalGeometry);
    }
    hide();
    event->accept();
}

void ChatWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange) {
        const bool wasMaximized = windowState() & Qt::WindowMaximized;
        const bool wasFullScreen = windowState() & Qt::WindowFullScreen;

        QWidget::changeEvent(event);

        const bool isMaximized = windowState() & Qt::WindowMaximized;
        const bool isFullScreen = windowState() & Qt::WindowFullScreen;

        if ((isMaximized || isFullScreen) && !wasMaximized && !wasFullScreen) {
            m_normalGeometry = geometry();
            qInfo() << "[ChatWindow] 保存全屏前几何:" << m_normalGeometry;
        } else if (!isMaximized && !isFullScreen && (wasMaximized || wasFullScreen)) {
            qInfo() << "[ChatWindow] 恢复几何:" << m_normalGeometry;
            if (m_normalGeometry.isValid()) {
                setGeometry(m_normalGeometry);
            }
        }
        return;
    }
    QWidget::changeEvent(event);
}

void ChatWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateBubbleMaxWidth();
}

bool ChatWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputEdit) {
        // 跟踪输入法组合状态：Enter 仅在组合结束后发送
        if (event->type() == QEvent::InputMethod) {
            auto *imeEvent = static_cast<QInputMethodEvent *>(event);
            m_imeComposing = !imeEvent->preeditString().isEmpty();
            return false; // 让输入法继续处理
        }
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                    if (m_imeComposing) {
                        return false; // 输入法正在组合，Enter 用于提交候选词，不发送
                    }
                    onSendClicked();
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void ChatWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (!m_backgroundImage.isNull()) {
        const QSize winSize = rect().size();

        // ── ① 先垫一层深色底色 ──
        const int darkBase = 24;
        painter.fillRect(rect(), QColor(darkBase, darkBase, darkBase));

        // ── ② cover 模式缩放并居中裁剪背景图 ──
        const QSize imgSize = m_backgroundImage.size();
        const double imgAspect = static_cast<double>(imgSize.width()) / imgSize.height();
        const double winAspect = static_cast<double>(winSize.width()) / winSize.height();

        QPixmap scaled;
        QRect sourceRect;
        if (imgAspect > winAspect) {
            scaled = m_backgroundImage.scaledToHeight(winSize.height(), Qt::SmoothTransformation);
            const int cropX = (scaled.width() - winSize.width()) / 2;
            sourceRect = QRect(cropX, 0, winSize.width(), winSize.height());
        } else {
            scaled = m_backgroundImage.scaledToWidth(winSize.width(), Qt::SmoothTransformation);
            const int cropY = (scaled.height() - winSize.height()) / 2;
            sourceRect = QRect(0, cropY, winSize.width(), winSize.height());
        }

        painter.setOpacity(m_backgroundOpacity);
        painter.drawPixmap(rect(), scaled, sourceRect);
        painter.setOpacity(1.0);
    } else {
        painter.fillRect(rect(), palette().window());
    }

    QWidget::paintEvent(event);
}

void ChatWindow::onSendClicked()
{
    const QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty()) return;

    appendUserMessage(text);
    m_inputEdit->clear();
    m_inputEdit->setFixedHeight(36);
    emit userMessageSent(text);
}

void ChatWindow::onFileClicked()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, QStringLiteral("导入文本文件"), QString(),
        QStringLiteral("文本文件 (*.txt *.md *.cpp *.h *.json *.xml *.py *.js *.html *.css);;所有文件 (*)")
    );
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_inputEdit->setPlainText(QStringLiteral("（文件打开失败：%1）").arg(filePath));
        return;
    }
    if (file.size() > 512 * 1024) {
        m_inputEdit->setPlainText(QStringLiteral("（文件超过 512KB，请缩小后再试）"));
        return;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    const QString content = stream.readAll();
    file.close();

    const QString existing = m_inputEdit->toPlainText();
    if (existing.isEmpty()) {
        m_inputEdit->setPlainText(content);
    } else {
        m_inputEdit->setPlainText(existing + QStringLiteral("\n") + content);
    }
    m_inputEdit->setFocus();
}

// ============================================================================
// 消息控件创建
// ============================================================================

QWidget *ChatWindow::createUserMessageRow(const QString &text)
{
    auto *row = new QWidget(m_conversationContainer);
    row->setObjectName(QStringLiteral("userMessageRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->setSpacing(0);

    const int maxBubbleW = m_scrollArea->viewport()
        ? static_cast<int>(m_scrollArea->viewport()->width() * 0.75)
        : 300;

    auto *bubble = new QLabel(row);
    bubble->setObjectName(QStringLiteral("userBubble"));
    bubble->setWordWrap(true);
    bubble->setTextFormat(Qt::PlainText);
    bubble->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    bubble->setMaximumWidth(maxBubbleW);
    // 无气泡背景，纯文字 — 旧版风格，系统主题自动穿透
    bubble->setStyleSheet(QStringLiteral(
        "QLabel { font-size: 15px; }"
    ));
    bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    bubble->setText(text);

    layout->addStretch();
    layout->addWidget(bubble);

    return row;
}

QWidget *ChatWindow::createAssistantBlock(const QString &text)
{
    auto *block = new QWidget(m_conversationContainer);
    block->setObjectName(QStringLiteral("assistantBlock"));
    auto *hLayout = new QHBoxLayout(block);
    hLayout->setContentsMargins(12, 4, 12, 4);
    hLayout->setSpacing(0);

    auto *inner = new QWidget(block);
    auto *vLayout = new QVBoxLayout(inner);
    vLayout->setContentsMargins(0, 0, 0, 0);
    vLayout->setSpacing(2);

    // 回复正文
    auto *content = new QTextEdit(inner);
    content->setObjectName(QStringLiteral("assistantContent"));
    content->setReadOnly(true);
    content->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    content->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; border: none; padding: 0; "
        "font-size: 15px; line-height: 1.8; }"
    ));
    content->document()->setDocumentMargin(0);

    // 设置文字
    content->setPlainText(text);

    // 自动调整高度
    connect(content->document(), &QTextDocument::contentsChanged, content, [content]() {
        autoResizeTextEdit(content);
    });
    // 初始调整
    QTimer::singleShot(0, content, [content]() {
        autoResizeTextEdit(content);
    });

    vLayout->addWidget(content);

    hLayout->addWidget(inner);
    hLayout->addStretch();

    return block;
}

QWidget *ChatWindow::createSystemMessageRow(const QString &text)
{
    auto *row = new QWidget(m_conversationContainer);
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->setAlignment(Qt::AlignCenter);

    auto *label = new QLabel(row);
    label->setTextFormat(Qt::RichText);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("color: gray; font-size: 13px; padding: 8px 0;"));
    label->setWordWrap(true);

    // 匹配原 appendSystemMessage 的居中样式
    label->setText(QStringLiteral(
        "<div style='text-align:center; margin:8px 0;'>"
        "<span style='color:gray; font-size:13px;'>%1</span>"
        "</div>"
    ).arg(text.toHtmlEscaped()));

    layout->addWidget(label);
    return row;
}

// ============================================================================
// 消息显示
// ============================================================================

void ChatWindow::appendUserMessage(const QString &text)
{
    // 切换到会话页（如果当前在搜索页）
    if (m_messageStack->currentIndex() != 0) {
        m_messageStack->setCurrentIndex(0);
    }

    QWidget *row = createUserMessageRow(text);
    // 插入到底部伸缩项之前
    m_messageListLayout->insertWidget(m_messageListLayout->count() - 1, row);

    scrollToBottom();
}

void ChatWindow::appendAssistantMessage(const QString &text)
{
    // 用于加载历史记录（非流式）
    QWidget *block = createAssistantBlock(text);
    m_messageListLayout->insertWidget(m_messageListLayout->count() - 1, block);

    scrollToBottom();
}

void ChatWindow::appendSystemMessage(const QString &text)
{
    QWidget *row = createSystemMessageRow(text);
    m_messageListLayout->insertWidget(m_messageListLayout->count() - 1, row);

    scrollToBottom();
}

void ChatWindow::updateAssistantMessage(const QString &text)
{
    if (!m_isStreaming) {
        // ── 首次流式更新：创建回复块 ──
        m_messageStack->setCurrentIndex(0);

        auto *block = new QWidget(m_conversationContainer);
        block->setObjectName(QStringLiteral("assistantBlock"));
        auto *hLayout = new QHBoxLayout(block);
        hLayout->setContentsMargins(12, 4, 12, 4);
        hLayout->setSpacing(0);

        auto *inner = new QWidget(block);
        auto *vLayout = new QVBoxLayout(inner);
        vLayout->setContentsMargins(0, 0, 0, 0);
        vLayout->setSpacing(2);

        // 回复正文（初始为空，流式追加）
        auto *content = new QTextEdit(inner);
        content->setObjectName(QStringLiteral("assistantContent"));
        content->setReadOnly(true);
        content->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        content->setStyleSheet(QStringLiteral(
            "QTextEdit { background: transparent; border: none; padding: 0; "
            "font-size: 15px; line-height: 1.8; }"
        ));
        content->document()->setDocumentMargin(0);

        // 自动调整高度
        connect(content->document(), &QTextDocument::contentsChanged, content, [content]() {
            autoResizeTextEdit(content);
        });

        vLayout->addWidget(content);

        hLayout->addWidget(inner);
        hLayout->addStretch();

        // 插入到底部伸缩项之前
        m_messageListLayout->insertWidget(m_messageListLayout->count() - 1, block);

        // 记录当前 assistant 块
        m_currentAssistant.container   = block;
        m_currentAssistant.contentEdit = content;

        m_isStreaming = true;
        m_lastStreamingText.clear();

        // 放入初始文字
        if (!text.isEmpty()) {
            m_currentAssistant.contentEdit->setPlainText(text);
            m_lastStreamingText = text;
        }

        scrollToBottom();
        return;
    }

    // ── 后续流式更新 ──
    if (!m_currentAssistant.contentEdit) return;

    // 传入完整累计文本
    if (text.length() > m_lastStreamingText.length() && text.startsWith(m_lastStreamingText)) {
        // 正常流式追加：只插入新增后缀
        const QString delta = text.mid(m_lastStreamingText.length());
        QTextCursor cursor(m_currentAssistant.contentEdit->document());
        cursor.movePosition(QTextCursor::End);
        cursor.insertText(delta);
    } else if (text != m_lastStreamingText) {
        // 回退/重试：以传入文本为权威，完整替换
        m_currentAssistant.contentEdit->setPlainText(text);
        QTextCursor cursor(m_currentAssistant.contentEdit->document());
        cursor.movePosition(QTextCursor::End);
    }
    m_lastStreamingText = text;

    scrollToBottom();
}

void ChatWindow::finishAssistantMessage()
{
    if (!m_isStreaming) return;

    m_isStreaming = false;
    m_currentAssistant = AssistantBlock{};
    m_lastStreamingText.clear();
}

void ChatWindow::loadHistoryMessages(const QStringList &userOrAssistantRoles,
                                     const QStringList &contents)
{
    clearMessageWidgets();
    m_isStreaming = false;
    m_currentAssistant = AssistantBlock{};
    m_lastStreamingText.clear();

    const int count = qMin(userOrAssistantRoles.size(), contents.size());
    for (int i = 0; i < count; ++i) {
        if (userOrAssistantRoles.at(i) == QStringLiteral("user")) {
            QWidget *row = createUserMessageRow(contents.at(i));
            m_messageListLayout->insertWidget(m_messageListLayout->count() - 1, row);
        } else {
            QWidget *block = createAssistantBlock(contents.at(i));
            m_messageListLayout->insertWidget(m_messageListLayout->count() - 1, block);
        }
    }

    QTimer::singleShot(0, this, &ChatWindow::scrollToBottom);
}

// ============================================================================
// 布局辅助
// ============================================================================

void ChatWindow::scrollToBottom()
{
    QScrollBar *sb = m_scrollArea->verticalScrollBar();
    if (sb) {
        sb->setValue(sb->maximum());
    }
}

void ChatWindow::adjustInputEditHeight()
{
    if (!m_inputEdit) return;

    m_inputEdit->document()->setTextWidth(m_inputEdit->viewport()->width());
    const int docHeight = static_cast<int>(m_inputEdit->document()->size().height());
    const int frameH = m_inputEdit->frameWidth() * 2;
    const int newHeight = qMax(36, qMin(docHeight + frameH + 4, 150));
    m_inputEdit->setFixedHeight(newHeight);
}

void ChatWindow::updateBubbleMaxWidth()
{
    if (!m_scrollArea || !m_scrollArea->viewport()) return;
    const int maxW = static_cast<int>(m_scrollArea->viewport()->width() * 0.75);

    // 遍历所有消息行，更新用户气泡最大宽度
    for (int i = 0; i < m_messageListLayout->count(); ++i) {
        QLayoutItem *item = m_messageListLayout->itemAt(i);
        if (!item || !item->widget()) continue;
        auto *bubble = item->widget()->findChild<QLabel *>(QStringLiteral("userBubble"));
        if (bubble) {
            bubble->setMaximumWidth(maxW);
        }
    }
}

void ChatWindow::clearMessageWidgets()
{
    // 移除除底部伸缩项外的所有子控件
    while (m_messageListLayout->count() > 1) {
        QLayoutItem *item = m_messageListLayout->takeAt(0);
        if (item->widget()) {
            item->widget()->deleteLater();
        }
        delete item;
    }
}

// ============================================================================
// 搜索功能
// ============================================================================

void ChatWindow::onSearchToggled()
{
    if (m_searchBar->isVisible()) {
        // 收起：切换到会话页
        m_searchResults.clear();
        m_searchIndex = -1;
        m_searchCountLabel->setText(QStringLiteral("0/0"));
        m_searchPrevBtn->setEnabled(false);
        m_searchNextBtn->setEnabled(false);

        auto *anim = new QPropertyAnimation(m_searchBar, "maximumHeight");
        anim->setDuration(180);
        anim->setStartValue(m_searchBar->height());
        anim->setEndValue(0);
        anim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            m_searchBar->setVisible(false);
            m_searchBar->setMaximumHeight(QWIDGETSIZE_MAX);
            // 切换回会话页并重新加载历史
            m_messageStack->setCurrentIndex(0);
            m_searchResultView->clear();
            if (m_memoryDb && !m_characterName.isEmpty()) {
                const auto history = m_memoryDb->loadHistory(
                    m_characterKey.isEmpty() ? m_characterName : m_characterKey, 200);
                QStringList roles, contents;
                for (const auto &rec : history) {
                    roles.append(rec.role);
                    contents.append(rec.content);
                }
                loadHistoryMessages(roles, contents);
            }
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        // 展开
        m_searchBar->setMaximumHeight(0);
        m_searchBar->setVisible(true);
        m_searchBar->adjustSize();
        const int targetH = m_searchBar->sizeHint().height();

        // 切换到搜索结果页
        m_messageStack->setCurrentIndex(1);

        auto *anim = new QPropertyAnimation(m_searchBar, "maximumHeight");
        anim->setDuration(200);
        anim->setStartValue(0);
        anim->setEndValue(targetH);
        anim->setEasingCurve(QEasingCurve::OutQuad);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            m_searchBar->setMaximumHeight(QWIDGETSIZE_MAX);
            m_searchInput->setFocus();
            m_searchInput->selectAll();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void ChatWindow::doSearch()
{
    const QString keyword = m_searchInput->text().trimmed();
    if (keyword.isEmpty() || !m_memoryDb) {
        m_searchResults.clear();
        m_searchIndex = -1;
        m_searchCountLabel->setText(QStringLiteral("0/0"));
        m_searchPrevBtn->setEnabled(false);
        m_searchNextBtn->setEnabled(false);
        return;
    }

    m_searchResults = m_memoryDb->searchMessages(
        keyword, m_characterKey.isEmpty() ? m_characterName : m_characterKey);
    m_searchIndex = m_searchResults.isEmpty() ? -1 : 0;

    m_searchCountLabel->setText(QStringLiteral("%1/%2")
        .arg(m_searchIndex + 1).arg(m_searchResults.size()));
    m_searchPrevBtn->setEnabled(m_searchResults.size() > 1);
    m_searchNextBtn->setEnabled(m_searchResults.size() > 1);

    if (!m_searchResults.isEmpty()) {
        highlightSearchResult(0);
    } else {
        m_searchResultView->clear();
        // 使用系统消息样式在搜索页显示未找到
        m_searchResultView->setHtml(QStringLiteral(
            "<div style='text-align:center; margin-top:20px; color:gray; font-size:13px;'>"
            "没有找到包含「%1」的消息</div>"
        ).arg(keyword.toHtmlEscaped()));
    }
}

void ChatWindow::clearSearchHighlight()
{
    m_searchResultView->setExtraSelections({});
}

void ChatWindow::highlightSearchResult(int index)
{
    if (index < 0 || index >= m_searchResults.size()) return;

    const auto &rec = m_searchResults.at(index);

    m_searchResultView->clear();

    // 结果提示
    m_searchResultView->append(QStringLiteral(
        "<div style='text-align:center; margin:6px 0;'>"
        "<span style='color:gray; font-size:12px;'>"
        "搜索结果 %1/%2  ——  匹配：%3"
        "</span></div>"
    ).arg(index + 1).arg(m_searchResults.size())
     .arg(m_searchInput->text().toHtmlEscaped()));

    // 角色标识
    const QString roleLabel = (rec.role == QStringLiteral("user"))
        ? QStringLiteral("👤 你")
        : QStringLiteral("🎀 %1").arg(m_characterName);
    const QString roleColor = (rec.role == QStringLiteral("user"))
        ? QStringLiteral("#4a90d9") : QStringLiteral("#d94a8e");

    m_searchResultView->append(QStringLiteral(
        "<div style='font-size:12px; color:%2; margin-bottom:2px;'>%1</div>"
        "<div style='font-size:15px; line-height:1.8; padding:6px 10px; "
        "background:#fff3cd; border-radius:6px;'>%3</div>"
        "<div style='font-size:11px; color:gray; margin-top:2px;'>%4</div>"
    ).arg(roleLabel, roleColor, rec.content.toHtmlEscaped(), rec.timestamp));

    // 光标移动到顶部
    m_searchResultView->moveCursor(QTextCursor::Start);

    // 更新计数
    m_searchIndex = index;
    m_searchCountLabel->setText(QStringLiteral("%1/%2")
        .arg(index + 1).arg(m_searchResults.size()));
}

void ChatWindow::onSearchNext()
{
    if (m_searchResults.isEmpty()) {
        doSearch();
        return;
    }
    int next = m_searchIndex + 1;
    if (next >= m_searchResults.size()) next = 0;
    highlightSearchResult(next);
}

void ChatWindow::onSearchPrev()
{
    if (m_searchResults.isEmpty()) return;
    int prev = m_searchIndex - 1;
    if (prev < 0) prev = m_searchResults.size() - 1;
    highlightSearchResult(prev);
}

// ============================================================================
// 导出功能
// ============================================================================

void ChatWindow::onExportClicked()
{
    QMenu menu(this);
    auto *txtAction  = menu.addAction(QStringLiteral("导出为 .txt"));
    auto *jsonAction = menu.addAction(QStringLiteral("导出为 .json"));
    auto *htmlAction = menu.addAction(QStringLiteral("导出为 .html"));

    QAction *chosen = menu.exec(m_exportBtn->mapToGlobal(QPoint(0, m_exportBtn->height())));
    if (!chosen) return;

    QString filter;
    QString suffix;
    if (chosen == txtAction) {
        filter = QStringLiteral("文本文件 (*.txt)");
        suffix = QStringLiteral("txt");
    } else if (chosen == jsonAction) {
        filter = QStringLiteral("JSON 文件 (*.json)");
        suffix = QStringLiteral("json");
    } else if (chosen == htmlAction) {
        filter = QStringLiteral("HTML 文件 (*.html)");
        suffix = QStringLiteral("html");
    } else {
        return;
    }

    const QString filePath = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出聊天记录"),
        QStringLiteral("聊天记录_%1.%2").arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")), suffix),
        filter
    );
    if (filePath.isEmpty()) return;

    if (chosen == txtAction) exportAsTxt(filePath);
    else if (chosen == jsonAction) exportAsJson(filePath);
    else if (chosen == htmlAction) exportAsHtml(filePath);
}

void ChatWindow::exportAsTxt(const QString &filePath)
{
    if (!m_memoryDb) return;
    const auto all = m_memoryDb->loadAllMessages(m_characterKey.isEmpty() ? m_characterName : m_characterKey);
    if (all.isEmpty()) {
        appendSystemMessage(QStringLiteral("没有可导出的聊天记录"));
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendSystemMessage(QStringLiteral("导出失败：无法写入文件"));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << QStringLiteral("=== 与 %1 的聊天记录 ===\n\n").arg(m_characterName);
    for (const auto &rec : all) {
        const QString roleLabel = (rec.role == QStringLiteral("user"))
            ? QStringLiteral("你") : m_characterName;
        out << QStringLiteral("[%1] %2\n%3\n\n").arg(rec.timestamp, roleLabel, rec.content);
    }
    file.close();

    appendSystemMessage(QStringLiteral("已导出 %1 条聊天记录到 .txt 文件").arg(all.size()));
    qInfo() << "[ChatWindow] 导出 .txt：" << filePath;
}

void ChatWindow::exportAsJson(const QString &filePath)
{
    if (!m_memoryDb) return;
    const auto all = m_memoryDb->loadAllMessages(m_characterKey.isEmpty() ? m_characterName : m_characterKey);
    if (all.isEmpty()) {
        appendSystemMessage(QStringLiteral("没有可导出的聊天记录"));
        return;
    }

    QJsonArray arr;
    for (const auto &rec : all) {
        QJsonObject obj;
        obj[QStringLiteral("role")]      = rec.role;
        obj[QStringLiteral("content")]   = rec.content;
        obj[QStringLiteral("timestamp")] = rec.timestamp;
        arr.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("character")] = m_characterName;
    root[QStringLiteral("messages")]  = arr;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendSystemMessage(QStringLiteral("导出失败：无法写入文件"));
        return;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    appendSystemMessage(QStringLiteral("已导出 %1 条聊天记录到 .json 文件").arg(all.size()));
    qInfo() << "[ChatWindow] 导出 .json：" << filePath;
}

void ChatWindow::exportAsHtml(const QString &filePath)
{
    if (!m_memoryDb) return;
    const auto all = m_memoryDb->loadAllMessages(m_characterKey.isEmpty() ? m_characterName : m_characterKey);
    if (all.isEmpty()) {
        appendSystemMessage(QStringLiteral("没有可导出的聊天记录"));
        return;
    }

    QStringList bodyParts;
    bodyParts << QStringLiteral("<h2 style='text-align:center; color:#333;'>与 %1 的聊天记录</h2>")
        .arg(m_characterName.toHtmlEscaped());

    for (const auto &rec : all) {
        const bool isUser = (rec.role == QStringLiteral("user"));
        const QString align  = isUser ? QStringLiteral("right") : QStringLiteral("left");
        const QString color  = isUser ? QStringLiteral("#d9e8ff") : QStringLiteral("#f0d9ff");
        const QString name   = isUser ? QStringLiteral("你") : m_characterName.toHtmlEscaped();

        bodyParts << QStringLiteral(
            "<div style='text-align:%1; margin:10px 20px;'>"
            "  <div style='font-size:12px; color:gray;'>%2</div>"
            "  <div style='display:inline-block; background:%3; padding:8px 14px; "
            "border-radius:10px; font-size:15px; line-height:1.7; max-width:70%%;'>"
            "    %4"
            "  </div>"
            "</div>"
        ).arg(align, rec.timestamp, color, rec.content.toHtmlEscaped());
    }

    const QString html = QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<title>与 %1 的聊天记录</title>"
        "<style>body { font-family: -apple-system, 'Microsoft YaHei', sans-serif; "
        "max-width: 800px; margin: 20px auto; padding: 0 20px; }</style>"
        "</head><body>%2</body></html>"
    ).arg(m_characterName.toHtmlEscaped(), bodyParts.join(QStringLiteral("\n")));

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        appendSystemMessage(QStringLiteral("导出失败：无法写入文件"));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << html;
    file.close();

    appendSystemMessage(QStringLiteral("已导出 %1 条聊天记录到 .html 文件").arg(all.size()));
    qInfo() << "[ChatWindow] 导出 .html：" << filePath;
}

// ============================================================================
// 清除聊天记录
// ============================================================================

void ChatWindow::onClearClicked()
{
    const auto result = QMessageBox::question(
        this,
        QStringLiteral("清除聊天记录"),
        QStringLiteral("确定要删除与 %1 的所有聊天记录吗？\n此操作不可撤销。").arg(m_characterName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (result != QMessageBox::Yes) return;

    clearMessageWidgets();
    m_searchResults.clear();
    m_searchIndex = -1;
    m_isStreaming = false;
    m_currentAssistant = AssistantBlock{};
    m_lastStreamingText.clear();

    emit clearRequested();
}

// ============================================================================
// 🏞️ 聊天背景图
// ============================================================================

void ChatWindow::setBackgroundImage(const QString &filePath)
{
    if (filePath.isEmpty()) {
        clearBackground();
        return;
    }

    QPixmap pixmap(filePath);
    if (pixmap.isNull()) {
        qWarning() << "[ChatWindow] 无法加载背景图：" << filePath;
        return;
    }

    m_backgroundImage = pixmap;
    m_backgroundImagePath = filePath;
    m_backgroundSourcePath = filePath;
    update();

    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("ChatWindow"));
    settings.setValue(QStringLiteral("background/path"), m_backgroundImagePath);
    settings.setValue(QStringLiteral("background/sourcePath"), m_backgroundSourcePath);

    qInfo() << "[ChatWindow] 已设置背景图：" << filePath
            << "尺寸：" << pixmap.width() << "x" << pixmap.height();
}

void ChatWindow::setBackgroundCropped(const QString &croppedPath, const QString &sourcePath)
{
    QPixmap pixmap(croppedPath);
    if (pixmap.isNull()) {
        qWarning() << "[ChatWindow] 无法加载裁剪图：" << croppedPath;
        return;
    }

    m_backgroundImage = pixmap;
    m_backgroundImagePath = croppedPath;
    m_backgroundSourcePath = sourcePath;
    update();

    QSettings settings2(QStringLiteral("DesktopPet"), QStringLiteral("ChatWindow"));
    settings2.setValue(QStringLiteral("background/path"), m_backgroundImagePath);
    settings2.setValue(QStringLiteral("background/sourcePath"), m_backgroundSourcePath);

    qInfo() << "[ChatWindow] 已应用裁剪背景，原始图：" << sourcePath;
}

void ChatWindow::clearBackground()
{
    m_backgroundImage = QPixmap();
    m_backgroundImagePath.clear();
    m_backgroundSourcePath.clear();
    update();

    QSettings clrSettings(QStringLiteral("DesktopPet"), QStringLiteral("ChatWindow"));
    clrSettings.setValue(QStringLiteral("background/path"), QString());
    clrSettings.setValue(QStringLiteral("background/sourcePath"), QString());
}

void ChatWindow::setBackgroundOpacity(double opacity)
{
    m_backgroundOpacity = qBound(0.0, opacity, 1.0);
    update();

    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("ChatWindow"))
        .setValue(QStringLiteral("background/opacity"), m_backgroundOpacity);
}
