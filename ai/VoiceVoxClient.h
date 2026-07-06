#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QByteArray>

class QNetworkAccessManager;
class QNetworkReply;

/**
 * @brief VoiceVoxClient — 与本地 VoiceVox HTTP API 通信的客户端。
 *
 * VoiceVox 是免费开源的中文/日语语音合成引擎，
 * 提供大量二次元角色语音（四国めたん、ずんだもん等）。
 * 需先启动 VoiceVox Engine。
 */
class VoiceVoxClient : public QObject
{
    Q_OBJECT

public:
    struct SpeakerInfo {
        QString name;       // "四国めたん"
        QString styleName;  // "ノーマル"
        int    styleId = 0; // 用于合成请求的 ID
    };

    explicit VoiceVoxClient(QObject *parent = nullptr);
    ~VoiceVoxClient() override;

    /// 设置 VoiceVox API 基础 URL，默认 http://127.0.0.1:50021
    void setBaseUrl(const QString &url);
    QString baseUrl() const { return m_baseUrl; }

    /// 获取缓存的说话人列表
    QList<SpeakerInfo> speakers() const { return m_cachedSpeakers; }

    /// 查询可用说话人列表（异步，结果通过 speakersReady 信号返回）
    void fetchSpeakers();

    /// 合成语音（异步，结果通过 audioReady 信号返回）
    void synthesize(const QString &text, int styleId, int requestId);

    /// 取消所有进行中的请求
    void cancelPendingRequests();

    /// 是否有进行中的网络请求
    bool hasPendingRequests() const { return !m_pendingReplies.isEmpty(); }

signals:
    void speakersReady();
    void audioReady(int requestId, const QByteArray &wavData);
    void requestFailed(int requestId, const QString &reason);
    void networkError(const QString &message);

private:
    void onSpeakersReply(const QByteArray &data);

private:
    QNetworkAccessManager *m_net = nullptr;
    QString m_baseUrl = QStringLiteral("http://127.0.0.1:50021");
    QList<SpeakerInfo> m_cachedSpeakers;
    QList<QNetworkReply*> m_pendingReplies;
};
