#include "TtsEngine.h"
#include "VoiceVoxClient.h"

#include <QProcess>
#include <QDebug>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUuid>

// ★ 必须设置 InputEncoding = UTF8，否则中文会被 GBK 编码乱码解析
static const char *kPowerShellScript = R"(
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8;
[Console]::InputEncoding = [System.Text.Encoding]::UTF8;
Add-Type -AssemblyName System.Speech;
$s = [System.Speech.Synthesis.SpeechSynthesizer]::new();
$s.Rate = %1;
try { $s.SelectVoice("%2"); } catch {};
while (($line = [Console]::ReadLine()) -ne $null) {
    if ($line -eq '__STOP__') { break };
    if ($line -eq '') { continue };
    $s.Speak($line);
    [Console]::WriteLine('OK');
}
)";

// WAV 播放脚本：接收文件路径，播放完毕退出
static const char *kWavPlayerScript = R"(
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8;
[Console]::InputEncoding = [System.Text.Encoding]::UTF8;
Add-Type -AssemblyName System.Windows.Forms;
(New-Object Media.SoundPlayer '%1').PlaySync();
)";

TtsEngine::TtsEngine(QObject *parent)
    : QObject(parent)
{
    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));

    // 恢复后端
    m_backend = static_cast<Backend>(settings.value(QStringLiteral("backend"), static_cast<int>(SAPI)).toInt());

    // 恢复 SAPI 语音
    m_sapiVoiceName = settings.value(QStringLiteral("sapiVoice")).toString();

    // 恢复 VoiceVox 配置
    m_vvUrl = settings.value(QStringLiteral("vvUrl"), QStringLiteral("http://127.0.0.1:50021")).toString();
    m_vvStyleId = settings.value(QStringLiteral("vvStyleId"), 0).toInt();
    m_vvVoiceDisplayName = settings.value(QStringLiteral("vvVoiceName")).toString();

    // 创建 VoiceVox 客户端
    m_vvClient = new VoiceVoxClient(this);
    m_vvClient->setBaseUrl(m_vvUrl);

    // VoiceVox 音频就绪 → 播放
    connect(m_vvClient, &VoiceVoxClient::audioReady, this, [this](const QByteArray &wav) {
        // 写入临时文件
        const QString tmpDir = QDir::tempPath();
        const QString wavPath = tmpDir + QStringLiteral("/deskpet_vv_") + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".wav");
        QFile f(wavPath);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(wav);
            f.close();
            playWavFile(wavPath);
        } else {
            qWarning() << "[TtsEngine] 无法写入临时 WAV 文件:" << wavPath;
        }
    });

    connect(m_vvClient, &VoiceVoxClient::networkError, this, [this](const QString &msg) {
        qWarning() << "[TtsEngine] VoiceVox 错误:" << msg;
    });

    // VoiceVox 说话人就绪 → 通知 UI
    connect(m_vvClient, &VoiceVoxClient::speakersReady, this, [this]() {
        emit VoiceVoxSpeakersReady();
    });
}

TtsEngine::~TtsEngine()
{
    killProcess();
}

// ============================================================================
// 后端切换
// ============================================================================

void TtsEngine::setBackend(Backend backend)
{
    if (backend == m_backend) return;
    m_backend = backend;

    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
    settings.setValue(QStringLiteral("backend"), static_cast<int>(backend));

    // 停止当前后端
    stop();

    qInfo() << "[TtsEngine] 后端切换为:" << (backend == SAPI ? "SAPI" : "VoiceVox");
}

// ============================================================================
// 语音列表 / 选择
// ============================================================================

QStringList TtsEngine::availableVoices()
{
    if (m_backend == SAPI) {
        if (m_cachedSapiVoices.isEmpty()) {
            m_cachedSapiVoices = querySapiVoices();
        }
        return m_cachedSapiVoices;
    }

    // VoiceVox：从缓存返回显示名列表
    QStringList names;
    const auto speakers = m_vvClient->speakers(); // 需要给 VoiceVoxClient 加 speakers() 方法
    for (const auto &s : speakers) {
        names.append(s.name + QStringLiteral("（") + s.styleName + QStringLiteral("）"));
    }
    return names;
}

void TtsEngine::setVoice(const QString &voiceDisplayName)
{
    if (m_backend == SAPI) {
        if (voiceDisplayName == m_sapiVoiceName) return;
        m_sapiVoiceName = voiceDisplayName;

        QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
            .setValue(QStringLiteral("sapiVoice"), m_sapiVoiceName);

        // 重启 SAPI 进程
        if (m_process && m_process->state() == QProcess::Running) {
            killProcess();
        }
        qInfo() << "[TtsEngine] SAPI 语音设置为:" << m_sapiVoiceName;
        return;
    }

    // VoiceVox：从显示名反查 styleId
    const auto speakers = m_vvClient->speakers();
    for (const auto &s : speakers) {
        const QString display = s.name + QStringLiteral("（") + s.styleName + QStringLiteral("）");
        if (display == voiceDisplayName) {
            m_vvStyleId = s.styleId;
            m_vvVoiceDisplayName = voiceDisplayName;

            QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
            settings.setValue(QStringLiteral("vvStyleId"), m_vvStyleId);
            settings.setValue(QStringLiteral("vvVoiceName"), m_vvVoiceDisplayName);

            qInfo() << "[TtsEngine] VoiceVox 说话人设置为:" << voiceDisplayName;
            return;
        }
    }
}

QString TtsEngine::voice() const
{
    if (m_backend == SAPI) {
        return m_sapiVoiceName;
    }
    return m_vvVoiceDisplayName;
}

void TtsEngine::refreshVoices()
{
    if (m_backend == VoiceVox) {
        m_vvClient->fetchSpeakers();
    }
}

// ============================================================================
// 朗读
// ============================================================================

void TtsEngine::speak(const QString &text)
{
    if (!m_enabled || text.isEmpty()) return;

    // 清理 HTML/Markdown 标记
    QString sanitized = text;
    sanitized.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    if (sanitized.isEmpty()) return;

    if (m_backend == SAPI) {
        ensureProcess();
        if (!m_process || m_process->state() != QProcess::Running) {
            qWarning() << "[TtsEngine] SAPI 进程不可用";
            return;
        }
        m_process->write((sanitized + QStringLiteral("\r\n")).toUtf8());
        // ★ 不阻塞等写入完成 —— 管道写几乎瞬间完成，waitForBytesWritten 会阻塞主线程 1 秒
        //   阻塞期间 Qt 事件循环无法处理窗口事件/动画/重绘 → 卡顿
        return;
    }

    // VoiceVox：异步合成
    m_vvClient->synthesize(sanitized, m_vvStyleId);
}

void TtsEngine::stop()
{
    if (m_backend == SAPI) {
        if (m_process && m_process->state() == QProcess::Running) {
            m_process->write("__STOP__\r\n");
            killProcess();
        }
    }
    // VoiceVox 的音频播放是独立进程，stop 不打断已开始的播放
}

// ============================================================================
// SAPI 进程管理
// ============================================================================

void TtsEngine::ensureProcess()
{
    if (m_process && m_process->state() == QProcess::Running) return;
    killProcess();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);

    QString safeVoice = m_sapiVoiceName;
    safeVoice.replace(QStringLiteral("\""), QStringLiteral("\"\""));

    const QString script = QString::fromUtf8(kPowerShellScript)
                               .arg(m_rate)
                               .arg(safeVoice);

    m_process->start(QStringLiteral("powershell"),
                     {QStringLiteral("-NoProfile"), QStringLiteral("-NoLogo"),
                      QStringLiteral("-Command"), script});

    if (!m_process->waitForStarted(3000)) {
        qWarning() << "[TtsEngine] 无法启动 PowerShell 进程:" << m_process->errorString();
        return;
    }
    qInfo() << "[TtsEngine] SAPI 进程已启动，语音:" << (m_sapiVoiceName.isEmpty() ? "默认" : m_sapiVoiceName);
}

void TtsEngine::killProcess()
{
    if (m_process) {
        if (m_process->state() == QProcess::Running) {
            m_process->write("__STOP__\r\n");
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        m_process->deleteLater();
        m_process = nullptr;
    }
}

// ============================================================================
// VoiceVox WAV 播放
// ============================================================================

void TtsEngine::playWavFile(const QString &wavPath)
{
    const QString script = QString::fromUtf8(kWavPlayerScript).arg(wavPath);

    auto *player = new QProcess(this);
    connect(player, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, player, wavPath](int, QProcess::ExitStatus) {
                // 播放完毕，清理
                QFile::remove(wavPath);
                player->deleteLater();
            });

    player->start(QStringLiteral("powershell"),
                  {QStringLiteral("-NoProfile"), QStringLiteral("-NoLogo"),
                   QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                   QStringLiteral("-Command"), script});

    if (!player->waitForStarted(3000)) {
        qWarning() << "[TtsEngine] 无法启动 WAV 播放进程:" << player->errorString();
        QFile::remove(wavPath);
        player->deleteLater();
    }
}

// ============================================================================
// 语速
// ============================================================================

void TtsEngine::setRate(int rate)
{
    m_rate = qBound(-10, rate, 10);
    if (m_process && m_process->state() == QProcess::Running) {
        killProcess();
    }
}

// ============================================================================
// VoiceVox URL
// ============================================================================

void TtsEngine::setVoiceVoxUrl(const QString &url)
{
    m_vvUrl = url;
    while (m_vvUrl.endsWith('/')) m_vvUrl.chop(1);
    m_vvClient->setBaseUrl(m_vvUrl);

    QSettings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"))
        .setValue(QStringLiteral("vvUrl"), m_vvUrl);
}

QString TtsEngine::voiceVoxUrl() const
{
    return m_vvUrl;
}

// ============================================================================
// 查询 SAPI 语音
// ============================================================================

QStringList TtsEngine::querySapiVoices()
{
    QProcess proc;
    proc.start(QStringLiteral("powershell"),
               {QStringLiteral("-NoProfile"), QStringLiteral("-NoLogo"),
                QStringLiteral("-Command"), QStringLiteral("-")});
    if (!proc.waitForStarted(5000)) {
        qWarning() << "[TtsEngine] 无法启动 PowerShell 查询语音";
        return {};
    }

    const QByteArray script =
        "[Console]::OutputEncoding = [System.Text.Encoding]::UTF8;\r\n"
        "Add-Type -AssemblyName System.Speech;\r\n"
        "$s = New-Object System.Speech.Synthesis.SpeechSynthesizer;\r\n"
        "$s.GetInstalledVoices() | ForEach-Object { $_.VoiceInfo.Name };\r\n"
        "exit\r\n";

    proc.write(script);
    proc.closeWriteChannel();
    proc.waitForFinished(5000);

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    QStringList voices;
    for (const QString &line : output.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                            Qt::SkipEmptyParts)) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) voices.append(t);
    }
    return voices;
}
