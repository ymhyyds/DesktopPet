#pragma once

#include <QObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QDateTime>
#include <QString>

/**
 * @brief WeatherManager — 异步获取天气信息，缓存，提供环境上下文字符串。
 *
 * 职责：
 *  1. 向 wttr.in 请求 JSON 格式天气数据（format=j1）
 *  2. 解析 current_condition / nearest_area
 *  3. 缓存 30 分钟，刷新失败时允许使用上一次结果
 *  4. 构建整洁的天气描述文本，供 PromptManager 注入 AI 上下文
 *
 * 使用方式（App 持有）：
 *   auto *wm = new WeatherManager(this);
 *   connect(wm, &WeatherManager::weatherUpdated, this, [this]{ ... });
 *   wm->requestWeather();
 */
class WeatherManager : public QObject
{
    Q_OBJECT

public:
    explicit WeatherManager(QObject *parent = nullptr);
    ~WeatherManager() override;

    /// 发起一次异步天气请求（已在途中则忽略）
    void requestWeather();

    /// 启动自动刷新定时器，立即请求一次，之后每 intervalMs 毫秒自动刷新
    void startAutoRefresh(int intervalMs = 30 * 60 * 1000);

    /// 停止自动刷新定时器并取消未完成请求
    void stopAutoRefresh();

    /// 检查缓存数据是否仍在有效期内（30 分钟内）
    bool hasFreshData() const;

    /// 构建天气部分的上下文文本（不含时间，时间由 PromptManager 生成）
    /// 未获取到数据时返回空字符串。
    QString buildWeatherContext() const;

    /// 格式化当前 UTC 偏移，例如 "UTC+08:00"
    static QString formatUtcOffset();

    /// 从 QSettings 缓存恢复天气数据（启动时调用）
    void restoreFromCache(const QString &weatherContext, qint64 fetchUtcMs);

    /// 上一次成功获取的 UTC 毫秒时间戳（epoch），0 表示从未成功过
    qint64 lastFetchUtcMs() const { return m_fetchTimestamp; }

    // ---- 测试用辅助 ----

    /// 覆写请求基址（默认 https://wttr.in），测试时指向本地 mock server
    void setBaseUrlForTesting(const QString &url);

    /// 覆写缓存有效期（毫秒），测试时可设小值加速
    void setCacheDurationForTesting(int ms);

    /// 覆写超时（毫秒），默认 10000
    void setTimeoutForTesting(int ms);

signals:
    /// 天气数据成功更新（包括首次获取和刷新）
    void weatherUpdated();

    /// 天气获取失败（仅用于日志，不会弹出错误框）
    void weatherFetchFailed();

private slots:
    void onReplyFinished();
    void onTimeout();

private:
    struct WeatherData {
        QString tempC;         // current_condition[0].temp_C
        QString feelsLikeC;    // current_condition[0].FeelsLikeC
        QString humidity;      // current_condition[0].humidity
        QString description;   // current_condition[0].weatherDesc[0].value
        QString obsDateTime;   // current_condition[0].localObsDateTime
        QString areaName;      // nearest_area[0].areaName[0].value

        bool isValid() const {
            return !tempC.isEmpty() && !description.isEmpty();
        }
    };

    void parseResponse(const QByteArray &data);
    void cleanupRequest();
    QString sanitize(const QString &text) const;

    QNetworkAccessManager m_nam;
    QNetworkReply *m_activeReply      = nullptr;
    QTimer          m_timeoutTimer;
    QTimer          m_autoRefreshTimer;

    bool m_requestPending = false;

    WeatherData m_currentData;
    QDateTime   m_cacheExpiry;
    int         m_cacheDurationMs = 30 * 60 * 1000; // 30 分钟

    /// 上一次成功构建的天气上下文字符串（不含过期标记），
    /// 用于 cache restore 路径，避免字段级重建。
    QString m_lastCleanContext;

    /// 上一次成功获取的 UTC 毫秒时间戳（QDateTime::currentMSecsSinceEpoch）
    qint64  m_fetchTimestamp = 0;

    QString m_baseUrl   = QStringLiteral("https://wttr.in");
    int     m_timeoutMs = 10000;
};
