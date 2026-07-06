#pragma once

#include <QWidget>
#include <QList>
#include <QPixmap>
#include <QString>
#include <QInputMethodEvent>

class QTextEdit;
class QLineEdit;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QStackedWidget;
class MemoryDB;
struct MessageRecord;

class ChatWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWindow(QWidget *parent = nullptr);

    void setCharacterName(const QString &name);
    /// 设置 DB 中使用的角色 key（文件夹名），用于搜索和导出
    void setCharacterKey(const QString &key) { m_characterKey = key; }
    void setMemoryDB(MemoryDB *db) { m_memoryDb = db; }

    // ──── 🏞️ 聊天背景图 ────
    void setBackgroundImage(const QString &filePath);
    void setBackgroundCropped(const QString &croppedPath, const QString &sourcePath);
    void clearBackground();
    void setBackgroundOpacity(double opacity);
    QString backgroundImagePath() const { return m_backgroundImagePath; }
    QString backgroundSourcePath() const { return m_backgroundSourcePath; }
    double backgroundOpacity() const { return m_backgroundOpacity; }

public slots:
    void showAndFocus();
    void appendAssistantMessage(const QString &text);
    void appendSystemMessage(const QString &text);
    void loadHistoryMessages(const QStringList &userOrAssistantRoles, const QStringList &contents);
    void updateAssistantMessage(const QString &text);
    void finishAssistantMessage();

signals:
    void userMessageSent(const QString &text);
    /// 请求清除当前角色的所有聊天记录
    void clearRequested();

protected:
    void closeEvent(QCloseEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onSendClicked();
    void onFileClicked();
    void onSearchToggled();
    void onSearchNext();
    void onSearchPrev();
    void onExportClicked();
    void onClearClicked();

private:
    void setupUi();
    void appendUserMessage(const QString &text);
    void scrollToBottom();
    void adjustInputEditHeight();
    void updateBubbleMaxWidth();
    void clearMessageWidgets();

    // ──── 消息控件创建 ────
    QWidget *createUserMessageRow(const QString &text);
    QWidget *createAssistantBlock(const QString &text);
    QWidget *createSystemMessageRow(const QString &text);

    // ──── 搜索 ────
    void doSearch();
    void clearSearchHighlight();
    void highlightSearchResult(int index);

    // ──── 导出 ────
    void exportAsTxt(const QString &filePath);
    void exportAsJson(const QString &filePath);
    void exportAsHtml(const QString &filePath);

private:
    // ──── 消息区域（QStackedWidget: 0=会话, 1=搜索结果） ────
    QStackedWidget *m_messageStack = nullptr;

    // ──── Page 0: 正常聊天消息列表 ────
    QScrollArea *m_scrollArea = nullptr;
    QWidget     *m_conversationContainer = nullptr;
    QVBoxLayout *m_messageListLayout = nullptr;

    // ──── Page 1: 搜索结果（QTextEdit 复用旧搜索逻辑） ────
    QTextEdit   *m_searchResultView = nullptr;

    // ──── 流式回复状态 ────
    struct AssistantBlock {
        QWidget  *container   = nullptr;
        QTextEdit *contentEdit = nullptr;
    };
    AssistantBlock m_currentAssistant;
    bool    m_isStreaming = false;
    QString m_lastStreamingText;        // 上次收到的完整流式文本，用于安全计算增量
    bool    m_imeComposing = false;     // 输入法正在组合中

    // ──── 输入区 ────
    QTextEdit   *m_inputEdit  = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_fileButton = nullptr;

    // ──── 基础信息 ────
    QString m_characterName = QStringLiteral("桌宠");
    QString m_characterKey;
    MemoryDB *m_memoryDb = nullptr;

    // ──── 搜索 UI ────
    QWidget     *m_searchBar = nullptr;
    QLineEdit   *m_searchInput = nullptr;
    QPushButton *m_searchPrevBtn = nullptr;
    QPushButton *m_searchNextBtn = nullptr;
    QLabel      *m_searchCountLabel = nullptr;
    QPushButton *m_searchToggleBtn = nullptr;

    // ──── 导出 ────
    QPushButton *m_exportBtn = nullptr;

    // ──── 清除 ────
    QPushButton *m_clearBtn = nullptr;

    // ──── 搜索结果 ────
    QList<MessageRecord> m_searchResults;
    int m_searchIndex = -1;

    // ──── 🏞️ 聊天背景图 ────
    QPixmap m_backgroundImage;
    QString m_backgroundImagePath;
    QString m_backgroundSourcePath;
    double  m_backgroundOpacity = 0.3;

    // ──── 全屏恢复 ────
    QRect   m_normalGeometry;
};
