#include "WeatherManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QDebug>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

WeatherManager::WeatherManager(QObject *parent)
    : QObject(parent)
{
    m_timeoutTimer.setSingleShot(true);
    connect(&m_timeoutTimer, &QTimer::timeout, this, &WeatherManager::onTimeout);

    // 自动刷新定时器：触发时发起请求，requestWeather() 内部会用
    // m_requestPending 过滤掉并发请求
    connect(&m_autoRefreshTimer, &QTimer::timeout,
            this, &WeatherManager::requestWeather);
}

WeatherManager::~WeatherManager()
{
    stopAutoRefresh();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WeatherManager::requestWeather()
{
    // 防止重复并发请求
    if (m_requestPending) {
        qDebug() << "[WeatherManager] 请求已在途中，跳过";
        return;
    }

    // 清理上一次已完成但未 delete 的 reply
    cleanupRequest();

    const QString url = m_baseUrl + QStringLiteral("/?format=j1&lang=zh-cn&m");
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "curl/8.0");
    req.setTransferTimeout(m_timeoutMs);

    m_requestPending = true;
    m_activeReply = m_nam.get(req);

    connect(m_activeReply, &QNetworkReply::finished, this, &WeatherManager::onReplyFinished);
    m_timeoutTimer.start(m_timeoutMs);

    qDebug() << "[WeatherManager] 开始请求天气:" << url;
}

bool WeatherManager::hasFreshData() const
{
    return m_currentData.isValid()
        && m_cacheExpiry.isValid()
        && QDateTime::currentDateTime() < m_cacheExpiry;
}

QString WeatherManager::buildWeatherContext() const
{
    if (!m_currentData.isValid())
        return {};

    QString text;

    // 大致地区
    if (!m_currentData.areaName.isEmpty()) {
        text += QStringLiteral("大致地区：%1\n").arg(m_currentData.areaName);
    }

    // 天气条件
    text += QStringLiteral("天气：%1，%2°C，体感 %3°C，湿度 %4%")
        .arg(m_currentData.description,
             m_currentData.tempC,
             m_currentData.feelsLikeC,
             m_currentData.humidity);

    // 观测时间
    if (!m_currentData.obsDateTime.isEmpty()) {
        text += QStringLiteral("\n天气观测时间：%1").arg(m_currentData.obsDateTime);
    }

    // 数据可能已过期
    if (m_cacheExpiry.isValid() && QDateTime::currentDateTime() >= m_cacheExpiry) {
        text += QStringLiteral("\n天气数据可能已过期");
    }

    return text;
}

QString WeatherManager::formatUtcOffset()
{
    const QDateTime now = QDateTime::currentDateTime();
    const int offsetSecs = now.offsetFromUtc();
    const int hours = offsetSecs / 3600;
    const int mins = qAbs(offsetSecs % 3600) / 60;
    const QString sign = (offsetSecs >= 0) ? QStringLiteral("+") : QStringLiteral("-");
    return QStringLiteral("UTC%1%2:%3")
        .arg(sign)
        .arg(qAbs(hours), 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'));
}

// ---------------------------------------------------------------------------
// 自动刷新
// ---------------------------------------------------------------------------

void WeatherManager::startAutoRefresh(int intervalMs)
{
    m_autoRefreshTimer.setInterval(intervalMs);

    // 立即发起第一次请求
    requestWeather();

    // 启动定时器，后续每次超时自动调用 requestWeather()
    m_autoRefreshTimer.start();
}

void WeatherManager::stopAutoRefresh()
{
    m_autoRefreshTimer.stop();
    cleanupRequest();
}

// ---------------------------------------------------------------------------
// 测试辅助
// ---------------------------------------------------------------------------

void WeatherManager::setBaseUrlForTesting(const QString &url)
{
    m_baseUrl = url;
}

void WeatherManager::setCacheDurationForTesting(int ms)
{
    m_cacheDurationMs = ms;
}

void WeatherManager::setTimeoutForTesting(int ms)
{
    m_timeoutMs = ms;
}

// ---------------------------------------------------------------------------
// 私有槽
// ---------------------------------------------------------------------------

void WeatherManager::onReplyFinished()
{
    if (!m_activeReply)
        return;

    auto *reply = m_activeReply;
    m_activeReply = nullptr;
    m_timeoutTimer.stop();

    const int statusCode = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const bool hasError = (reply->error() != QNetworkReply::NoError);

    reply->deleteLater();
    m_requestPending = false;

    // HTTP 状态 / 网络错误检查
    if (hasError || statusCode != 200 || body.isEmpty()) {
        qWarning() << "[WeatherManager] 天气请求失败: HTTP" << statusCode
                    << "error:" << reply->errorString();
        // 保留上一次有效缓存，不清除
        if (!m_currentData.isValid()) {
            // 从未获取成功过 → 确保数据为空
            m_currentData = {};
        }
        emit weatherFetchFailed();
        return;
    }

    parseResponse(body);

    if (m_currentData.isValid()) {
        m_cacheExpiry = QDateTime::currentDateTime().addMSecs(m_cacheDurationMs);
        qInfo() << "[WeatherManager] 天气更新成功:"
                << m_currentData.tempC << "°C"
                << m_currentData.description
                << "| 地区:" << m_currentData.areaName;
        emit weatherUpdated();
    } else {
        qWarning() << "[WeatherManager] JSON 解析后无有效天气数据";
        emit weatherFetchFailed();
    }
}

void WeatherManager::onTimeout()
{
    m_timeoutTimer.stop();

    // 保存指针后立即清空 m_activeReply，防止 abort() 同步触发
    // onReplyFinished 时重入访问同一 reply。
    QNetworkReply *reply = m_activeReply;
    m_activeReply = nullptr;
    m_requestPending = false;

    if (reply) {
        // 先断开连接，确保 abort() 发出的 finished 信号不会进入
        // onReplyFinished（该函数也会清理 m_activeReply 并 emit 信号）。
        reply->disconnect();
        reply->abort();
        reply->deleteLater();
    }

    qWarning() << "[WeatherManager] 天气请求超时 (" << m_timeoutMs << "ms)";
    if (!m_currentData.isValid()) {
        m_currentData = {};
    }
    emit weatherFetchFailed();
}

// ---------------------------------------------------------------------------
// JSON 解析
// ---------------------------------------------------------------------------

void WeatherManager::parseResponse(const QByteArray &data)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "[WeatherManager] JSON 解析错误:" << err.errorString();
        return;
    }

    const QJsonObject root = doc.object();

    // --- current_condition ---
    const QJsonArray cc = root.value(QStringLiteral("current_condition")).toArray();
    if (cc.isEmpty()) {
        qWarning() << "[WeatherManager] current_condition 缺失";
        return;
    }

    const QJsonObject cond = cc[0].toObject();
    WeatherData wd;

    wd.tempC        = cond.value(QStringLiteral("temp_C")).toString();
    wd.feelsLikeC   = cond.value(QStringLiteral("FeelsLikeC")).toString();
    wd.humidity     = cond.value(QStringLiteral("humidity")).toString();
    wd.obsDateTime  = cond.value(QStringLiteral("localObsDateTime")).toString();

    // weatherDesc 嵌套数组
    const QJsonArray wda = cond.value(QStringLiteral("weatherDesc")).toArray();
    if (!wda.isEmpty()) {
        wd.description = wda[0].toObject()
                             .value(QStringLiteral("value")).toString();
    }

    // --- nearest_area ---
    const QJsonArray na = root.value(QStringLiteral("nearest_area")).toArray();
    if (!na.isEmpty()) {
        const QJsonArray areaNameArr = na[0].toObject()
                                            .value(QStringLiteral("areaName")).toArray();
        if (!areaNameArr.isEmpty()) {
            wd.areaName = areaNameArr[0].toObject()
                                       .value(QStringLiteral("value")).toString();
        }
    }

    // 清洗
    wd.description = sanitize(wd.description);
    wd.areaName    = sanitize(wd.areaName);

    // 有效性校验
    if (wd.description.isEmpty() || wd.tempC.isEmpty()) {
        qWarning() << "[WeatherManager] 解析后字段不完整:"
                    << "tempC=" << wd.tempC
                    << "desc=" << wd.description;
        return;
    }

    m_currentData = wd;
}

// ---------------------------------------------------------------------------
// 辅助方法
// ---------------------------------------------------------------------------

void WeatherManager::cleanupRequest()
{
    if (m_activeReply) {
        m_activeReply->disconnect();
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
    m_timeoutTimer.stop();
    m_requestPending = false;
}

QString WeatherManager::sanitize(const QString &text) const
{
    // 去除控制字符 (0x00-0x1F 除了 \t \n)
    QString result = text;
    result.remove(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")));
    result = result.trimmed();
    if (result.length() > 200)
        result = result.left(200);
    return result;
}
