#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>

#include "PromptManager.h" // 复用 ChatMessage 类型定义

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @brief DeepSeekClient 封装对 DeepSeek Chat Completion API 的调用。
 *
 * 支持流式输出（streaming），AI 回复逐字显示。
 *
 * API Key 获取方式（按优先级）：
 *  1. 环境变量 DEEPSEEK_API_KEY
 *  2. QStandardPaths::AppConfigLocation/deepseek_api_key.txt
 *  3. （向后兼容）旧程序目录下的 deepseek_api_key.txt
 * Key 持久化使用 QSaveFile 原子写入，空 Key 时自动删除持久化文件。
 * 强烈建议不要把 API Key 硬编码进源码。
 *
 * 超时与取消：
 *  - sendMessage / sendSummaryRequest 各有 30 秒总时限（QTimer + setTransferTimeout 双重保护）。
 *  - 超时时自动终止请求并发射错误信号。
 *  - cancelMessage() / cancelSummary() 可主动取消，不发射错误信号。
 *  - 请求结束后自动清理计时器，防止 error / finished / 流式错误重复终结。
 */
class DeepSeekClient : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekClient(QObject *parent = nullptr);
    ~DeepSeekClient() override;

    void setApiKey(const QString &apiKey) { m_apiKey = apiKey; }
    bool hasApiKey() const { return !m_apiKey.isEmpty(); }

    bool setApiKeyAndPersist(const QString &apiKey);

    void setModel(const QString &model) { m_model = model; }
    void setEndpoint(const QString &endpoint) { m_endpoint = endpoint; }

    // 供独立的 MemoryExtractor 复用同一套 API 配置（不共用连接/请求通道）
    QString apiKey() const { return m_apiKey; }
    QString model() const { return m_model; }
    QString endpoint() const { return m_endpoint; }

    void sendMessage(const QString &latestUserText, const QList<ChatMessage> &history);

    /**
     * @brief 发送摘要请求（非流式，stream=false）。
     * 使用独立固定基础指令，不加载当前角色/风格，不扮演角色。
     * 与 sendMessage 严格串行使用——App 层保证二者不同时运行。
     * @param conversationHistory 待摘要的 user/assistant 对话历史列表
     * @param requestId 调用方指定 ID，在 summaryReceived/summaryFailed 中原样返回
     */
    void sendSummaryRequest(const QList<ChatMessage> &conversationHistory, const QString &requestId);

    /// 取消正在进行的消息请求（不触发 errorOccurred 信号）。
    void cancelMessage();
    /// 取消正在进行的摘要请求（不触发 summaryFailed 信号）。
    void cancelSummary();

    /// 设置请求超时毫秒数（默认 30000，仅用于测试覆写）。
    void setRequestTimeoutMs(int ms) { m_timeoutMs = ms; }

signals:
    /// 流式输出中间结果（每次收到一个 delta 片段时触发）。
    void partialReply(const QString &text);

    /// 流式输出结束，返回完整文本。
    void replyReceived(const QString &reply);

    void errorOccurred(const QString &error);

    /// 摘要请求成功返回。
    void summaryReceived(const QString &requestId, const QString &text);

    /// 摘要请求失败（网络错误、API 错误、空响应等）。
    void summaryFailed(const QString &requestId, const QString &error);

private:
    QString loadApiKeyFromFileFallback() const;
    void handleNetworkReply(QNetworkReply *reply);
    void handleStreamData(QNetworkReply *reply);

    /// 超时触发时调用（由 m_requestTimer 触发）。
    void onRequestTimeout();

    /// 清理请求相关状态，停止计时器，重置标志。
    void cleanupRequest();

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    QString m_apiKey;
    QString m_model = QStringLiteral("deepseek-v4-flash");
    QString m_endpoint = QStringLiteral("https://api.deepseek.com/chat/completions");

    // 流式输出缓冲区
    QByteArray m_streamBuffer;
    QString m_fullReply;

    // ──── 超时与取消 ────
    QTimer    *m_requestTimer    = nullptr;
    QNetworkReply *m_activeReply = nullptr;
    int        m_timeoutMs       = 30000;
    bool       m_timedOut        = false;
    bool       m_cancelled       = false;
    bool       m_isSummaryRequest = false;
};
