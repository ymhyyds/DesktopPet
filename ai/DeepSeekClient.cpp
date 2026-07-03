#include "DeepSeekClient.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QProcessEnvironment>
#include <QFile>
#include <QTextStream>
#include <QDebug>

DeepSeekClient::DeepSeekClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    m_apiKey = QProcessEnvironment::systemEnvironment().value(QStringLiteral("DEEPSEEK_API_KEY"));

    if (m_apiKey.isEmpty()) {
        m_apiKey = loadApiKeyFromFileFallback();
    }

    if (m_apiKey.isEmpty()) {
        qWarning() << "[DeepSeekClient] 未配置 API Key，请设置环境变量 DEEPSEEK_API_KEY，"
                       "或在程序运行目录下创建 deepseek_api_key.txt 文件。";
    }
}

DeepSeekClient::~DeepSeekClient() = default;

QString DeepSeekClient::loadApiKeyFromFileFallback() const
{
    QFile file(QStringLiteral("deepseek_api_key.txt"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    const QString key = stream.readLine().trimmed();
    file.close();
    return key;
}

bool DeepSeekClient::setApiKeyAndPersist(const QString &apiKey)
{
    m_apiKey = apiKey;

    QFile file(QStringLiteral("deepseek_api_key.txt"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "[DeepSeekClient] 无法写入 deepseek_api_key.txt，本次设置仅在内存中生效（重启后失效）";
        return false;
    }
    QTextStream stream(&file);
    stream << apiKey;
    file.close();
    return true;
}

void DeepSeekClient::sendMessage(const QString &latestUserText, const QList<ChatMessage> &history)
{
    Q_UNUSED(latestUserText);

    if (m_apiKey.isEmpty()) {
        emit errorOccurred(tr("缺少 DeepSeek API Key，无法发起请求"));
        return;
    }

    QJsonArray messagesArray;
    for (const ChatMessage &msg : history) {
        QJsonObject obj;
        obj.insert(QStringLiteral("role"), msg.first);
        obj.insert(QStringLiteral("content"), msg.second);
        messagesArray.append(obj);
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), m_model);
    body.insert(QStringLiteral("messages"), messagesArray);
    body.insert(QStringLiteral("stream"), true);
    body.insert(QStringLiteral("temperature"), 1.0);

    QNetworkRequest request{QUrl(m_endpoint)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_apiKey).toUtf8());

    QNetworkReply *reply = m_networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));

    // 流式模式：实时读取数据块
    m_streamBuffer.clear();
    m_fullReply.clear();
    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        handleStreamData(reply);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        // 流结束，发送完整回复
        if (!m_fullReply.isEmpty()) {
            emit replyReceived(m_fullReply);
        } else {
            // 没有流数据，退化为普通响应处理
            handleNetworkReply(reply);
        }
        reply->deleteLater();
    });
}

void DeepSeekClient::handleStreamData(QNetworkReply *reply)
{
    m_streamBuffer.append(reply->readAll());

    // 按 SSE 格式解析：data: {...}\n\n
    while (true) {
        int idx = m_streamBuffer.indexOf("\n\n");
        if (idx < 0) break; // 还未收到完整事件

        QByteArray event = m_streamBuffer.left(idx);
        m_streamBuffer.remove(0, idx + 2);

        // 去掉 "data: " 前缀
        if (event.startsWith("data: ")) {
            event = event.mid(6);
        } else {
            continue;
        }

        // [DONE] 结束标记
        if (event.trimmed() == "[DONE]") {
            continue;
        }

        // 解析 JSON
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(event, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            continue;
        }

        const QJsonObject root = doc.object();

        // 检查错误
        if (root.contains(QStringLiteral("error"))) {
            const QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
            emit errorOccurred(tr("DeepSeek API 错误：%1")
                                    .arg(errObj.value(QStringLiteral("message")).toString()));
            return;
        }

        // 提取 content delta
        const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) continue;

        const QJsonObject firstChoice = choices.first().toObject();
        const QJsonObject delta = firstChoice.value(QStringLiteral("delta")).toObject();
        const QString content = delta.value(QStringLiteral("content")).toString();

        if (!content.isEmpty()) {
            m_fullReply += content;
            emit partialReply(m_fullReply);
        }
    }
}

void DeepSeekClient::handleNetworkReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(tr("网络请求失败：%1").arg(reply->errorString()));
        return;
    }

    const QByteArray rawData = reply->readAll();

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(rawData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit errorOccurred(tr("AI 响应解析失败：%1").arg(parseError.errorString()));
        return;
    }

    const QJsonObject root = doc.object();

    if (root.contains(QStringLiteral("error"))) {
        const QJsonObject errObj = root.value(QStringLiteral("error")).toObject();
        emit errorOccurred(tr("DeepSeek API 错误：%1")
                                .arg(errObj.value(QStringLiteral("message")).toString()));
        return;
    }

    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        emit errorOccurred(tr("AI 响应中没有可用的回复内容"));
        return;
    }

    const QJsonObject firstChoice = choices.first().toObject();
    const QJsonObject message = firstChoice.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString();

    if (content.isEmpty()) {
        emit errorOccurred(tr("AI 回复内容为空"));
        return;
    }

    emit replyReceived(content);
}
