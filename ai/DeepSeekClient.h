#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>

#include "PromptManager.h" // 复用 ChatMessage 类型定义

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief DeepSeekClient 封装对 DeepSeek Chat Completion API 的调用。
 *
 * 支持流式输出（streaming），AI 回复逐字显示。
 *
 * API Key 获取方式（按优先级）：
 *  1. 环境变量 DEEPSEEK_API_KEY
 *  2. 当前目录下的 deepseek_api_key.txt 文件（方便本地开发，不建议提交到版本库）
 * 强烈建议不要把 API Key 硬编码进源码。
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

    void sendMessage(const QString &latestUserText, const QList<ChatMessage> &history);

signals:
    /// 流式输出中间结果（每次收到一个 delta 片段时触发）。
    void partialReply(const QString &text);

    /// 流式输出结束，返回完整文本。
    void replyReceived(const QString &reply);

    void errorOccurred(const QString &error);

private:
    QString loadApiKeyFromFileFallback() const;
    void handleNetworkReply(QNetworkReply *reply);
    void handleStreamData(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    QString m_apiKey;
    QString m_model = QStringLiteral("deepseek-v4-flash");
    QString m_endpoint = QStringLiteral("https://api.deepseek.com/chat/completions");

    // 流式输出缓冲区
    QByteArray m_streamBuffer;
    QString m_fullReply;
};
