#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

class QProcess;
class VoiceVoxClient;

/**
 * @brief TtsEngine — 文本转语音引擎，支持 SAPI 和 VoiceVox 双后端。
 *
 * SAPI 后端：通过 QProcess + PowerShell System.Speech 实现，零依赖。
 * VoiceVox 后端：通过 VoiceVoxClient HTTP API 合成语音，用 PowerShell 播放 WAV。
 */
class TtsEngine : public QObject
{
    Q_OBJECT

public:
    enum Backend { SAPI, VoiceVox };
    Q_ENUM(Backend)

    explicit TtsEngine(QObject *parent = nullptr);
    ~TtsEngine() override;

    /// --- 通用控制 ---
    void speak(const QString &text);
    void stop();

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    /// 设置语速（-10 ~ 10，仅 SAPI 后端生效）
    void setRate(int rate);
    int rate() const { return m_rate; }

    /// --- 后端选择 ---
    void setBackend(Backend backend);
    Backend backend() const { return m_backend; }

    /// --- 语音选择 ---
    /// 返回当前后端可用的语音名列表。
    QStringList availableVoices();

    /// 设置当前语音（由语音显示名确定）。
    void setVoice(const QString &voiceDisplayName);
    QString voice() const;

    /// 重新查询当前后端的语音列表（SAPI 不变，VoiceVox 重新拉取说话人）。
    void refreshVoices();

    /// --- VoiceVox 专用 ---
    void setVoiceVoxUrl(const QString &url);
    QString voiceVoxUrl() const;

signals:
    void VoiceVoxSpeakersReady();

private:
    void ensureProcess();
    void killProcess();
    static QStringList querySapiVoices();

    /// VoiceVox 合成后播放 WAV 文件
    void playWavFile(const QString &wavPath);

private:
    // --- 通用 ---
    QProcess *m_process = nullptr;
    bool m_enabled = true;
    int m_rate = 0;                     // -10 ~ 10
    Backend m_backend = SAPI;

    // --- SAPI ---
    QString m_sapiVoiceName;            // 当前 SAPI 语音名
    QStringList m_cachedSapiVoices;

    // --- VoiceVox ---
    VoiceVoxClient *m_vvClient = nullptr;
    QString m_vvUrl = QStringLiteral("http://127.0.0.1:50021");
    int m_vvStyleId = 0;                // 当前说话人 style ID
    QString m_vvVoiceDisplayName;       // 当前显示名，如 "四国めたん（ノーマル）"
};
