#include "VoiceVoxClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QUrlQuery>

VoiceVoxClient::VoiceVoxClient(QObject *parent)
    : QObject(parent)
{
    m_net = new QNetworkAccessManager(this);
}

VoiceVoxClient::~VoiceVoxClient() = default;

void VoiceVoxClient::setBaseUrl(const QString &url)
{
    m_baseUrl = url;
    while (m_baseUrl.endsWith('/'))
        m_baseUrl.chop(1);
}

// ============================================================================
// 查询说话人列表
// ============================================================================
void VoiceVoxClient::fetchSpeakers()
{
    QNetworkRequest req(QUrl(m_baseUrl + QStringLiteral("/speakers")));
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit networkError(QStringLiteral("获取说话人列表失败: %1").arg(reply->errorString()));
            return;
        }

        onSpeakersReply(reply->readAll());
    });
}

void VoiceVoxClient::onSpeakersReply(const QByteArray &data)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        emit networkError(QStringLiteral("解析说话人数据失败: %1").arg(err.errorString()));
        return;
    }

    const QJsonArray arr = doc.array();
    QList<SpeakerInfo> speakers;

    for (const QJsonValue &val : arr) {
        const QJsonObject obj = val.toObject();
        const QString name = obj.value(QStringLiteral("name")).toString();

        const QJsonArray styles = obj.value(QStringLiteral("styles")).toArray();
        for (const QJsonValue &sv : styles) {
            const QJsonObject sObj = sv.toObject();
            SpeakerInfo info;
            info.name      = name;
            info.styleName = sObj.value(QStringLiteral("name")).toString();
            info.styleId   = sObj.value(QStringLiteral("id")).toInt();
            speakers.append(info);
        }
    }

    m_cachedSpeakers = speakers;
    qInfo() << "[VoiceVoxClient] 获取到" << speakers.size() << "个说话人风格";
    emit speakersReady();
}

// ============================================================================
// 语音合成（两步：audio_query → synthesis）
// ============================================================================
void VoiceVoxClient::synthesize(const QString &text, int styleId)
{
    // Step 1: POST /audio_query
    QUrl queryUrl(m_baseUrl + QStringLiteral("/audio_query"));
    QUrlQuery params;
    params.addQueryItem(QStringLiteral("text"), text);
    params.addQueryItem(QStringLiteral("speaker"), QString::number(styleId));
    queryUrl.setQuery(params);

    QNetworkRequest req(queryUrl);
    req.setRawHeader("Accept", "application/json");

    QNetworkReply *queryReply = m_net->post(req, QByteArray());
    connect(queryReply, &QNetworkReply::finished, this, [this, queryReply, text, styleId]() {
        queryReply->deleteLater();

        if (queryReply->error() != QNetworkReply::NoError) {
            emit networkError(QStringLiteral("audio_query 失败: %1").arg(queryReply->errorString()));
            return;
        }

        // Step 2: POST /synthesis 带上 query JSON
        const QByteArray queryJson = queryReply->readAll();

        QUrl synthUrl(m_baseUrl + QStringLiteral("/synthesis"));
        QUrlQuery synthParams;
        synthParams.addQueryItem(QStringLiteral("speaker"), QString::number(styleId));
        synthUrl.setQuery(synthParams);

        QNetworkRequest synthReq(synthUrl);
        synthReq.setRawHeader("Accept", "audio/wav");
        synthReq.setRawHeader("Content-Type", "application/json");

        QNetworkReply *synthReply = m_net->post(synthReq, queryJson);
        connect(synthReply, &QNetworkReply::finished, this, [this, synthReply]() {
            synthReply->deleteLater();

            if (synthReply->error() != QNetworkReply::NoError) {
                emit networkError(QStringLiteral("synthesis 失败: %1").arg(synthReply->errorString()));
                return;
            }

            const QByteArray wav = synthReply->readAll();
            if (wav.isEmpty()) {
                emit networkError(QStringLiteral("synthesis 返回空数据"));
                return;
            }

            emit audioReady(wav);
        });
    });
}
