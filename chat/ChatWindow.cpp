#include "ChatWindow.h"
#include "../memory/MemoryDB.h"

#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCloseEvent>
#include <QDateTime>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollBar>
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
#include <QKeyEvent>
#include <QSettings>
#include <QEvent>
#include <QTimer>
#include <QMessageBox>

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
}

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

    // 布局稳定后执行一次输入框高度修正，防初始 150px 问题
    QTimer::singleShot(0, this, [this]() {
        if (m_inputEdit) m_inputEdit->setFixedHeight(36);
    });
}

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

    // ========== 历史记录区 ==========
    m_historyView = new QTextEdit(this);
    m_historyView->setReadOnly(true);
    m_historyView->setStyleSheet(QStringLiteral(
        "QTextEdit { background: transparent; border: none; padding: 8px; }"
    ));
    m_historyView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_historyView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_historyView->setAlignment(Qt::AlignLeft);

    // ========== 临时状态文字（风格切换用，不进入聊天记录） ==========
    m_transientStatus = new QLabel(this);
    m_transientStatus->setAlignment(Qt::AlignCenter);
    m_transientStatus->setWordWrap(true);
    m_transientStatus->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #888;"
        "  font-size: 13px;"
        "  padding: 4px 8px;"
        "  background: transparent;"
        "}"
    ));
    m_transientStatus->setVisible(false);

    // ========== 输入区 ==========
    m_inputEdit = new QTextEdit(this);
    m_inputEdit->setPlaceholderText(QStringLiteral("说点什么吧... （Shift+Enter换行，Enter发送）"));
    m_inputEdit->setFixedHeight(36);
    m_inputEdit->setMaximumHeight(150);
    m_inputEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_inputEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_inputEdit->setAcceptRichText(false);
    m_inputEdit->installEventFilter(this);

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
    mainLayout->addWidget(m_historyView, 1);
    mainLayout->addWidget(m_transientStatus);
    mainLayout->addLayout(inputLayout);
    setLayout(mainLayout);

    // ========== 信号连接 ==========
    connect(m_sendButton, &QPushButton::clicked, this, &ChatWindow::onSendClicked);
    connect(m_inputEdit, &QTextEdit::textChanged, this, &ChatWindow::adjustInputEditHeight);
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
        showNormal();       // 从最小化恢复正常
    } else if (isHidden()) {
        show();             // 从隐藏（X 掉）恢复显示
        if (isMaximized()) {
            showNormal();   // 如果之前是最大化后隐藏，恢复到普通状态
        }
    }
    raise();
    activateWindow();
    m_inputEdit->setFocus();
}

void ChatWindow::closeEvent(QCloseEvent *event)
{
    // 保存当前正常几何（退出时记录，下次打开恢复用）
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

        // 调用基类先更新状态
        QWidget::changeEvent(event);

        const bool isMaximized = windowState() & Qt::WindowMaximized;
        const bool isFullScreen = windowState() & Qt::WindowFullScreen;

        if ((isMaximized || isFullScreen) && !wasMaximized && !wasFullScreen) {
            // 即将全屏/最大化：保存当前几何
            m_normalGeometry = geometry();
            qInfo() << "[ChatWindow] 保存全屏前几何:" << m_normalGeometry;
        } else if (!isMaximized && !isFullScreen && (wasMaximized || wasFullScreen)) {
            // 从全屏/最大化恢复：强制恢复保存的几何
            qInfo() << "[ChatWindow] 恢复几何:" << m_normalGeometry;
            if (m_normalGeometry.isValid()) {
                setGeometry(m_normalGeometry);
            }
        }
        return; // 基类已调用
    }
    QWidget::changeEvent(event);
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
                    if (!m_inputEnabled) return true; // 输入已禁用
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
        //     让图片淡出时向深色过渡，而不是向 Widget 默认的白色过渡
        //     这样 m_backgroundOpacity 调节才像真正的"透明度/昏暗"，而不是"褪色到白"
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
        // 无背景图时保持默认浅色背景
        painter.fillRect(rect(), palette().window());
    }

    // 让 QWidget 继续绘制子控件
    QWidget::paintEvent(event);
}

void ChatWindow::adjustInputEditHeight()
{
    if (!m_inputEdit) return;

    // 空输入直接保持最小高度，避免文档布局未就绪时计算错误
    if (m_inputEdit->toPlainText().isEmpty()) {
        m_inputEdit->setFixedHeight(36);
        return;
    }

    // viewport 宽度尚未生效时也保持最小高度
    const int vpW = m_inputEdit->viewport()->width();
    if (vpW <= 0) {
        m_inputEdit->setFixedHeight(36);
        return;
    }

    m_inputEdit->document()->setTextWidth(vpW);
    const int docHeight = static_cast<int>(m_inputEdit->document()->size().height());
    const int frameH = m_inputEdit->frameWidth() * 2;
    const int newHeight = qMax(36, qMin(docHeight + frameH + 4, 150));
    m_inputEdit->setFixedHeight(newHeight);
}

void ChatWindow::onSendClicked()
{
    if (!m_inputEnabled) return;
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
// 消息显示
// ============================================================================

void ChatWindow::appendFormattedLine(const QString &html, Qt::Alignment align)
{
    QTextCursor cursor = m_historyView->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextDocument *doc = m_historyView->document();
    if (doc && !doc->isEmpty()) {
        QTextBlockFormat sepFmt;
        cursor.insertBlock(sepFmt);
    }

    cursor.insertHtml(html);

    QTextBlockFormat msgFmt;
    msgFmt.setAlignment(align);
    msgFmt.setBottomMargin(30);
    cursor.mergeBlockFormat(msgFmt);

    QScrollBar *sb = m_historyView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

QString ChatWindow::buildAssistantBubble(const QString &, const QString &text)
{
    return QStringLiteral(
        "<div style='font-size:15px; line-height:1.8; padding:2px 0;'>%1</div>"
    ).arg(text.toHtmlEscaped());
}

QString ChatWindow::buildUserBubble(const QString &, const QString &text)
{
    // 使用 table 实现：块级右对齐将整个表格推到右侧，
    // 单元格内 text-align:left 保证文字左对齐。
    // 表格无固定宽度，内容短时自动收缩，内容长时自然换行。
    return QStringLiteral(
        "<table style='border:none;' cellpadding='0' cellspacing='0'><tr>"
        "<td style='text-align:left; border:none; "
        "font-size:15px; line-height:1.8; padding:2px 0;'>"
        "%1"
        "</td></tr></table>"
    ).arg(text.toHtmlEscaped());
}

void ChatWindow::appendUserMessage(const QString &text)
{
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    appendFormattedLine(buildUserBubble(time, text), Qt::AlignRight);
}

void ChatWindow::appendAssistantMessage(const QString &text)
{
    const QString time = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
    appendFormattedLine(buildAssistantBubble(time, text), Qt::AlignLeft);
}

void ChatWindow::updateAssistantMessage(const QString &text)
{
    QTextCursor cursor = m_historyView->textCursor();
    cursor.movePosition(QTextCursor::End);

    if (m_isStreaming) {
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    } else {
        QTextDocument *doc = m_historyView->document();
        if (doc && !doc->isEmpty()) {
            cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        }
        m_isStreaming = true;
    }

    cursor.insertHtml(buildAssistantBubble(QString(), text));

    QTextBlockFormat msgFmt;
    msgFmt.setAlignment(Qt::AlignLeft);
    msgFmt.setBottomMargin(30);
    cursor.mergeBlockFormat(msgFmt);

    QScrollBar *sb = m_historyView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ChatWindow::finishAssistantMessage()
{
    if (!m_isStreaming) return;
    m_isStreaming = false;
}

void ChatWindow::appendSystemMessage(const QString &text)
{
    appendFormattedLine(QStringLiteral(
        "<div style='text-align:center; margin:8px 0;'>"
        "<span style='color:gray; font-size:13px;'>%1</span>"
        "</div>"
    ).arg(text.toHtmlEscaped()), Qt::AlignCenter);
}

void ChatWindow::setMessageInputEnabled(bool enabled)
{
    m_inputEnabled = enabled;
    m_inputEdit->setReadOnly(!enabled);
    m_inputEdit->setPlaceholderText(enabled
        ? QStringLiteral("说点什么吧... （Shift+Enter换行，Enter发送）")
        : QStringLiteral("正在处理，请稍候..."));
    m_sendButton->setEnabled(enabled);
}

void ChatWindow::showTransientStatus(const QString &text)
{
    m_transientStatus->setText(text);
    m_transientStatus->setVisible(true);
}

void ChatWindow::clearTransientStatus()
{
    m_transientStatus->setVisible(false);
    m_transientStatus->clear();
}

void ChatWindow::loadHistoryMessages(const QStringList &userOrAssistantRoles,
                                     const QStringList &contents)
{
    m_historyView->clear();
    const int count = qMin(userOrAssistantRoles.size(), contents.size());
    for (int i = 0; i < count; ++i) {
        if (userOrAssistantRoles.at(i) == QStringLiteral("user")) {
            appendUserMessage(contents.at(i));
        } else {
            appendAssistantMessage(contents.at(i));
        }
    }
}

// ============================================================================
// 搜索功能
// ============================================================================

void ChatWindow::onSearchToggled()
{
    if (m_searchBar->isVisible()) {
        // 收起：向下压缩到消失
        m_searchResults.clear();
        m_searchIndex = -1;
        m_searchCountLabel->setText(QStringLiteral("0/0"));
        m_searchPrevBtn->setEnabled(false);
        m_searchNextBtn->setEnabled(false);
        clearSearchHighlight();

        auto *anim = new QPropertyAnimation(m_searchBar, "maximumHeight");
        anim->setDuration(180);
        anim->setStartValue(m_searchBar->height());
        anim->setEndValue(0);
        anim->setEasingCurve(QEasingCurve::InOutQuad);
        connect(anim, &QPropertyAnimation::finished, this, [this]() {
            m_searchBar->setVisible(false);
            m_searchBar->setMaximumHeight(QWIDGETSIZE_MAX);
            // 恢复原始内容
            if (m_memoryDb && !m_characterName.isEmpty()) {
                const auto history = m_memoryDb->loadHistory(m_characterKey.isEmpty() ? m_characterName : m_characterKey, 200);
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
        // 展开：从 0 滑动到目标高度
        m_searchBar->setMaximumHeight(0);
        m_searchBar->setVisible(true);
        m_searchBar->adjustSize();
        const int targetH = m_searchBar->sizeHint().height();

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

    m_searchResults = m_memoryDb->searchMessages(keyword, m_characterKey.isEmpty() ? m_characterName : m_characterKey);
    m_searchIndex = m_searchResults.isEmpty() ? -1 : 0;

    m_searchCountLabel->setText(QStringLiteral("%1/%2")
        .arg(m_searchIndex + 1).arg(m_searchResults.size()));
    m_searchPrevBtn->setEnabled(m_searchResults.size() > 1);
    m_searchNextBtn->setEnabled(m_searchResults.size() > 1);

    // 显示搜索结果
    if (!m_searchResults.isEmpty()) {
        highlightSearchResult(0);
    } else {
        clearSearchHighlight();
        m_historyView->clear();
        appendSystemMessage(QStringLiteral("没有找到包含「%1」的消息").arg(keyword));
    }
}

void ChatWindow::clearSearchHighlight()
{
    // 恢复 QTextEdit 背景
    m_historyView->setExtraSelections({});
}

void ChatWindow::highlightSearchResult(int index)
{
    if (index < 0 || index >= m_searchResults.size()) return;

    const auto &rec = m_searchResults.at(index);

    // 清除当前内容，只显示当前搜索结果及其上下文
    m_historyView->clear();

    // 显示结果提示
    appendFormattedLine(QStringLiteral(
        "<div style='text-align:center; margin:6px 0;'>"
        "<span style='color:gray; font-size:12px;'>"
        "搜索结果 %1/%2  ——  匹配：%3"
        "</span></div>"
    ).arg(index + 1).arg(m_searchResults.size())
     .arg(m_searchInput->text().toHtmlEscaped()), Qt::AlignCenter);

    // 显示角色标识
    const QString roleLabel = (rec.role == QStringLiteral("user"))
        ? QStringLiteral("👤 你")
        : QStringLiteral("🎀 %1").arg(m_characterName);
    const QString roleColor = (rec.role == QStringLiteral("user")) ? QStringLiteral("#4a90d9") : QStringLiteral("#d94a8e");

    appendFormattedLine(QStringLiteral(
        "<div style='font-size:12px; color:%2; margin-bottom:2px;'>%1</div>"
        "<div style='font-size:15px; line-height:1.8; padding:6px 10px; "
        "background:#fff3cd; border-radius:6px;'>%3</div>"
        "<div style='font-size:11px; color:gray; margin-top:2px;'>%4</div>"
    ).arg(roleLabel, roleColor, rec.content.toHtmlEscaped(), rec.timestamp), Qt::AlignLeft);

    // 更新计数
    m_searchIndex = index;
    m_searchCountLabel->setText(QStringLiteral("%1/%2")
        .arg(index + 1).arg(m_searchResults.size()));
}

void ChatWindow::onSearchNext()
{
    if (m_searchResults.isEmpty()) {
        // 按回车触发搜索
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
    bodyParts << QStringLiteral("<h2 style='text-align:center; color:#333;'>与 %1 的聊天记录</h2>").arg(m_characterName.toHtmlEscaped());

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
    // 确认对话框
    const auto result = QMessageBox::question(
        this,
        QStringLiteral("清除聊天记录"),
        QStringLiteral("确定要删除与 %1 的所有聊天记录吗？\n此操作不可撤销。").arg(m_characterName),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );
    if (result != QMessageBox::Yes) return;

    // 清空显示
    m_historyView->clear();
    m_searchResults.clear();
    m_searchIndex = -1;
    m_isStreaming = false;

    // 通知 App 层清除数据库和 Prompt 上下文
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
    m_backgroundSourcePath = filePath;   // 首次选择时原始路径 = 显示路径
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

    qInfo() << "[ChatWindow] 已清除背景图";
}

void ChatWindow::setBackgroundOpacity(double opacity)
{
    m_backgroundOpacity = qBound(0.0, opacity, 1.0);
    update();

    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("ChatWindow"))
        .setValue(QStringLiteral("background/opacity"), m_backgroundOpacity);
}
