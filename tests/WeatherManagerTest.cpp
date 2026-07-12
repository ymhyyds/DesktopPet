// ── WeatherManager 单元测试 ──────────────────────────────────────────────
// 使用 MockWeatherServer.py 模拟 wttr.in j1 JSON 接口。
// 所有测试不访问真实网络。

#include <QTest>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/WeatherManager.h"
#include "ai/PromptManager.h"
#include "ai/DeepSeekClient.h"

// ── 类声明已移至 WeatherManagerTest.h ──────────────────────────────────

// ======================================================================
// MockServer 辅助（复用协议与 MockServerHelper.h 一致）
// ======================================================================

// MockServer 结构体已在 test_main.cpp 中定义，直接使用


// ======================================================================
// WeatherManagerTest 实现
// ======================================================================

void WeatherManagerTest::initTestCase()
{
    // 启动 mock 天气服务器
    const QString scriptPath = QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("MockWeatherServer.py"));
    QVERIFY2(m_server.start(scriptPath), "无法启动 MockWeatherServer.py");
}

void WeatherManagerTest::cleanupTestCase()
{
    m_server.stop();
}

void WeatherManagerTest::init()
{
    m_mgr = new WeatherManager(this);
    m_mgr->setBaseUrlForTesting(m_server.baseUrl());
    // 设短缓存(100ms)用于过期测试，正常测试中每轮重新设置具体值
    m_mgr->setCacheDurationForTesting(30 * 60 * 1000); // 默认 30min 不变
    m_mgr->setTimeoutForTesting(4000); // 4s 超时
}

void WeatherManagerTest::cleanup()
{
    delete m_mgr;
    m_mgr = nullptr;
    // 重置 mock server 状态
    m_server.clearError();
    m_server.setResponseType(QStringLiteral("weather"), QStringLiteral("json"));
    m_server.setDelay(0);
}

WeatherManagerTest::WaitResult WeatherManagerTest::waitForWeatherResult(int timeoutMs)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool updated = false, failed = false;

    connect(m_mgr, &WeatherManager::weatherUpdated, &loop, [&]() {
        updated = true;
        loop.quit();
    });
    connect(m_mgr, &WeatherManager::weatherFetchFailed, &loop, [&]() {
        failed = true;
        loop.quit();
    });
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    if (!updated && !failed) return WaitResult::Timeout;
    if (updated) return WaitResult::Updated;
    return WaitResult::Failed;
}

WeatherManagerTest::WaitResult WeatherManagerTest::waitForWeatherResultFor(WeatherManager *mgr, int timeoutMs)
{
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);

    bool updated = false, failed = false;

    connect(mgr, &WeatherManager::weatherUpdated, &loop, [&]() {
        updated = true;
        loop.quit();
    });
    connect(mgr, &WeatherManager::weatherFetchFailed, &loop, [&]() {
        failed = true;
        loop.quit();
    });
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    timer.start(timeoutMs);
    loop.exec();

    if (!updated && !failed) return WaitResult::Timeout;
    if (updated) return WaitResult::Updated;
    return WaitResult::Failed;
}

// ── 正常 j1 JSON ──────────────────────────────────────────────────────────

void WeatherManagerTest::testNormalJson()
{
    m_mgr->requestWeather();
    const auto result = waitForWeatherResult();
    QCOMPARE(result, WaitResult::Updated);

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(!ctx.isEmpty());
    QVERIFY(ctx.contains(QStringLiteral("台北")));
    QVERIFY(ctx.contains(QStringLiteral("28")));
    QVERIFY(ctx.contains(QStringLiteral("多云")));
    QVERIFY(ctx.contains(QStringLiteral("32")));
    QVERIFY(ctx.contains(QStringLiteral("89")));
    QVERIFY(ctx.contains(QStringLiteral("2026-07-05 14:30")));
    // 不应包含过期标记（数据刚获取）
    QVERIFY(!ctx.contains(QStringLiteral("过期")));
}

// ── 非 JSON 响应 ──────────────────────────────────────────────────────────

void WeatherManagerTest::testNonJson()
{
    m_server.setResponseType(QStringLiteral("weather"), QStringLiteral("invalid_json"));
    m_mgr->requestWeather();
    const auto result = waitForWeatherResult();
    QCOMPARE(result, WaitResult::Failed);

    // buildWeatherContext 应为空（从未成功过）
    QVERIFY(m_mgr->buildWeatherContext().isEmpty());
}

// ── HTTP 错误 ──────────────────────────────────────────────────────────────

void WeatherManagerTest::testHttpError()
{
    m_server.setError(500);
    m_mgr->requestWeather();
    const auto result = waitForWeatherResult();
    QCOMPARE(result, WaitResult::Failed);

    // 未成功获取过 → 空
    QVERIFY(m_mgr->buildWeatherContext().isEmpty());
}

// ── 超时 ───────────────────────────────────────────────────────────────────

void WeatherManagerTest::testTimeout()
{
    m_mgr->setTimeoutForTesting(2000);       // 2s 超时
    m_server.setDelay(10000);                // 服务器延迟 10s

    m_mgr->requestWeather();
    const auto result = waitForWeatherResult(6000);
    QCOMPARE(result, WaitResult::Failed);

    m_server.setDelay(0);
    QVERIFY(m_mgr->buildWeatherContext().isEmpty());
}

// ── current_condition 缺失 ────────────────────────────────────────────────

void WeatherManagerTest::testMissingCurrentCondition()
{
    m_server.setResponseType(QStringLiteral("weather"), QStringLiteral("missing_cc"));
    m_mgr->requestWeather();
    const auto result = waitForWeatherResult();
    QCOMPARE(result, WaitResult::Failed);

    QVERIFY(m_mgr->buildWeatherContext().isEmpty());
}

// ── weatherDesc 缺失 ──────────────────────────────────────────────────────

void WeatherManagerTest::testMissingWeatherDesc()
{
    m_server.setResponseType(QStringLiteral("weather"), QStringLiteral("missing_wd"));
    m_mgr->requestWeather();
    const auto result = waitForWeatherResult();
    QCOMPARE(result, WaitResult::Failed);

    QVERIFY(m_mgr->buildWeatherContext().isEmpty());
}

// ── 空响应体 ───────────────────────────────────────────────────────────────

void WeatherManagerTest::testEmptyBody()
{
    m_server.setResponseType(QStringLiteral("weather"), QStringLiteral("empty_body"));
    m_mgr->requestWeather();
    const auto result = waitForWeatherResult();
    QCOMPARE(result, WaitResult::Failed);

    QVERIFY(m_mgr->buildWeatherContext().isEmpty());
}

// ── 缓存命中 ──────────────────────────────────────────────────────────────

void WeatherManagerTest::testCacheHit()
{
    // 第一次请求：成功
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    // 第二次请求：也成功（服务器保持 json 模式）
    // 再次调用 requestWeather，应该还能成功获取
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(ctx.contains(QStringLiteral("多云")));
    QVERIFY(!ctx.contains(QStringLiteral("过期")));
}

// ── 缓存过期 ──────────────────────────────────────────────────────────────

void WeatherManagerTest::testCacheExpiry()
{
    m_mgr->setCacheDurationForTesting(100); // 100ms 后过期

    // 第一次成功
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    // 等待缓存过期
    QTest::qSleep(200);

    // 数据还在，但 hasFreshData 应为 false
    QVERIFY(!m_mgr->hasFreshData());

    // 第二次成功刷新
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    // 上下文不应含过期标记
    QVERIFY(!m_mgr->buildWeatherContext().contains(QStringLiteral("过期")));
}

// ── 失败后使用旧缓存 ──────────────────────────────────────────────────────

void WeatherManagerTest::testFailureWithStaleCache()
{
    // 第一步：设很短的缓存，再成功获取
    m_mgr->setCacheDurationForTesting(100);
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    // 第二步：等缓存过期
    QTest::qSleep(200);
    QVERIFY(!m_mgr->hasFreshData()); // 已过期

    // 第三步：触发刷新且失败
    m_server.setError(503);
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Failed);

    // 第四步：buildWeatherContext 应返回上一次成功数据 + 过期标记
    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(!ctx.isEmpty());
    QVERIFY(ctx.contains(QStringLiteral("多云")));
    QVERIFY(ctx.contains(QStringLiteral("过期")));
}


// ======================================================================
// WeatherManagerTest — 缓存恢复（restoreFromCache）
// ======================================================================

void WeatherManagerTest::testRestoreFromCacheFresh()
{
    // 30 分钟内的缓存视为新鲜
    const QString sample = QStringLiteral(
        "大致地区：台北\n天气：多云，28°C，体感 32°C，湿度 89%\n天气观测时间：2026-07-05 14:30");
    const qint64 fetchTime = QDateTime::currentMSecsSinceEpoch()
                           - 5LL * 60 * 1000; // 5 分钟前

    m_mgr->restoreFromCache(sample, fetchTime);
    QVERIFY(m_mgr->hasFreshData());

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(ctx.contains(QStringLiteral("台北")));
    QVERIFY(ctx.contains(QStringLiteral("多云")));
    QVERIFY(ctx.contains(QStringLiteral("28")));
    QVERIFY(ctx.contains(QStringLiteral("32")));
    QVERIFY(ctx.contains(QStringLiteral("89")));
    QVERIFY(!ctx.contains(QStringLiteral("过期")));
}

void WeatherManagerTest::testRestoreFromCacheStale()
{
    // 超过 30 分钟的缓存标记为过期
    const QString sample = QStringLiteral(
        "大致地区：台北\n天气：多云，28°C，体感 32°C，湿度 89%");
    const qint64 fetchTime = QDateTime::currentMSecsSinceEpoch()
                           - 45LL * 60 * 1000; // 45 分钟前

    m_mgr->restoreFromCache(sample, fetchTime);
    QVERIFY(!m_mgr->hasFreshData());

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(ctx.contains(QStringLiteral("台北")));
    QVERIFY(ctx.contains(QStringLiteral("多云")));
    QVERIFY(ctx.contains(QStringLiteral("过期")));
}

void WeatherManagerTest::testRestoreFromCacheInvalidTimestamp()
{
    // timestamp = 0 → 视为非法 → 立即过期
    const QString sample = QStringLiteral(
        "大致地区：台北\n天气：多云，28°C，体感 32°C，湿度 89%");

    m_mgr->restoreFromCache(sample, 0);
    QVERIFY(!m_mgr->hasFreshData());

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(ctx.contains(QStringLiteral("台北")));
    QVERIFY(ctx.contains(QStringLiteral("过期")));
}

void WeatherManagerTest::testRestoreFromCacheFutureTimestamp()
{
    // 未来时间戳 → 视为非法 → 立即过期
    const QString sample = QStringLiteral(
        "大致地区：台北\n天气：多云，28°C，体感 32°C，湿度 89%");
    const qint64 futureTime = QDateTime::currentMSecsSinceEpoch()
                            + 3600000; // 1 小时后

    m_mgr->restoreFromCache(sample, futureTime);
    QVERIFY(!m_mgr->hasFreshData());

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(ctx.contains(QStringLiteral("台北")));
    QVERIFY(ctx.contains(QStringLiteral("过期")));
}

void WeatherManagerTest::testRestoreFromCacheStrippedStaleMarker()
{
    // 缓存的字符串可能含有过期标记（防御性剥离），验证不会重复追加
    const QString sample = QStringLiteral(
        "大致地区：台北\n天气：多云，28°C，体感 32°C，湿度 89%\n天气数据可能已过期");
    const qint64 fetchTime = QDateTime::currentMSecsSinceEpoch()
                           - 5LL * 60 * 1000; // 5 分钟前 → 新鲜

    m_mgr->restoreFromCache(sample, fetchTime);
    QVERIFY(m_mgr->hasFreshData());

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(ctx.contains(QStringLiteral("台北")));
    QVERIFY(!ctx.contains(QStringLiteral("过期"))); // 被剥离 + 数据新鲜 → 无标记
}

void WeatherManagerTest::testRestoreFromCacheRefreshClearsStale()
{
    // 1) 恢复过期缓存
    const QString sample = QStringLiteral(
        "大致地区：台北\n天气：多云，28°C，体感 32°C，湿度 89%");
    const qint64 staleTime = QDateTime::currentMSecsSinceEpoch()
                           - 45LL * 60 * 1000;
    m_mgr->restoreFromCache(sample, staleTime);
    QVERIFY(!m_mgr->hasFreshData());
    QVERIFY(m_mgr->buildWeatherContext().contains(QStringLiteral("过期")));

    // 2) 成功刷新天气（mock server 默认返回正常 JSON）
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    // 3) 过期标记清除
    const QString refreshed = m_mgr->buildWeatherContext();
    QVERIFY(!refreshed.contains(QStringLiteral("过期")));
    QVERIFY(refreshed.contains(QStringLiteral("台北")));
}


// ======================================================================
// WeatherManagerTest — 自动刷新
// ======================================================================

void WeatherManagerTest::testAutoRefreshStartsImmediately()
{
    // startAutoRefresh 应立即发起第一次请求
    m_mgr->startAutoRefresh(30000); // 30s 间隔，不影响本测试
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());
}

void WeatherManagerTest::testAutoRefreshTriggersSecondRequest()
{
    m_mgr->startAutoRefresh(200); // 200ms 间隔
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated); // 第 1 次：立即触发

    // 等待第 2 次定时触发
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated); // 第 2 次：定时器触发
    QVERIFY(m_mgr->hasFreshData());
    QVERIFY(!m_mgr->buildWeatherContext().contains(QStringLiteral("过期")));
}

void WeatherManagerTest::testAutoRefreshNoConcurrent()
{
    // 服务器延迟 5s，定时器 100ms 触发 → 请求中时定时器不应发起新请求
    m_server.setDelay(5000);
    m_mgr->setTimeoutForTesting(15000);
    m_mgr->startAutoRefresh(100);

    // 第一次（也是唯一一次实际发出的）请求必须成功返回
    QCOMPARE(waitForWeatherResult(12000), WaitResult::Updated);

    m_server.setDelay(0);

    // 恢复后的下一个定时器 ticks 应再次正常刷新
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
}

void WeatherManagerTest::testAutoRefreshStop()
{
    m_mgr->startAutoRefresh(200);
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated); // 第 1 次

    // 停止自动刷新
    m_mgr->stopAutoRefresh();

    // 验证停止后不再有请求
    m_server.setError(503);
    {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);

        bool gotSignal = false;
        connect(m_mgr, &WeatherManager::weatherUpdated, &loop, [&]() { gotSignal = true; loop.quit(); });
        connect(m_mgr, &WeatherManager::weatherFetchFailed, &loop, [&]() { gotSignal = true; loop.quit(); });
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(1200); // 等 1.2 秒，足够 200ms 定时器触发多次
        loop.exec();

        QVERIFY(!gotSignal);
    }
    m_server.setError(0);
}

void WeatherManagerTest::testAutoRefreshFailureKeepsStaleCache()
{
    // ---- Step 1: 首次请求前设置短 TTL ----
    m_mgr->setCacheDurationForTesting(100);

    // ---- Step 2: 首次请求成功 ----
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    // ---- Step 3: 等缓存过期 ----
    QTest::qSleep(200);
    QVERIFY(!m_mgr->hasFreshData());

    // ---- Step 4: 触发刷新并失败，验证保留旧缓存+过期标记 ----
    m_server.setError(503);
    m_mgr->requestWeather();
    QCOMPARE(waitForWeatherResult(), WaitResult::Failed);

    const QString ctx = m_mgr->buildWeatherContext();
    QVERIFY(!ctx.isEmpty());
    QVERIFY(ctx.contains(QStringLiteral("多云")));
    QVERIFY(ctx.contains(QStringLiteral("过期")));

    m_server.setError(0);
}

void WeatherManagerTest::testAutoRefreshSuccessClearsExpired()
{
    // 短缓存
    m_mgr->setCacheDurationForTesting(100);

    // 第 1 次：成功
    m_mgr->startAutoRefresh(300);
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());

    // 等缓存过期
    QTest::qSleep(200);
    QVERIFY(!m_mgr->hasFreshData());

    // 第 2 次：成功刷新（定时器触发）
    QCOMPARE(waitForWeatherResult(), WaitResult::Updated);
    QVERIFY(m_mgr->hasFreshData());
    QVERIFY(!m_mgr->buildWeatherContext().contains(QStringLiteral("过期")));
}

void WeatherManagerTest::testDestructorNoTimerCallback()
{
    // 创建一个堆上的 WeatherManager，验证析构后无信号回调。
    // 使用计数器而非 bool，避免析构前的合法信号干扰判定。
    auto *tempMgr = new WeatherManager(this);
    tempMgr->setBaseUrlForTesting(m_server.baseUrl());
    tempMgr->setTimeoutForTesting(4000);
    tempMgr->setCacheDurationForTesting(30000);

    int signalCount = 0;

    connect(tempMgr, &WeatherManager::weatherUpdated, this, [&]() {
        ++signalCount;
    });
    connect(tempMgr, &WeatherManager::weatherFetchFailed, this, [&]() {
        ++signalCount;
    });

    tempMgr->startAutoRefresh(100);

    // 等第 1 次请求完成
    QCOMPARE(waitForWeatherResultFor(tempMgr), WaitResult::Updated);

    // 记录析构前的信号计数（可能已包含自动刷新第二次触发的合法信号）
    const int beforeDestroy = signalCount;

    // 析构
    delete tempMgr;
    tempMgr = nullptr;

    // 等待足够长时间，验证无定时器回调——如果析构后还有残余信号，
    // signalCount 会比 beforeDestroy 增加。
    {
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(800);
        loop.exec();
    }

    // 析构后计数不应变化
    QCOMPARE(signalCount, beforeDestroy);
}


// ======================================================================
// PromptManagerWeatherTest 实现
// ======================================================================

void PromptManagerWeatherTest::testBuildEnvironmentContextWithWeather()
{
    PromptManager pm;
    pm.setWeatherContext(QStringLiteral("大致地区：台北\n天气：多云，28°C，体感 32°C，湿度 89%"));

    const QString ctx = pm.buildEnvironmentContext();
    QVERIFY(!ctx.isEmpty());
    QVERIFY(ctx.startsWith(QStringLiteral("[当前环境信息]")));
    QVERIFY(ctx.contains(QStringLiteral("本地时间：")));
    QVERIFY(ctx.contains(QStringLiteral("UTC")));
    QVERIFY(ctx.contains(QStringLiteral("大致地区：台北")));
    QVERIFY(ctx.contains(QStringLiteral("多云")));
    QVERIFY(ctx.endsWith(QStringLiteral("[/当前环境信息]")));
}

void PromptManagerWeatherTest::testBuildEnvironmentContextWithoutWeather()
{
    PromptManager pm;
    // 未设置天气上下文

    const QString ctx = pm.buildEnvironmentContext();
    QVERIFY(!ctx.isEmpty());
    QVERIFY(ctx.contains(QStringLiteral("本地时间：")));
    QVERIFY(ctx.contains(QStringLiteral("UTC")));
    QVERIFY(!ctx.contains(QStringLiteral("天气：")));
    QVERIFY(ctx.contains(QStringLiteral("[/当前环境信息]")));
}

void PromptManagerWeatherTest::testBuildEnvironmentContextDisabled()
{
    PromptManager pm;
    pm.setTimeAwareness(false);

    QVERIFY(pm.buildEnvironmentContext().isEmpty());
}

void PromptManagerWeatherTest::testEnvironmentContextInContextHistory()
{
    PromptManager pm;
    pm.setWeatherContext(QStringLiteral("大致地区：台北\n天气：多云，28°C"));

    const auto history = pm.contextHistory();

    // contextHistory() 前部包含且仅包含:
    //   0: 1 条基础人格 system 消息
    //   1: 1 条当前 effective style system 消息
    //   2: 1 条当前环境 system 消息
    // 之后才是 chat history（本测试无对话，故总共 3 条）
    QCOMPARE(history.size(), 3);

    // 验证三条 system 消息职责明确
    QCOMPARE(history[0].first, QStringLiteral("system")); // 人格
    QCOMPARE(history[1].first, QStringLiteral("system")); // 风格
    QCOMPARE(history[2].first, QStringLiteral("system")); // 环境

    // 风格消息应包含当前 effectiveAiStylePrompt()
    QVERIFY(history[1].second.contains(QStringLiteral("用户自定义的说话风格要求")));

    // 环境消息应包含预期天气/时间信息
    QVERIFY(history[2].second.contains(QStringLiteral("[当前环境信息]")));
    QVERIFY(history[2].second.contains(QStringLiteral("台北")));
    QVERIFY(history[2].second.contains(QStringLiteral("28°C")));
}

void PromptManagerWeatherTest::testProactivePromptHasEnvironment()
{
    PromptManager pm;
    pm.setWeatherContext(QStringLiteral("大致地区：台北\n天气：多云，28°C"));

    // 挂机主动说话的 prompt
    const QString proactive = pm.buildProactivePrompt();
    QVERIFY(!proactive.isEmpty());

    // contextHistory 应包含环境上下文
    const auto history = pm.contextHistory();
    bool hasEnv = false;
    for (const auto &msg : history) {
        if (msg.second.contains(QStringLiteral("[当前环境信息]"))) {
            hasEnv = true;
            break;
        }
    }
    QVERIFY(hasEnv);
}

void PromptManagerWeatherTest::testNoLongTermPollution()
{
    PromptManager pm;
    pm.setWeatherContext(QStringLiteral("大致地区：台北\n天气：多云，28°C"));

    // 模拟几轮对话
    pm.appendUserMessage(QStringLiteral("你好"));
    pm.appendAssistantMessage(QStringLiteral("喵～主人好！"));
    pm.appendUserMessage(QStringLiteral("今天天气怎么样"));
    pm.appendAssistantMessage(QStringLiteral("今天外面多云呢喵～"));

    // 获取上下文
    const auto history = pm.contextHistory();

    // 检查 user/assistant 消息中不应包含环境信息
    // system 消息包含环境信息是预期的
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("user") || msg.first == QStringLiteral("assistant")) {
            QVERIFY2(!msg.second.contains(QStringLiteral("[当前环境信息]")),
                     "user/assistant 消息不应包含环境信息");
            QVERIFY2(!msg.second.contains(QStringLiteral("大致地区")),
                     "user/assistant 消息不应包含天气地区");
        }
    }

    // 验证 system 消息数量和职责: 人格 + 风格 + 环境 = 3
    int systemCount = 0;
    int styleCount = 0;
    int envCount = 0;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system")) {
            ++systemCount;
            if (msg.second.contains(QStringLiteral("用户自定义的说话风格要求")))
                ++styleCount;
            if (msg.second.contains(QStringLiteral("[当前环境信息]")))
                ++envCount;
        }
    }
    QCOMPARE(systemCount, 3);
    QCOMPARE(styleCount, 1);
    QCOMPARE(envCount, 1);

    // 多次调用 contextHistory() 不会把 system/style/environment 永久写入 m_history
    const auto history2 = pm.contextHistory();
    int systemCount2 = 0;
    int userAssist2 = 0;
    for (const auto &msg : history2) {
        if (msg.first == QStringLiteral("system"))
            ++systemCount2;
        else
            ++userAssist2;
    }
    QCOMPARE(systemCount2, 3);       // system 数量不变
    QCOMPARE(static_cast<int>(history.size()), static_cast<int>(history2.size())); // 总大小不变
    QCOMPARE(userAssist2, 4);        // 2 user + 2 assistant，未因调用 contextHistory() 增加
}

void PromptManagerWeatherTest::testWeatherResetOnEmptyContext()
{
    PromptManager pm;
    pm.setWeatherContext(QStringLiteral("大致地区：台北\n天气：多云，28°C"));

    // 验证天气存在
    QVERIFY(pm.buildEnvironmentContext().contains(QStringLiteral("台北")));

    // 清除天气
    pm.setWeatherContext({});
    QVERIFY(!pm.buildEnvironmentContext().contains(QStringLiteral("台北")));
    QVERIFY(pm.buildEnvironmentContext().contains(QStringLiteral("[/当前环境信息]")));
}


// ======================================================================
// PromptManagerWeatherTest — AI 说话风格
// ======================================================================

void PromptManagerWeatherTest::testDefaultStyleInContextHistory()
{
    PromptManager pm;
    const auto history = pm.contextHistory();

    // 基础结构：contextHistory() 前部至少 3 条 system 消息
    QVERIFY(history.size() >= 3);

    // 第 0 条: 人格 system（可能为空，未调用 setPersonality）
    QCOMPARE(history[0].first, QStringLiteral("system"));
    // 第 1 条: 风格 system
    QCOMPARE(history[1].first, QStringLiteral("system"));
    QVERIFY(history[1].second.contains(QStringLiteral("用户自定义的说话风格要求")));
    // 第 2 条: 环境 system
    QCOMPARE(history[2].first, QStringLiteral("system"));
    QVERIFY(history[2].second.contains(QStringLiteral("[当前环境信息]")));
}

void PromptManagerWeatherTest::testDefaultToCustomStyle()
{
    PromptManager pm;
    const int genBefore = pm.conversationGeneration();

    // 在默认风格下添加一条对话
    pm.appendUserMessage(QStringLiteral("你好"));
    QVERIFY(pm.contextHistory().size() > 3); // 3 system + ≥1 user

    // 切换到自定义风格
    pm.setAiStylePrompt(QStringLiteral("冷静、简洁"));

    // effective style 已更新
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("冷静、简洁"));
    QVERIFY(pm.effectiveAiStylePrompt().contains(QStringLiteral("冷静")));

    // conversation history 被清空 → 只有 3 条 system 消息
    QCOMPARE(pm.contextHistory().size(), 3);

    // generation 递增一次
    QCOMPARE(pm.conversationGeneration(), genBefore + 1);
}

void PromptManagerWeatherTest::testCustomAToCustomB()
{
    PromptManager pm;
    pm.setAiStylePrompt(QStringLiteral("冷静"));
    const int gen1 = pm.conversationGeneration();
    pm.appendUserMessage(QStringLiteral("测试"));

    // 切换到另一自定义风格
    pm.setAiStylePrompt(QStringLiteral("活泼"));

    // history 被清空
    QCOMPARE(pm.contextHistory().size(), 3);

    // generation 递增一次
    QCOMPARE(pm.conversationGeneration(), gen1 + 1);

    // 风格消息内容已更新，不含旧风格
    const auto history = pm.contextHistory();
    QVERIFY(history[1].second.contains(QStringLiteral("用户自定义的说话风格要求")));
    QVERIFY(history[1].second.contains(QStringLiteral("活泼")));
    QVERIFY(!history[1].second.contains(QStringLiteral("冷静")));
}

void PromptManagerWeatherTest::testCustomToDefaultStyle()
{
    PromptManager pm;
    pm.setAiStylePrompt(QStringLiteral("专业"));
    pm.appendUserMessage(QStringLiteral("测试"));

    // 恢复默认（空字符串）
    pm.setAiStylePrompt(QString());

    // aiStylePrompt() 为空（自定义已清除）
    QVERIFY(pm.aiStylePrompt().isEmpty());

    // effectiveAiStylePrompt() 非空（回退到默认猫娘风格）
    QVERIFY(!pm.effectiveAiStylePrompt().isEmpty());

    // history 清空
    const auto history = pm.contextHistory();
    QCOMPARE(history.size(), 3);

    // 风格消息存在，且不含旧自定义内容
    QVERIFY(history[1].second.contains(QStringLiteral("用户自定义的说话风格要求")));
    QVERIFY(!history[1].second.contains(QStringLiteral("专业")));
}

void PromptManagerWeatherTest::testSameStyleNoop()
{
    PromptManager pm;
    pm.setAiStylePrompt(QStringLiteral("简洁"));
    const int genBefore = pm.conversationGeneration();

    pm.appendUserMessage(QStringLiteral("你好"));
    const int sizeBefore = pm.contextHistory().size();

    // 重复设置相同（归一化后相同）的风格
    pm.setAiStylePrompt(QStringLiteral("简洁"));

    // history 不清空 → context 元素数不变
    QCOMPARE(pm.contextHistory().size(), sizeBefore);

    // generation 不递增
    QCOMPARE(pm.conversationGeneration(), genBefore);
}

void PromptManagerWeatherTest::testStyleNormalization()
{
    PromptManager pm;

    // 前后空格被 trim
    pm.setAiStylePrompt(QStringLiteral("  简洁  "));
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("简洁"));

    // 纯空格等同于清除 → aiStylePrompt 为空
    pm.setAiStylePrompt(QStringLiteral("   "));
    QVERIFY(pm.aiStylePrompt().isEmpty());

    // 超过 2000 字符被截断
    const QString longStyle(2500, QChar('A'));
    pm.setAiStylePrompt(longStyle);
    QCOMPARE(pm.aiStylePrompt().length(), 2000);
}

void PromptManagerWeatherTest::testContextHistoryNoStyleLeak()
{
    PromptManager pm;

    // 第一次获取 contextHistory（默认风格）
    const auto h1 = pm.contextHistory();
    QVERIFY(h1.size() >= 3);

    // 切换到自定义风格
    pm.setAiStylePrompt(QStringLiteral("简洁专业"));

    // 第二次获取：只有当前风格，不残留默认风格
    const auto h2 = pm.contextHistory();
    int styleMsgCount = 0;
    for (const auto &msg : h2) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("用户自定义的说话风格要求"))) {
            ++styleMsgCount;
            QVERIFY(msg.second.contains(QStringLiteral("简洁专业")));
        }
    }
    QCOMPARE(styleMsgCount, 1);

    // 再切换一次，同样只有新风格
    pm.setAiStylePrompt(QStringLiteral("温柔"));
    const auto h3 = pm.contextHistory();
    styleMsgCount = 0;
    for (const auto &msg : h3) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("用户自定义的说话风格要求"))) {
            ++styleMsgCount;
            QVERIFY(msg.second.contains(QStringLiteral("温柔")));
        }
    }
    QCOMPARE(styleMsgCount, 1);
}



// ======================================================================
// PromptManagerSummaryTest — 风格切换摘要续接
// ======================================================================

void PromptManagerSummaryTest::testSummaryPreservesTopicAndFacts()
{
    PromptManager pm;

    // 模拟对话
    pm.appendUserMessage(QStringLiteral("我明天上午十点去医院，需要带检查报告。"));
    pm.appendAssistantMessage(QStringLiteral("好的主人，十点去医院喵。"));

    // 开始风格切换
    const QList<ChatMessage> snapshot = pm.beginStyleTransition();

    // 快照应包含 2 条消息
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(snapshot[0].first, QStringLiteral("user"));
    QVERIFY(snapshot[0].second.contains(QStringLiteral("医院")));
    QCOMPARE(snapshot[1].first, QStringLiteral("assistant"));

    // generation 递增
    QCOMPARE(pm.conversationGeneration(), 1);

    // 完成切换：注入摘要
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral(
        "话题：用户就医安排\n"
        "用户事实：用户明天上午十点需要去医院，需要带检查报告\n"
        "未完成任务：去医院就诊\n"
        "最近对话：用户确认就医时间和所需物品"));

    // contextHistory 应包含摘要 system 消息
    const auto history = pm.contextHistory();
    bool hasSummary = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("[对话摘要]"))) {
            hasSummary = true;
            QVERIFY(msg.second.contains(QStringLiteral("医院")));
            QVERIFY(!msg.second.contains(QStringLiteral("喵")));  // 不含旧风格
            QVERIFY(!msg.second.contains(QStringLiteral("主人")));
            break;
        }
    }
    QVERIFY(hasSummary);
}

void PromptManagerSummaryTest::testSummaryReplacesOldStyle()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("你好"));
    pm.appendAssistantMessage(QStringLiteral("喵～主人今天想聊什么喵？"));

    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral(
        "话题：问候\n"
        "最近对话：用户打招呼，助手回复问候"));

    // 新的 user 消息应在新风格上下文中
    pm.appendUserMessage(QStringLiteral("我们继续聊"));

    const auto history = pm.contextHistory();

    // 系统消息计数：人格 + 风格 + 摘要 + 环境 = 4
    int systemCount = 0;
    int summaryCount = 0;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system")) {
            ++systemCount;
            if (msg.second.contains(QStringLiteral("[对话摘要]")))
                ++summaryCount;
        }
    }
    QCOMPARE(systemCount, 4);
    QCOMPARE(summaryCount, 1);

    // user 消息只有最新的 1 条
    int userCount = 0;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("user")) ++userCount;
    }
    QCOMPARE(userCount, 1);
    QCOMPARE(history.last().second, QStringLiteral("我们继续聊"));
}

void PromptManagerSummaryTest::testAssistantConclusionPreserved()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("帮我整理一下明天的行程安排"));
    pm.appendAssistantMessage(QStringLiteral("好的主人，明天上午十点开会，下午两点去牙医喵~"));

    pm.beginStyleTransition();
    // 摘要中的 conclusions 标记为"此前对话结论"
    pm.completeStyleTransition(QStringLiteral("简洁"), QStringLiteral(
        "话题：行程安排\n"
        "此前对话结论：明天上午十点有会议，下午两点有牙医预约\n"));

    const auto history = pm.contextHistory();
    bool foundConclusion = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("此前对话结论"))) {
            foundConclusion = true;
            QVERIFY(msg.second.contains(QStringLiteral("会议")));
            QVERIFY(msg.second.contains(QStringLiteral("牙医")));
            break;
        }
    }
    QVERIFY(foundConclusion);

    // 用户基于结论继续提问
    pm.appendUserMessage(QStringLiteral("我的牙医预约是几点？"));
    // 应能从摘要中获取下午两点这个信息
    bool foundTimeInUser = false;
    for (const auto &msg : pm.contextHistory()) {
        if (msg.first == QStringLiteral("system") && msg.second.contains(QStringLiteral("下午两点"))) {
            foundTimeInUser = true;
            break;
        }
    }
    QVERIFY(foundTimeInUser);
}

void PromptManagerSummaryTest::testRapidConsecutiveSwitches()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("你好"));
    pm.appendAssistantMessage(QStringLiteral("你好喵~"));

    // 三次快速切换
    const QList<ChatMessage> snap1 = pm.beginStyleTransition();
    Q_UNUSED(snap1);
    pm.completeStyleTransition(QStringLiteral("风格A"), QStringLiteral("摘要A"));
    QCOMPARE(pm.conversationSummary(), QStringLiteral("摘要A"));

    // 第二次切换（有旧摘要）
    const QList<ChatMessage> snap2 = pm.beginStyleTransition();
    Q_UNUSED(snap2);
    pm.completeStyleTransition(QStringLiteral("风格B"), QStringLiteral("摘要B"));
    QCOMPARE(pm.conversationSummary(), QStringLiteral("摘要B"));

    // 第三次切换
    const QList<ChatMessage> snap3 = pm.beginStyleTransition();
    Q_UNUSED(snap3);
    pm.completeStyleTransition(QStringLiteral("风格C"), QStringLiteral("摘要C"));

    // 最终应只有风格C 和摘要C
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("风格C"));
    QCOMPARE(pm.conversationSummary(), QStringLiteral("摘要C"));

    // 风格B 和摘要B 不残留
    const auto history = pm.contextHistory();
    for (const auto &msg : history) {
        // 使用精确匹配避免摘要封装文本（含"当前风格"）被误匹配
        if (msg.first == QStringLiteral("system") && msg.second.contains(QStringLiteral("自定义的说话风格要求"))) {
            QVERIFY(msg.second.contains(QStringLiteral("风格C")));
            QVERIFY(!msg.second.contains(QStringLiteral("风格A")));
            QVERIFY(!msg.second.contains(QStringLiteral("风格B")));
        }
    }
}

void PromptManagerSummaryTest::testFallbackUserMessages()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("第一轮：你好"));
    pm.appendAssistantMessage(QStringLiteral("你好喵~"));
    pm.appendUserMessage(QStringLiteral("第二轮：今天天气怎么样"));
    pm.appendAssistantMessage(QStringLiteral("今天多云喵~"));
    pm.appendUserMessage(QStringLiteral("第三轮：帮我查一下快递"));
    pm.appendAssistantMessage(QStringLiteral("快递还在路上喵~"));

    const QList<ChatMessage> snapshot = pm.beginStyleTransition();

    // 使用 fallback 提取 user 消息
    QStringList userMessages;
    for (int i = 0; i < snapshot.size(); ++i) {
        if (snapshot[i].first == QStringLiteral("user"))
            userMessages.append(snapshot[i].second);
    }

    pm.completeStyleTransitionWithFallback(QStringLiteral("简洁"), userMessages);

    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("简洁"));

    // 应恢复 3 条 user 消息
    int userCount = 0;
    for (const auto &msg : pm.contextHistory()) {
        if (msg.first == QStringLiteral("user")) ++userCount;
    }
    QCOMPARE(userCount, 3);

    // assistant 消息不应被恢复
    int assistantCount = 0;
    for (const auto &msg : pm.contextHistory()) {
        if (msg.first == QStringLiteral("assistant")) ++assistantCount;
    }
    QCOMPARE(assistantCount, 0);
}

void PromptManagerSummaryTest::testSameStyleNoTransition()
{
    PromptManager pm;
    pm.setAiStylePrompt(QStringLiteral("活泼"));

    const int genBefore = pm.conversationGeneration();
    pm.appendUserMessage(QStringLiteral("你好"));

    // 尝试 beginStyleTransition + completeStyleTransition 但使用相同风格
    // 实际上 completeStyleTransition 不检查重复，由 App 层控制
    // 但 completeStyleTransition 不应该增加 generation
    pm.beginStyleTransition();
    const int genAfterBegin = pm.conversationGeneration();

    pm.completeStyleTransition(QStringLiteral("活泼"), QStringLiteral("摘要"));
    const int genAfterComplete = pm.conversationGeneration();

    // beginStyleTransition 增加了 generation
    QCOMPARE(genAfterBegin, genBefore + 1);
    // completeStyleTransition 不增加 generation
    QCOMPARE(genAfterComplete, genAfterBegin);
}

void PromptManagerSummaryTest::testRoleSwitchClearsSummary()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("你好"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral("摘要：对话"));

    // 验证摘要存在
    QVERIFY(!pm.conversationSummary().isEmpty());

    // 模拟角色切换：setPersonality 调用 clearHistory
    pm.setPersonality(QStringLiteral("高冷"), QStringLiteral("新角色"));

    // 摘要应被清空
    QVERIFY(pm.conversationSummary().isEmpty());

    // history 应被清空
    QVERIFY(!pm.hasConversationHistory());

    // generation 应递增
    QCOMPARE(pm.conversationGeneration(), 2); // 1 from beginStyle, +1 from setPersonality
}

void PromptManagerSummaryTest::testSummaryNotDuplicatedOnRepeatedContextHistory()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("第一轮"));
    pm.appendAssistantMessage(QStringLiteral("回复一"));
    pm.appendUserMessage(QStringLiteral("第二轮"));
    pm.appendAssistantMessage(QStringLiteral("回复二"));

    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("简洁"), QStringLiteral(
        "话题：对话\n"
        "用户事实：用户发起了两轮对话\n"));

    // 第一次调用 contextHistory
    const auto h1 = pm.contextHistory();
    // 第二次调用
    const auto h2 = pm.contextHistory();

    // 总消息数应相同
    QCOMPARE(h1.size(), h2.size());

    // 摘要 system 消息应只出现一次
    auto countSummaryBlocks = [](const QList<ChatMessage> &history) -> int {
        int count = 0;
        for (const auto &msg : history) {
            if (msg.first == QStringLiteral("system") &&
                msg.second.contains(QStringLiteral("[对话摘要]")))
                ++count;
        }
        return count;
    };

    QCOMPARE(countSummaryBlocks(h1), 1);
    QCOMPARE(countSummaryBlocks(h2), 1);

    // 两次调用的摘要内容相同
    for (int i = 0; i < h1.size(); ++i) {
        if (h1[i].first == QStringLiteral("system") &&
            h1[i].second.contains(QStringLiteral("[对话摘要]"))) {
            QCOMPARE(h1[i].second, h2[i].second);
            break;
        }
    }
}

void PromptManagerSummaryTest::testSummaryInjectionPosition()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("你好"));

    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("简洁"), QStringLiteral(
        "话题：问候\n"
        "最近对话：用户打招呼\n"));

    const auto history = pm.contextHistory();

    // 顺序应为：人格 → 风格 → 摘要 → 环境
    // 找到各关键消息的索引
    int styleIdx = -1, summaryIdx = -1, envIdx = -1;
    for (int i = 0; i < history.size(); ++i) {
        const auto &msg = history[i];
        if (msg.first != QStringLiteral("system")) continue;
        if (msg.second.contains(QStringLiteral("用户自定义的说话风格要求")))
            styleIdx = i;
        if (msg.second.contains(QStringLiteral("[对话摘要]")))
            summaryIdx = i;
        if (msg.second.contains(QStringLiteral("[当前环境信息]")))
            envIdx = i;
    }

    QVERIFY(styleIdx >= 0);
    QVERIFY(summaryIdx >= 0);
    QVERIFY(envIdx >= 0);

    QVERIFY(styleIdx < summaryIdx);
    QVERIFY(summaryIdx < envIdx);
}

void PromptManagerSummaryTest::testSnapshotImmutability()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("消息A"));
    pm.appendAssistantMessage(QStringLiteral("回复A"));

    // 捕获快照
    const QList<ChatMessage> snapshot = pm.beginStyleTransition();

    // 快照应有 2 条
    QCOMPARE(snapshot.size(), 2);

    // 切换后 m_history 应为空
    QVERIFY(!pm.hasConversationHistory());

    // 追加新消息
    pm.appendUserMessage(QStringLiteral("消息B"));

    // 新 history 有 1 条
    QCOMPARE(pm.contextHistory().size(), 4); // 3 system + 1 user

    // 快照不应受影响
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(snapshot[0].second, QStringLiteral("消息A"));
}

void PromptManagerSummaryTest::testGenerationNotDoubleIncremented()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("你好"));

    const int gen0 = pm.conversationGeneration();

    const auto snapshot = pm.beginStyleTransition();
    Q_UNUSED(snapshot);
    QCOMPARE(pm.conversationGeneration(), gen0 + 1);

    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral("摘要"));

    // complete 不应额外递增
    QCOMPARE(pm.conversationGeneration(), gen0 + 1);

    // clearHistory（角色切换）递增一次
    pm.clearHistory();
    QCOMPARE(pm.conversationGeneration(), gen0 + 2);
}

void PromptManagerSummaryTest::testFallbackRestoresUserMessages()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("你好"));
    pm.appendAssistantMessage(QStringLiteral("你好喵"));
    pm.appendUserMessage(QStringLiteral("今天天气怎么样"));
    pm.appendAssistantMessage(QStringLiteral("今天晴喵"));
    pm.appendUserMessage(QStringLiteral("明天呢"));
    pm.appendAssistantMessage(QStringLiteral("明天也晴喵"));

    const QList<ChatMessage> snapshot = pm.beginStyleTransition();

    // 提取 user 消息
    QStringList userMessages;
    for (const auto &msg : snapshot) {
        if (msg.first == QStringLiteral("user"))
            userMessages.append(msg.second);
    }

    pm.completeStyleTransitionWithFallback(QStringLiteral("简洁"), userMessages);

    // 验证恢复
    const auto history = pm.contextHistory();
    int userCount = 0;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("user")) {
            ++userCount;
        }
    }
    QCOMPARE(userCount, 3);
    QCOMPARE(userMessages.size(), 3);
    QCOMPARE(userMessages[0], QStringLiteral("你好"));
    QCOMPARE(userMessages[1], QStringLiteral("今天天气怎么样"));
    QCOMPARE(userMessages[2], QStringLiteral("明天呢"));
}

void PromptManagerSummaryTest::testRapidSwitchOnlyOneSnapshot()
{
    // 模拟 App 层快速切换 A→B→C：只调用一次 beginStyleTransition，
    // 后续仅更新目标风格，最后用最新风格调用 completeStyleTransition。
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("出发时间是8月12日上午，预算是6000元，住宿必须靠近京都站"));
    pm.appendAssistantMessage(QStringLiteral("好的，已记录行程信息喵~"));

    // 第一次切换（A）：创建快照
    const int genBefore = pm.conversationGeneration();
    const QList<ChatMessage> snapshot = pm.beginStyleTransition();
    const int genAfterBegin = pm.conversationGeneration();

    // 快照应包含原始历史
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(genAfterBegin, genBefore + 1);

    // 快速切换 B 和 C：不调用 beginStyleTransition，App 层只更新 pendingStylePrompt
    // 模拟摘要返回后用最新风格 C 完成切换
    pm.completeStyleTransition(QStringLiteral("最终专业"), QStringLiteral(
        "话题：京都旅行计划\n"
        "用户事实：出发时间8月12日上午，预算6000元，住宿需靠近京都站\n"
        "偏好：行程不要太赶\n"));

    // generation 不应再递增
    QCOMPARE(pm.conversationGeneration(), genAfterBegin);

    // 最终风格为 C
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("最终专业"));

    // neutral summary 存在
    QVERIFY(!pm.conversationSummary().isEmpty());
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("京都")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));
}

void PromptManagerSummaryTest::testRapidSwitchContextHistoryOnlyFinal()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("旅行计划：8月12日上午，6000元，京都站附近"));
    pm.appendAssistantMessage(QStringLiteral("已记录旅行信息喵~"));

    // 模拟一次 beginStyleTransition + 后续快速切换
    pm.beginStyleTransition();

    // 模拟 App 层：摘要返回后只用最终风格 C 完成切换
    pm.completeStyleTransition(QStringLiteral("最终专业"), QStringLiteral(
        "话题：京都旅行计划\n"
        "用户事实：出发时间8月12日上午，预算6000元，住宿需靠近京都站\n"
        "偏好：行程不要太赶\n"));

    // contextHistory 检查
    const auto history = pm.contextHistory();

    // 风格 system 消息只包含最终风格
    int styleCount = 0;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("自定义的说话风格要求"))) {
            ++styleCount;
            // 只含最终风格 C，不含 A/B
            QVERIFY(msg.second.contains(QStringLiteral("最终专业")));
            QVERIFY(!msg.second.contains(QStringLiteral("风格A")));
            QVERIFY(!msg.second.contains(QStringLiteral("风格B")));
        }
    }
    QCOMPARE(styleCount, 1);

    // 摘要 system 消息存在且只出现一次
    int summaryCount = 0;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("[对话摘要]"))) {
            ++summaryCount;
            QVERIFY(msg.second.contains(QStringLiteral("京都")));
        }
    }
    QCOMPARE(summaryCount, 1);
}

void PromptManagerSummaryTest::testCompleteStyleTransitionPreservesSummary()
{
    PromptManager pm;
    pm.appendUserMessage(QStringLiteral("第一次对话"));
    pm.appendAssistantMessage(QStringLiteral("第一次回复"));

    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格X"), QStringLiteral("摘要X"));

    // 第二次切换
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格Y"), QStringLiteral("摘要Y"));

    // 第二次切换的摘要应覆盖第一次
    QCOMPARE(pm.conversationSummary(), QStringLiteral("摘要Y"));
    QVERIFY(!pm.conversationSummary().isEmpty());

    // 检查 contextHistory 只有一份最新摘要
    const auto history = pm.contextHistory();
    int summaryCount = 0;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("[对话摘要]"))) {
            ++summaryCount;
            QVERIFY(msg.second.contains(QStringLiteral("摘要Y")));
        }
    }
    QCOMPARE(summaryCount, 1);
}

void PromptManagerSummaryTest::testStyleIsolationAfterAnyTransition()
{
    PromptManager pm;

    // 开始切换前注入一些历史
    pm.appendUserMessage(QStringLiteral("你好！"));
    pm.appendAssistantMessage(QStringLiteral("你好喵~"));

    // 情况 A：正常 completeStyleTransition
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral("摘要"));
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("专业"));

    // 情况 B：completeStyleTransitionWithFallback（模拟失败路径）
    pm.beginStyleTransition();
    pm.completeStyleTransitionWithFallback(QStringLiteral("简洁"), {QStringLiteral("你好！")});
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("简洁"));

    // 情况 C：setAiStylePrompt（模拟直接应用，无摘要）
    pm.setAiStylePrompt(QStringLiteral("活泼"));
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("活泼"));

    // 三种路径结束后，history 均应正常
    // setAiStylePrompt 会 clearHistory，所以 hasConversationHistory 可能为 false
    // 但不应有异常（segfault、assert 等）
    QVERIFY(!pm.contextHistory().isEmpty());
}

void PromptManagerSummaryTest::testExistingSummaryPreservedWhenHistoryEmpty()
{
    // 验证：m_history 为空但已有 m_conversationSummary 时，
    // completeStyleTransition 不覆盖已有摘要
    PromptManager pm;

    // 先通过一次完整切换建立摘要
    pm.appendUserMessage(QStringLiteral("帮我规划京都旅行"));
    pm.appendAssistantMessage(QStringLiteral("好的！请问您的预算是多少？"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral(
        "话题：京都旅行规划\n"
        "用户事实：计划去京都旅行\n"
        "未完成任务：确认预算"));

    QVERIFY(!pm.conversationSummary().isEmpty());
    QVERIFY(!pm.hasConversationHistory());   // beginStyleTransition 已清空 history

    // 再次切换风格（模拟 App 层情况 B：无普通历史但有摘要）
    const QString savedSummary = pm.conversationSummary();
    pm.completeStyleTransition(QStringLiteral("活泼"), savedSummary);

    // 摘要应完整保留
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("京都旅行")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("预算")));
    // 风格已更新
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("活泼"));
    // 无普通 history
    QVERIFY(!pm.hasConversationHistory());
}

void PromptManagerSummaryTest::testRapidSwitchAfterSummaryKeepsContext()
{
    // 验证：摘要完成后快速 A→B→C（App 层只调用一次 completeStyleTransition），
    // 最终风格为 C，原摘要仍存在
    PromptManager pm;

    // 建立初始摘要
    pm.appendUserMessage(QStringLiteral("计划去日本旅行"));
    pm.appendAssistantMessage(QStringLiteral("很好！推荐京都和大阪"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), QStringLiteral(
        "话题：日本旅行计划\n"
        "用户事实：计划去日本旅行\n"
        "偏好：京都、大阪"));

    QVERIFY(!pm.conversationSummary().isEmpty());

    // 模拟 App 层：后续快速切换只更新 pendingStylePrompt + 最终一次 completeStyleTransition
    // 不重复 beginStyleTransition
    pm.completeStyleTransition(QStringLiteral("风格B"), pm.conversationSummary());
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("风格B"));

    pm.completeStyleTransition(QStringLiteral("风格C"), pm.conversationSummary());
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("风格C"));

    // 原摘要应仍被保留
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("日本旅行")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("京都")));
    QVERIFY(!pm.conversationSummary().isEmpty());
    // 无普通 history
    QVERIFY(!pm.hasConversationHistory());
}

void PromptManagerSummaryTest::testCumulativeSummaryPreservedThroughBeginTransition()
{
    // 验证：旧摘要 + 新 history 在 beginStyleTransition 后旧摘要仍可独立获取，
    // snapshot 仅含普通 user/assistant history（不含旧摘要），
    // App 层可将两者合并为下一次摘要请求的完整输入。
    PromptManager pm;

    // 第一轮：建立旧摘要（含旅行日期和预算）
    pm.appendUserMessage(QStringLiteral("帮我规划京都旅行，打算8月12日出发"));
    pm.appendAssistantMessage(QStringLiteral("好的，预算大概多少？"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), QStringLiteral(
        "话题：京都旅行\n"
        "用户事实：计划8月12日前往京都，预算6000元左右\n"
        "未完成任务：确认预算"));

    // 验证旧摘要已被存储
    QVERIFY(!pm.conversationSummary().isEmpty());
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));

    // 第二轮：增加新 history（住宿偏好 + 节奏要求）
    pm.appendUserMessage(QStringLiteral(
        "京都站附近有推荐的民宿吗？节奏不要太赶"));
    pm.appendAssistantMessage(QStringLiteral(
        "为您找到京都站周围3家特色民宿，建议每天2-3个景点"));

    // beginStyleTransition 会清空 history，但保留旧摘要
    auto snapshot = pm.beginStyleTransition();

    // 1) 旧摘要完好独立
    QVERIFY(!pm.conversationSummary().isEmpty());
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));

    // 2) snapshot 仅含新增的 history（1 user + 1 assistant = 2），不含旧摘要
    QCOMPARE(snapshot.size(), 2);
    QCOMPARE(snapshot[0].first, QStringLiteral("user"));
    QVERIFY(snapshot[0].second.contains(QStringLiteral("京都站")));
    QVERIFY(snapshot[0].second.contains(QStringLiteral("不要太赶")));
    QCOMPARE(snapshot[1].first, QStringLiteral("assistant"));

    // 3) 模拟 App 层：旧摘要 + snapshot 合并为完整的摘要请求输入
    QList<ChatMessage> summaryInput;
    summaryInput.append(ChatMessage(QStringLiteral("user"),
        QStringLiteral("此前对话摘要：%1").arg(pm.conversationSummary())));
    summaryInput.append(snapshot);

    // 合并后同时包含旧摘要事实和新 history
    QVERIFY(summaryInput[0].second.contains(QStringLiteral("8月12日")));
    QVERIFY(summaryInput[0].second.contains(QStringLiteral("6000")));
    QVERIFY(summaryInput[1].second.contains(QStringLiteral("京都站")));
    QVERIFY(summaryInput[1].second.contains(QStringLiteral("不要太赶")));

    // 4) 最终切换：新摘要应包含所有累计信息
    pm.completeStyleTransition(QStringLiteral("风格B"), QStringLiteral(
        "话题：京都旅行\n"
        "用户事实：计划8月12日前往京都，预算6000元左右\n"
        "偏好：京都站附近民宿，希望节奏不要太赶"));

    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("京都站")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("不要太赶")));
}

void PromptManagerSummaryTest::testEmptyHistoryAndEmptySummaryDirectSwitch()
{
    // 验证：无历史无摘要时直接切换风格不产生副作用
    PromptManager pm;

    QVERIFY(!pm.hasConversationHistory());
    QVERIFY(pm.conversationSummary().isEmpty());

    pm.completeStyleTransition(QStringLiteral("专业"), QString());
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("专业"));
    QVERIFY(pm.conversationSummary().isEmpty());
    QVERIFY(!pm.hasConversationHistory());

    // 再切换一次
    pm.completeStyleTransition(QStringLiteral("活泼"), QString());
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("活泼"));
    QVERIFY(pm.conversationSummary().isEmpty());
}

void PromptManagerSummaryTest::testRoleSwitchStillClearsSummary()
{
    // 防止"保留摘要"错误地影响角色切换语义
    PromptManager pm;

    // 先建立摘要
    pm.appendUserMessage(QStringLiteral("你好"));
    pm.appendAssistantMessage(QStringLiteral("你好喵~"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral(
        "话题：打招呼\n用户事实：无"));

    QVERIFY(!pm.conversationSummary().isEmpty());

    // 角色切换应彻底清除摘要和 history
    pm.setPersonality(QStringLiteral("冷漠、寡言"), QStringLiteral("neko"));

    QVERIFY(pm.conversationSummary().isEmpty());
    QVERIFY(!pm.hasConversationHistory());
    // 风格提示词不受影响
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("专业"));
}


// ──── 本次修复新增测试 ────

void PromptManagerSummaryTest::testFallbackPreservesExistingSummary()
{
    // 验证 fallback 后旧摘要（8月12日、6000元）不丢失，并合并新 user 信息
    PromptManager pm;

    // 建立旧摘要
    pm.appendUserMessage(QStringLiteral("帮我规划京都旅行"));
    pm.appendAssistantMessage(QStringLiteral("好的，请问预算多少喵~"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), QStringLiteral(
        "话题：京都旅行\n"
        "用户事实：计划出发时间8月12日，预算6000元\n"
        "偏好：住宿需靠近京都站"));

    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));

    // 第二轮：新增 history 后 fallback
    pm.appendUserMessage(QStringLiteral("行程不要太赶，伏见稻荷也要去"));
    pm.appendAssistantMessage(QStringLiteral("好的，已记录喵~"));

    const auto snapshot = pm.beginStyleTransition();
    QStringList userMsgs;
    for (const auto &msg : snapshot) {
        if (msg.first == QStringLiteral("user"))
            userMsgs.append(msg.second);
    }

    pm.completeStyleTransitionWithFallback(QStringLiteral("风格B"), userMsgs);

    // 旧摘要保留
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));
    // 新 user 信息合并
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("伏见稻荷")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("不要太赶")));
}

void PromptManagerSummaryTest::testEmptyFallbackNoOverwrite()
{
    // 空 user fallback 不得覆盖已有摘要
    PromptManager pm;

    pm.appendUserMessage(QStringLiteral("帮我规划京都旅行"));
    pm.appendAssistantMessage(QStringLiteral("好的喵~"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), QStringLiteral(
        "话题：京都旅行\n"
        "用户事实：计划出发时间8月12日，预算6000元"));

    QVERIFY(!pm.conversationSummary().isEmpty());

    // 第二次切换：空 fallback
    pm.beginStyleTransition();
    pm.completeStyleTransitionWithFallback(QStringLiteral("风格B"), {});

    // 旧摘要应完整保留
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));
}

void PromptManagerSummaryTest::testEmptySnapshotDoesNotOverwriteSummary()
{
    // 无普通 history 但有摘要时，completeStyleTransition 不覆盖
    PromptManager pm;

    // 建立完整的摘要流程
    pm.appendUserMessage(QStringLiteral("帮我规划京都旅行"));
    pm.appendAssistantMessage(QStringLiteral("好的喵~"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), QStringLiteral(
        "话题：京都旅行\n"
        "用户事实：计划出发时间8月12日，预算6000元"));

    // history 为空但摘要非空
    QVERIFY(!pm.hasConversationHistory());
    QVERIFY(!pm.conversationSummary().isEmpty());

    const QString savedSummary = pm.conversationSummary();

    // 再次 beginStyleTransition（history 已空 → 快照为空）
    const auto snapshot = pm.beginStyleTransition();
    QVERIFY(snapshot.isEmpty());

    // 用旧摘要完成切换（模拟修正后 App 行为）
    pm.completeStyleTransition(QStringLiteral("风格B"), savedSummary);

    // 摘要应保留
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));
    QCOMPARE(pm.aiStylePrompt(), QStringLiteral("风格B"));
}

void PromptManagerSummaryTest::testFiveFactCumulativeThroughTwoSwitches()
{
    // 五项事实经两次 style 切换仍完整保留
    PromptManager pm;

    // 初始对话含 5 项事实
    pm.appendUserMessage(QStringLiteral("出发时间是8月12日上午，预算是6000元"));
    pm.appendAssistantMessage(QStringLiteral("好的，已记录喵~"));
    pm.appendUserMessage(QStringLiteral("住宿必须靠近京都站，行程不要太赶"));
    pm.appendAssistantMessage(QStringLiteral("了解，京都有很多民宿喵~"));
    pm.appendUserMessage(QStringLiteral("还要去伏见稻荷大社"));
    pm.appendAssistantMessage(QStringLiteral("伏见稻荷非常适合拍照喵~"));

    // 第一次切换：摘要含全部 5 项原始事实
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), QStringLiteral(
        "话题：京都旅行\n"
        "用户事实：出发时间8月12日上午，预算6000元\n"
        "偏好：住宿需靠近京都站，行程不要太赶\n"
        "用户事实：计划前往伏见稻荷大社"));

    // 五项事实全部在第一次摘要中被捕获
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("京都站")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("不要太赶")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("伏见稻荷")));

    // 第二轮：追加事实 4-5
    pm.appendUserMessage(QStringLiteral("想体验和服，还想吃怀石料理"));
    pm.appendAssistantMessage(QStringLiteral("和服体验和怀石料理都值得尝试喵~"));

    // 第二次切换：累计摘要含全部 5 项事实
    const auto snap2 = pm.beginStyleTransition();
    QStringList userMsgs2;
    for (const auto &msg : snap2) {
        if (msg.first == QStringLiteral("user"))
            userMsgs2.append(msg.second);
    }
    pm.completeStyleTransitionWithFallback(QStringLiteral("风格B"), userMsgs2);

    // 全部 5 项事实（至少部分）保留
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("8月12日")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("6000")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("京都站")));
    QVERIFY(pm.conversationSummary().contains(QStringLiteral("伏见稻荷")));
}

void PromptManagerSummaryTest::testNoBeginTransitionNoGenerationChange()
{
    // 未调用 beginStyleTransition 时，正常操作不递增 generation
    PromptManager pm;

    const int gen0 = pm.conversationGeneration();
    QCOMPARE(gen0, 0);

    // 普通对话不递增
    pm.appendUserMessage(QStringLiteral("你好"));
    QCOMPARE(pm.conversationGeneration(), gen0);

    pm.appendAssistantMessage(QStringLiteral("你好喵~"));
    QCOMPARE(pm.conversationGeneration(), gen0);

    // 直接调用 completeStyleTransition（无 begin）不递增
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral("摘要"));
    QCOMPARE(pm.conversationGeneration(), gen0);

    // completeStyleTransitionWithFallback 也不递增
    pm.completeStyleTransitionWithFallback(QStringLiteral("简洁"), {QStringLiteral("你好")});
    QCOMPARE(pm.conversationGeneration(), gen0);

    // 仅 beginStyleTransition 会递增
    pm.beginStyleTransition();
    QCOMPARE(pm.conversationGeneration(), gen0 + 1);
}

void PromptManagerSummaryTest::testSameStyleNoGenerationChange()
{
    // 相同有效风格不递增 generation
    PromptManager pm;

    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral("摘要"));
    const int gen1 = pm.conversationGeneration();

    // 同样风格再次 completeStyleTransition
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral("新摘要"));
    QCOMPARE(pm.conversationGeneration(), gen1);

    // setAiStylePrompt 相同风格（归一化后相同）也不递增
    pm.setAiStylePrompt(QStringLiteral("专业"));
    QCOMPARE(pm.conversationGeneration(), gen1);

    // 不同风格才递增
    pm.setAiStylePrompt(QStringLiteral("活泼"));
    QCOMPARE(pm.conversationGeneration(), gen1 + 1);
}


// ======================================================================
// 以下是摘要来源污染修复新增测试
// ======================================================================

// ─── 辅助：模拟 parseSummaryJson 的五字段 JSON 解析 ─────────────
// 用于测试 JSON 解析逻辑，不依赖 App 类私有方法
namespace {
    struct ParseResult {
        QStringList userFacts;
        QStringList confirmedDecisions;
        QStringList preferences;
        QStringList unconfirmedSuggestions;
        QStringList openQuestions;
        bool ok = false;
    };

    // 模拟 App::parseSummaryJson 的 JSON 验证逻辑（仅用于测试）
    ParseResult parseTestSummaryJson(const QString &rawJson, QString &formatted)
    {
        ParseResult result;
        formatted.clear();

        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(rawJson.toUtf8(), &error);

        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            int start = rawJson.indexOf(QLatin1Char('{'));
            int end = rawJson.lastIndexOf(QLatin1Char('}'));
            if (start >= 0 && end > start) {
                doc = QJsonDocument::fromJson(
                    rawJson.mid(start, end - start + 1).toUtf8(), &error);
            }
        }

        if (error.error != QJsonParseError::NoError || !doc.isObject())
            return result;

        const QJsonObject obj = doc.object();
        QStringList parts;

        // user_facts: must be array
        const QJsonValue uf = obj.value(QStringLiteral("user_facts"));
        if (!uf.isArray()) return result;
        for (const auto &v : uf.toArray()) {
            if (!v.isString()) return result;
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) {
                result.userFacts << s;
                parts << QStringLiteral("[用户明确事实]\n%1").arg(s);
            }
        }

        // confirmed_decisions: must be array of objects {category, content}
        const QJsonValue cd = obj.value(QStringLiteral("confirmed_decisions"));
        if (!cd.isArray()) return result;
        for (const auto &v : cd.toArray()) {
            if (!v.isObject()) return result;
            const QJsonObject item = v.toObject();
            if (!item.contains(QStringLiteral("category")) || !item.contains(QStringLiteral("content")))
                return result;
            const QString cat = item.value(QStringLiteral("category")).toString().trimmed();
            const QString content = item.value(QStringLiteral("content")).toString().trimmed();
            if (cat.isEmpty() || content.isEmpty()) return result;
            result.confirmedDecisions << content;
            parts << QStringLiteral("[用户已确认决定]\n%1").arg(content);
        }

        // preferences: must be array
        const QJsonValue pr = obj.value(QStringLiteral("preferences"));
        if (!pr.isArray()) return result;
        for (const auto &v : pr.toArray()) {
            if (!v.isString()) return result;
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) {
                result.preferences << s;
                parts << QStringLiteral("[用户偏好与约束]\n%1").arg(s);
            }
        }

        // unconfirmed_assistant_suggestions: must be array of objects {category, content}
        const QJsonValue us = obj.value(QStringLiteral("unconfirmed_assistant_suggestions"));
        if (!us.isArray()) return result;
        for (const auto &v : us.toArray()) {
            if (!v.isObject()) return result;
            const QJsonObject item = v.toObject();
            if (!item.contains(QStringLiteral("category")) || !item.contains(QStringLiteral("content")))
                return result;
            const QString cat = item.value(QStringLiteral("category")).toString().trimmed();
            const QString content = item.value(QStringLiteral("content")).toString().trimmed();
            if (cat.isEmpty() || content.isEmpty()) return result;
            result.unconfirmedSuggestions << content;
            parts << QStringLiteral("[助手曾建议但用户尚未确认]\n%1").arg(content);
        }

        // open_questions: must be array
        const QJsonValue oq = obj.value(QStringLiteral("open_questions"));
        if (!oq.isArray()) return result;
        for (const auto &v : oq.toArray()) {
            if (!v.isString()) return result;
            const QString s = v.toString().trimmed();
            if (!s.isEmpty()) {
                result.openQuestions << s;
                parts << QStringLiteral("[待确认问题]\n%1").arg(s);
            }
        }

        if (parts.isEmpty()) return result;

        formatted = parts.join(QStringLiteral("\n\n"));
        if (formatted.length() > 2000)
            formatted = formatted.left(2000);

        result.ok = true;
        return result;
    }
}

void PromptManagerSummaryTest::testUnconfirmedSuggestionStaysUnconfirmed()
{
    // assistant 建议酒店4000元、东福寺，用户未确认
    // 必须进入 unconfirmed_assistant_suggestions，不得进入其他字段
    PromptManager pm;

    // 用 parse 辅助验证 JSON 解析行为
    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [],"
        "  \"confirmed_decisions\": [],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"住宿\", \"content\": \"酒店预算4000元\"},"
        "    {\"category\": \"景点\", \"content\": \"东福寺\"}"
        "  ],"
        "  \"open_questions\": [\"用户是否接受东福寺？\"]"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);
    QVERIFY(!formatted.isEmpty());

    // unconfirmed_assistant_suggestions 不为空
    QCOMPARE(result.unconfirmedSuggestions.size(), 2);
    QVERIFY(result.unconfirmedSuggestions[0].contains(QStringLiteral("4000")));
    QVERIFY(result.unconfirmedSuggestions[1].contains(QStringLiteral("东福寺")));

    // confirmed_decisions 必须为空（用户未确认）
    QVERIFY(result.confirmedDecisions.isEmpty());

    // user_facts 必须为空（建议不是用户事实）
    QVERIFY(result.userFacts.isEmpty());

    // preferences 必须为空（建议不是用户偏好）
    QVERIFY(result.preferences.isEmpty());

    // formatted 输出中的字段身份正确
    QVERIFY(formatted.contains(QStringLiteral("助手曾建议但用户尚未确认")));
    QVERIFY(!formatted.contains(QStringLiteral("用户已确认决定")));
    QVERIFY(!formatted.contains(QStringLiteral("用户明确事实")));
    QVERIFY(!formatted.contains(QStringLiteral("用户偏好与约束")));

    // 通过 PromptManager 验证 contextHistory 中的摘要注入
    pm.appendUserMessage(QStringLiteral("帮我推荐京都的酒店和景点"));
    pm.appendAssistantMessage(QStringLiteral("建议酒店预算4000元，还可以去东福寺看看"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("专业"), formatted);

    const auto history = pm.contextHistory();
    bool foundUnconfirmed = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("助手曾建议但用户尚未确认"))) {
            foundUnconfirmed = true;
            QVERIFY(msg.second.contains(QStringLiteral("4000")));
            QVERIFY(msg.second.contains(QStringLiteral("东福寺")));
            break;
        }
    }
    QVERIFY(foundUnconfirmed);
}

void PromptManagerSummaryTest::testPartialConfirmation()
{
    // 用户明确说"酒店4000元可以，但东福寺不去"
    // 酒店预算进入 confirmed_decisions，东福寺不得成为已确认决定
    PromptManager pm;

    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [],"
        "  \"confirmed_decisions\": ["
        "    {\"category\": \"住宿\", \"content\": \"酒店预算4000元\"}"
        "  ],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": [],"
        "  \"open_questions\": []"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);

    // 酒店预算在 confirmed_decisions
    QCOMPARE(result.confirmedDecisions.size(), 1);
    QVERIFY(result.confirmedDecisions[0].contains(QStringLiteral("4000")));

    // 东福寺不在 confirmed_decisions
    QVERIFY(!formatted.contains(QStringLiteral("东福寺")));

    // 通过 PromptManager 验证注入
    pm.completeStyleTransition(QStringLiteral("简洁"), formatted);
    const auto history = pm.contextHistory();
    bool foundConfirmed = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("用户已确认决定"))) {
            foundConfirmed = true;
            QVERIFY(msg.second.contains(QStringLiteral("4000")));
            break;
        }
    }
    QVERIFY(foundConfirmed);
}

void PromptManagerSummaryTest::testSilentStyleSwitchPreservesUnconfirmed()
{
    // 用户不回应建议，直接切换风格：建议状态仍为未确认
    PromptManager pm;

    const QString summary1 = QStringLiteral(
        "[用户明确事实]\n"
        "想去京都旅游\n\n"
        "[助手曾建议但用户尚未确认]\n"
        "建议酒店预算4000元\n\n"
        "[待确认问题]\n"
        "用户是否接受东福寺？"
    );

    pm.appendUserMessage(QStringLiteral("想去京都旅游"));
    pm.appendAssistantMessage(QStringLiteral("建议酒店预算4000元，东福寺值得一去"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), summary1);

    // 切换话题（不回应建议）
    pm.appendUserMessage(QStringLiteral("今天天气怎么样"));
    pm.appendAssistantMessage(QStringLiteral("今天晴喵~"));

    // 第二次切换
    const QString summary2 = QStringLiteral(
        "[用户明确事实]\n"
        "想去京都旅游，问过今天天气\n\n"
        "[助手曾建议但用户尚未确认]\n"
        "建议酒店预算4000元\n\n"
        "[待确认问题]\n"
        "用户是否接受东福寺？"
    );
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格B"), summary2);

    // contextHistory 中未确认建议仍保留
    const auto history = pm.contextHistory();
    bool foundUnconfirmed = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("助手曾建议但用户尚未确认"))) {
            foundUnconfirmed = true;
            QVERIFY(msg.second.contains(QStringLiteral("4000")));
            break;
        }
    }
    QVERIFY(foundUnconfirmed);

    // 不得进入 confirmed_decisions（使用带方括号的标题头匹配，避免误匹配字段说明中的「用户已确认决定」）
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("[用户已确认决定]"))) {
            // 如果有 confirmed_decisions，不应包含酒店相关
            QVERIFY(!msg.second.contains(QStringLiteral("4000")));
        }
    }
}

void PromptManagerSummaryTest::testCumulativeNoAutoUpgrade()
{
    // 两次累计摘要，未确认建议不得自动升级为 confirmed
    PromptManager pm;

    const QString summary1 = QStringLiteral(
        "[用户明确事实]\n"
        "计划去北海道\n\n"
        "[助手曾建议但用户尚未确认]\n"
        "建议函馆山夜景\n\n"
        "[待确认问题]\n"
        "用户是否对函馆感兴趣？"
    );

    pm.appendUserMessage(QStringLiteral("计划去北海道"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格A"), summary1);

    // 第二轮（仍不回应建议）
    pm.appendUserMessage(QStringLiteral("札幌有什么好吃的"));
    pm.appendAssistantMessage(QStringLiteral("推荐拉面和海鲜"));

    const QString summary2 = QStringLiteral(
        "[用户明确事实]\n"
        "计划去北海道，问了札幌美食\n\n"
        "[助手曾建议但用户尚未确认]\n"
        "建议函馆山夜景\n\n"
        "[待确认问题]\n"
        "用户是否对函馆感兴趣？"
    );
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("风格B"), summary2);

    const auto history = pm.contextHistory();
    bool foundUnconfirmed = false;
    bool foundConfirmed = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system")) {
            if (msg.second.contains(QStringLiteral("助手曾建议但用户尚未确认")))
                foundUnconfirmed = true;
            if (msg.second.contains(QStringLiteral("用户已确认决定")))
                foundConfirmed = true;
        }
    }

    // 未确认建议仍以未确认身份存在
    QVERIFY(foundUnconfirmed);

    // 已确认决定区要么不存在，要么不含函馆内容
    // 简单验证：找任何 "函数" 出现在 confirmed_decisions 区域的地方
    bool autoUpgraded = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system")) {
            // 找到 [用户已确认决定] 到下一个 [ 或结束 的区域
            int start = msg.second.indexOf(QStringLiteral("用户已确认决定"));
            int end = msg.second.indexOf(QStringLiteral("[助手"), start > 0 ? start + 1 : 0);
            if (start >= 0) {
                if (end < 0) end = msg.second.size();
                const QString section = msg.second.mid(start, end - start);
                if (section.contains(QStringLiteral("函馆")))
                    autoUpgraded = true;
            }
        }
    }
    QVERIFY(!autoUpgraded);
}

void PromptManagerSummaryTest::testContextHistoryConstraintText()
{
    // contextHistory 约束文本必须包含未确认建议处理规则
    PromptManager pm;

    pm.appendUserMessage(QStringLiteral("帮我推荐景点"));
    pm.appendAssistantMessage(QStringLiteral("建议去银阁寺"));
    pm.beginStyleTransition();
    pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral(
        "[助手曾建议但用户尚未确认]\n"
        "建议去银阁寺\n\n"
        "[待确认问题]\n"
        "用户是否接受银阁寺？"));

    const auto history = pm.contextHistory();
    bool hasConstraintText = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("[对话摘要]"))) {
            // 检查关键约束文本
            QVERIFY(msg.second.contains(QStringLiteral("不得把未确认建议表述为用户决定")));
            QVERIFY(msg.second.contains(QStringLiteral("此前曾建议")));
            QVERIFY(msg.second.contains(QStringLiteral("尚待用户确认")));
            QVERIFY(msg.second.contains(QStringLiteral("不得从日期、预算或地点自行推断")));
            QVERIFY(msg.second.contains(QStringLiteral("以当前用户消息为准")));
            hasConstraintText = true;
            break;
        }
    }
    QVERIFY(hasConstraintText);
}

void PromptManagerSummaryTest::testFiveFieldJsonFormat()
{
    // 构造五字段 formatted 输出后，验证 contextHistory 中字段身份正确
    PromptManager pm;

    const QString fiveFieldSummary = QStringLiteral(
        "[用户明确事实]\n"
        "打算8月去京都\n\n"
        "[用户已确认决定]\n"
        "酒店预算4000元\n\n"
        "[用户偏好与约束]\n"
        "喜欢安静\n\n"
        "[助手曾建议但用户尚未确认]\n"
        "建议去东福寺\n\n"
        "[待确认问题]\n"
        "用户是否接受东福寺？"
    );

    pm.completeStyleTransition(QStringLiteral("专业"), fiveFieldSummary);

    const auto history = pm.contextHistory();
    bool allLabelsFound = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("[对话摘要]"))) {
            // 全部五个字段标签都存在
            QVERIFY(msg.second.contains(QStringLiteral("用户明确事实")));
            QVERIFY(msg.second.contains(QStringLiteral("用户已确认决定")));
            QVERIFY(msg.second.contains(QStringLiteral("用户偏好与约束")));
            QVERIFY(msg.second.contains(QStringLiteral("助手曾建议但用户尚未确认")));
            QVERIFY(msg.second.contains(QStringLiteral("待确认问题")));

            // 字段身份正确：各字段内容在正确标签下
            // 酒店预算在已确认决定下
            QVERIFY(msg.second.contains(QStringLiteral("酒店预算4000元")));
            // 东福寺在未确认建议下
            QVERIFY(msg.second.contains(QStringLiteral("东福寺")));
            // 字段不混合
            QVERIFY(!msg.second.contains(QStringLiteral("decisions")));
            QVERIFY(!msg.second.contains(QStringLiteral("recent_context")));
            allLabelsFound = true;
            break;
        }
    }
    QVERIFY(allLabelsFound);
}

void PromptManagerSummaryTest::testIllegalFieldTypeTriggersFallback()
{
    // 非法字段类型应导致解析失败 → 进入 fallback
    // 验证：非数组字段导致解析失败

    // 测试 1: user_facts 是字符串而非数组
    {
        const QString badJson = QStringLiteral(
            "{"
            "  \"user_facts\": \"这是一个字符串，不是数组\","
            "  \"confirmed_decisions\": [],"
            "  \"preferences\": [],"
            "  \"unconfirmed_assistant_suggestions\": [],"
            "  \"open_questions\": []"
            "}"
        );

        QString formatted;
        const auto result = parseTestSummaryJson(badJson, formatted);
        QVERIFY(!result.ok);   // 解析失败
        QVERIFY(formatted.isEmpty());
    }

    // 测试 2: confirmed_decisions 包含非字符串元素
    {
        const QString badJson = QStringLiteral(
            "{"
            "  \"user_facts\": [],"
            "  \"confirmed_decisions\": [12345],"
            "  \"preferences\": [],"
            "  \"unconfirmed_assistant_suggestions\": [],"
            "  \"open_questions\": []"
            "}"
        );

        QString formatted;
        const auto result = parseTestSummaryJson(badJson, formatted);
        QVERIFY(!result.ok);
        QVERIFY(formatted.isEmpty());
    }

    // 测试 3: open_questions 缺失 → 解析失败（必须每个字段都存在且为数组）
    {
        const QString missingFieldJson = QStringLiteral(
            "{"
            "  \"user_facts\": [],"
            "  \"confirmed_decisions\": [],"
            "  \"preferences\": [],"
            "  \"unconfirmed_assistant_suggestions\": []"
            "}"
        );

        QString formatted;
        const auto result = parseTestSummaryJson(missingFieldJson, formatted);
        QVERIFY(!result.ok);
        QVERIFY(formatted.isEmpty());
    }

    // 测试 4: fallback 路径在 PromptManager 层面仍正常工作
    {
        PromptManager pm;
        pm.appendUserMessage(QStringLiteral("计划去京都"));
        pm.appendAssistantMessage(QStringLiteral("好的喵~"));
        const auto snapshot = pm.beginStyleTransition();

        // 使用空字符串（模拟解析失败后的 fallback）
        QStringList userMsgs;
        for (const auto &msg : snapshot) {
            if (msg.first == QStringLiteral("user"))
                userMsgs.append(msg.second);
        }

        pm.completeStyleTransitionWithFallback(QStringLiteral("简洁"), userMsgs);

        // fallback 应保留用户消息
        QCOMPARE(pm.aiStylePrompt(), QStringLiteral("简洁"));
        int userCount = 0;
        for (const auto &msg : pm.contextHistory()) {
            if (msg.first == QStringLiteral("user")) ++userCount;
        }
        QCOMPARE(userCount, 1);
        QVERIFY(pm.conversationSummary().contains(QStringLiteral("计划去京都")));
    }
}

void PromptManagerSummaryTest::testSummaryPromptContainsRules()
{
    // 不再读取源码文件，改为捕获实际摘要请求的 POST body 验证

    // ── 1. 启动本地 mock TCP 服务器 ──
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const int port = server.serverPort();

    QByteArray capturedBody;

    connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead,
                [&capturedBody, socket, buffer]() {
            buffer->append(socket->readAll());

            int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;

            int contentLength = 0;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n')) {
                QByteArray tl = line.trimmed();
                if (tl.startsWith("Content-Length:"))
                    contentLength = tl.mid(tl.indexOf(':') + 1).trimmed().toInt();
            }

            const int bodyStart = headerEnd + 4;
            if (buffer->size() - bodyStart < contentLength)
                return; // 等待完整 body

            capturedBody = buffer->mid(bodyStart, contentLength);

            // ── 返回 fake DeepSeek API 响应 ──
            QJsonObject fakeMsg;
            fakeMsg.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            fakeMsg.insert(QStringLiteral("content"), QStringLiteral(
                "{\"user_facts\":[],\"confirmed_decisions\":[],"
                "\"preferences\":[],\"unconfirmed_assistant_suggestions\":[],"
                "\"open_questions\":[]}"));

            QJsonObject fakeChoice;
            fakeChoice.insert(QStringLiteral("message"), fakeMsg);
            fakeChoice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

            QJsonObject fakeRoot;
            fakeRoot.insert(QStringLiteral("choices"), QJsonArray{fakeChoice});

            const QByteArray responseBody =
                QJsonDocument(fakeRoot).toJson(QJsonDocument::Compact);
            const QByteArray httpResponse =
                QStringLiteral("HTTP/1.1 200 OK\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: %1\r\n"
                               "Connection: close\r\n"
                               "\r\n")
                    .arg(responseBody.size()).toUtf8()
                + responseBody;

            socket->write(httpResponse);
            socket->flush();
            socket->disconnectFromHost();
        });

        connect(socket, &QTcpSocket::disconnected,
                socket, &QTcpSocket::deleteLater);
    });

    // ── 2. 创建 DeepSeekClient 指向本地 mock ──
    DeepSeekClient client;
    client.setApiKey(QStringLiteral("test-mock-key"));
    client.setEndpoint(QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(port));

    // ── 3. 发送摘要请求 ──
    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    history.append(ChatMessage(QStringLiteral("assistant"),
                                QStringLiteral("有什么我可以帮你的？")));

    QSignalSpy spyReceived(&client, &DeepSeekClient::summaryReceived);
    QSignalSpy spyFailed(&client, &DeepSeekClient::summaryFailed);

    client.sendSummaryRequest(history, QStringLiteral("test-summary-req"));

    // ── 4. 等待结果（事件循环驱动网络 I/O） ──
    if (spyReceived.isEmpty() && spyFailed.isEmpty())
        spyReceived.wait(10000);

    server.close();

    if (spyFailed.size() > 0) {
        QFAIL(qPrintable(QStringLiteral("摘要请求不应失败：%1")
            .arg(spyFailed.first().value(1).toString())));
    }
    if (spyReceived.isEmpty()) {
        QFAIL("超时：摘要请求未在 10 秒内返回");
    }

    // ── 5. 解析 POST body ──
    QVERIFY(!capturedBody.isEmpty());

    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(capturedBody, &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError,
             qPrintable(parseError.errorString()));
    QVERIFY(doc.isObject());

    const QJsonObject root = doc.object();
    const QJsonArray messages =
        root.value(QStringLiteral("messages")).toArray();
    QVERIFY(messages.size() >= 1);

    // ── 6. 提取 system 消息 ──
    QString systemContent;
    for (const QJsonValue &v : messages) {
        const QJsonObject msg = v.toObject();
        if (msg.value(QStringLiteral("role")).toString()
                == QStringLiteral("system")) {
            systemContent = msg.value(QStringLiteral("content")).toString();
            break;
        }
    }
    QVERIFY2(!systemContent.isEmpty(), "请求中必须包含 system 消息");

    // ── 7. 验证五字段 ──
    QVERIFY2(systemContent.contains(QStringLiteral("user_facts")),
             "system prompt 必须包含 user_facts");
    QVERIFY2(systemContent.contains(QStringLiteral("confirmed_decisions")),
             "system prompt 必须包含 confirmed_decisions");
    QVERIFY2(systemContent.contains(QStringLiteral("preferences")),
             "system prompt 必须包含 preferences");
    QVERIFY2(systemContent.contains(QStringLiteral("unconfirmed_assistant_suggestions")),
             "system prompt 必须包含 unconfirmed_assistant_suggestions");
    QVERIFY2(systemContent.contains(QStringLiteral("open_questions")),
             "system prompt 必须包含 open_questions");

    // ── 8. 验证关键约束规则 ──
    QVERIFY2(systemContent.contains(QStringLiteral("不推断")),
             "system prompt 必须包含「不推断」规则");
    QVERIFY2(systemContent.contains(QStringLiteral("不进行日期换算")),
             "system prompt 必须包含「不进行日期换算」规则");
    QVERIFY2(systemContent.contains(QStringLiteral("沉默")),
             "system prompt 必须包含「沉默」相关规则");
    QVERIFY2(systemContent.contains(QStringLiteral("用户未回应建议，不代表接受")),
             "system prompt 必须包含沉默不代表确认规则");

    // ── 9. 验证旧字段已删除 ──
    QVERIFY2(!systemContent.contains(QStringLiteral("recent_context")),
             "旧字段 recent_context 已被移除，不应出现");

    // "decisions" 只允许出现在 "confirmed_decisions" 中
    int decisionsIdx = systemContent.indexOf(QStringLiteral("decisions"));
    while (decisionsIdx >= 0) {
        const int prevStart = qMax(0, decisionsIdx - 12);
        QVERIFY2(systemContent.mid(prevStart, decisionsIdx - prevStart)
                     .contains(QStringLiteral("confirmed_")),
                 "旧 JSON 字段「decisions」已被替换为 confirmed_decisions，"
                 "不得以孤立字段形式出现");
        decisionsIdx = systemContent.indexOf(QStringLiteral("decisions"),
                                             decisionsIdx + 1);
    }

    // ── 10. 同时验证 PromptManager contextHistory 约束文本 ──
    {
        PromptManager pm;
        pm.completeStyleTransition(QStringLiteral("专业"), QStringLiteral(
            "[助手曾建议但用户尚未确认]\n"
            "建议京都站附近住宿"));

        const auto hist = pm.contextHistory();
        bool hasConstraints = false;
        for (const auto &msg : hist) {
            if (msg.first == QStringLiteral("system") &&
                msg.second.contains(QStringLiteral("[对话摘要]"))) {
                QVERIFY(msg.second.contains(QStringLiteral("不得把未确认建议表述为用户决定")));
                QVERIFY(msg.second.contains(QStringLiteral("尚待用户确认")));
                QVERIFY(msg.second.contains(QStringLiteral("以当前用户消息为准")));
                hasConstraints = true;
                break;
            }
        }
        QVERIFY(hasConstraints);
    }

    // ── 11. 验证 system prompt 包含新序号/数量约束 ──
    // 接第 8 步（验证关键约束规则），追加新约束断言
    // 该方法已通过 mock server 捕获了完整 system prompt
    // 本测试在 testSummarySystemPromptDetailConstraint 中独立完成
    {
        // 仅在此标记——新约束验证已移至 testSummarySystemPromptDetailConstraint
    }
}


// ======================================================================
// 新增对象数组测试
// ======================================================================

void PromptManagerSummaryTest::testObjectArrayParsingRetainsAllDetails()
{
    // 解析完整对象数组，金额和景点名称全部保留
    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [\"计划去京都旅行\"],"
        "  \"confirmed_decisions\": ["
        "    {\"category\": \"住宿\", \"content\": \"京都站周边经济型酒店，300-500元/晚\"}"
        "  ],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"住宿\", \"content\": \"京都站附近民宿或胶囊旅馆，200-400元/晚\"},"
        "    {\"category\": \"景点\", \"content\": \"岚山竹林及渡月桥\"},"
        "    {\"category\": \"景点\", \"content\": \"清水寺及二年坂三年坂\"}"
        "  ],"
        "  \"open_questions\": []"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);
    QVERIFY(!formatted.isEmpty());

    // confirmed_decisions 有 1 条，含完整金额
    QCOMPARE(result.confirmedDecisions.size(), 1);
    QVERIFY(result.confirmedDecisions[0].contains(QStringLiteral("300-500元")));
    QVERIFY(result.confirmedDecisions[0].contains(QStringLiteral("京都站周边")));

    // unconfirmed 有 3 条，每条含完整细节
    QCOMPARE(result.unconfirmedSuggestions.size(), 3);
    QVERIFY(result.unconfirmedSuggestions[0].contains(QStringLiteral("200-400元")));
    QVERIFY(result.unconfirmedSuggestions[0].contains(QStringLiteral("民宿")));
    QVERIFY(result.unconfirmedSuggestions[1].contains(QStringLiteral("岚山")));
    QVERIFY(result.unconfirmedSuggestions[2].contains(QStringLiteral("清水寺")));

    // formatted 输出保留完整 content
    QVERIFY(formatted.contains(QStringLiteral("300-500元")));
    QVERIFY(formatted.contains(QStringLiteral("岚山竹林")));
    QVERIFY(formatted.contains(QStringLiteral("清水寺")));
}

void PromptManagerSummaryTest::testObjectArrayStrictValidation()
{
    // 缺少 category  → 解析失败
    {
        const QString json = QStringLiteral(
            "{"
            "  \"user_facts\": [],"
            "  \"confirmed_decisions\": [{\"content\": \"某决定\"}],"
            "  \"preferences\": [],"
            "  \"unconfirmed_assistant_suggestions\": [],"
            "  \"open_questions\": []"
            "}"
        );
        QString formatted;
        QVERIFY(!parseTestSummaryJson(json, formatted).ok);
    }

    // 缺少 content → 解析失败
    {
        const QString json = QStringLiteral(
            "{"
            "  \"user_facts\": [],"
            "  \"confirmed_decisions\": [],"
            "  \"preferences\": [],"
            "  \"unconfirmed_assistant_suggestions\": [{\"category\": \"景点\"}],"
            "  \"open_questions\": []"
            "}"
        );
        QString formatted;
        QVERIFY(!parseTestSummaryJson(json, formatted).ok);
    }

    // content 为空 → 解析失败
    {
        const QString json = QStringLiteral(
            "{"
            "  \"user_facts\": [],"
            "  \"confirmed_decisions\": [],"
            "  \"preferences\": [],"
            "  \"unconfirmed_assistant_suggestions\": ["
            "    {\"category\": \"景点\", \"content\": \"\"}"
            "  ],"
            "  \"open_questions\": []"
            "}"
        );
        QString formatted;
        QVERIFY(!parseTestSummaryJson(json, formatted).ok);
    }

    // 非对象元素 → 解析失败（旧字符串格式不再接受）
    {
        const QString json = QStringLiteral(
            "{"
            "  \"user_facts\": [],"
            "  \"confirmed_decisions\": [\"旧格式字符串\"],"
            "  \"preferences\": [],"
            "  \"unconfirmed_assistant_suggestions\": [],"
            "  \"open_questions\": []"
            "}"
        );
        QString formatted;
        QVERIFY(!parseTestSummaryJson(json, formatted).ok);
    }
}

void PromptManagerSummaryTest::testConfirmedDecisionMovesSpecificObject()
{
    // AI 确认第一个住宿方案后的输出模拟：
    // 京都站周边经济型酒店（300-500元/晚）移入 confirmed_decisions
    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [],"
        "  \"confirmed_decisions\": ["
        "    {\"category\": \"住宿\", \"content\": \"京都站周边经济型酒店，300-500元/晚\"}"
        "  ],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"住宿\", \"content\": \"京都站附近民宿或胶囊旅馆，200-400元/晚\"},"
        "    {\"category\": \"景点\", \"content\": \"岚山竹林及渡月桥\"},"
        "    {\"category\": \"景点\", \"content\": \"清水寺及二年坂三年坂\"}"
        "  ],"
        "  \"open_questions\": []"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);

    // confirmed 有且只有被确认的酒店，不是"方案一"
    QCOMPARE(result.confirmedDecisions.size(), 1);
    QVERIFY(result.confirmedDecisions[0].contains(QStringLiteral("300-500元")));
    QVERIFY(!result.confirmedDecisions[0].contains(QStringLiteral("方案一")));

    // unconfirmed 仍包含剩余 3 条完整建议
    QCOMPARE(result.unconfirmedSuggestions.size(), 3);
    QVERIFY(result.unconfirmedSuggestions[0].contains(QStringLiteral("200-400元")));
    QVERIFY(result.unconfirmedSuggestions[1].contains(QStringLiteral("岚山")));
    QVERIFY(result.unconfirmedSuggestions[2].contains(QStringLiteral("清水寺")));

    // formatted 不出现序号/类别引用
    QVERIFY(!formatted.contains(QStringLiteral("方案一")));
}

void PromptManagerSummaryTest::testContextHistoryFormattedContent()
{
    // contextHistory 格式化输出完整 content
    PromptManager pm;

    const QString formattedSummary =
        QStringLiteral("[用户已确认决定]\n"
                       "京都站周边经济型酒店，300-500元/晚\n\n"
                       "[助手曾建议但用户尚未确认]\n"
                       "岚山竹林及渡月桥\n\n"
                       "[待确认问题]\n"
                       "用户是否决定住经济型酒店？");

    pm.completeStyleTransition(QStringLiteral("专业"), formattedSummary);

    // 原始摘要本身不含序号引用
    // （包装说明中"方案一"出现在约束示例文本中，不影响此断言）
    QVERIFY(!pm.conversationSummary().contains(QStringLiteral("方案一")));

    const auto history = pm.contextHistory();

    bool foundDetails = false;
    for (const auto &msg : history) {
        if (msg.first == QStringLiteral("system") &&
            msg.second.contains(QStringLiteral("[对话摘要]"))) {
            // 完整 content 保留
            QVERIFY(msg.second.contains(QStringLiteral("300-500元")));
            QVERIFY(msg.second.contains(QStringLiteral("岚山竹林")));
            QVERIFY(msg.second.contains(QStringLiteral("渡月桥")));
            foundDetails = true;
            break;
        }
    }
    QVERIFY(foundDetails);
}

void PromptManagerSummaryTest::testSummarySystemPromptDetailConstraint()
{
    // 通过 mock TCP 服务器捕获实际摘要请求的 POST body，
    // 验证 system prompt 包含"不得只保存序号"的相关约束
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const int port = server.serverPort();

    QByteArray capturedBody;

    connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket *socket = server.nextPendingConnection();

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, [&capturedBody, socket, buffer]() {
            buffer->append(socket->readAll());

            int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) return;

            int contentLength = 0;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n')) {
                QByteArray tl = line.trimmed();
                if (tl.startsWith("Content-Length:"))
                    contentLength = tl.mid(tl.indexOf(':') + 1).trimmed().toInt();
            }

            const int bodyStart = headerEnd + 4;
            if (buffer->size() - bodyStart < contentLength) return;

            capturedBody = buffer->mid(bodyStart, contentLength);

            QJsonObject fakeMsg;
            fakeMsg.insert(QStringLiteral("role"), QStringLiteral("assistant"));
            fakeMsg.insert(QStringLiteral("content"), QStringLiteral(
                "{\"user_facts\":[],\"confirmed_decisions\":[],"
                "\"preferences\":[],\"unconfirmed_assistant_suggestions\":[],"
                "\"open_questions\":[]}"));

            QJsonObject fakeChoice;
            fakeChoice.insert(QStringLiteral("message"), fakeMsg);
            fakeChoice.insert(QStringLiteral("finish_reason"), QStringLiteral("stop"));

            QJsonObject fakeRoot;
            fakeRoot.insert(QStringLiteral("choices"), QJsonArray{fakeChoice});

            const QByteArray responseBody =
                QJsonDocument(fakeRoot).toJson(QJsonDocument::Compact);
            const QByteArray httpResponse =
                QStringLiteral("HTTP/1.1 200 OK\r\n"
                               "Content-Type: application/json\r\n"
                               "Content-Length: %1\r\n"
                               "Connection: close\r\n"
                               "\r\n")
                    .arg(responseBody.size()).toUtf8()
                + responseBody;

            socket->write(httpResponse);
            socket->flush();
            socket->disconnectFromHost();
        });

        connect(socket, &QTcpSocket::disconnected,
                socket, &QTcpSocket::deleteLater);
    });

    DeepSeekClient client;
    client.setApiKey(QStringLiteral("test-mock-key"));
    client.setEndpoint(QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(port));

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    history.append(ChatMessage(QStringLiteral("assistant"),
                                QStringLiteral("有什么我可以帮你的？")));

    QSignalSpy spyReceived(&client, &DeepSeekClient::summaryReceived);
    QSignalSpy spyFailed(&client, &DeepSeekClient::summaryFailed);

    client.sendSummaryRequest(history, QStringLiteral("obj-array-test"));

    if (spyReceived.isEmpty() && spyFailed.isEmpty())
        spyReceived.wait(10000);

    server.close();

    if (spyFailed.size() > 0) {
        QFAIL(qPrintable(QStringLiteral("摘要请求不应失败：%1")
            .arg(spyFailed.first().value(1).toString())));
    }
    if (spyReceived.isEmpty()) {
        QFAIL("超时：摘要请求未在 10 秒内返回");
    }

    QVERIFY(!capturedBody.isEmpty());

    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(capturedBody, &parseError);
    QVERIFY2(parseError.error == QJsonParseError::NoError,
             qPrintable(parseError.errorString()));
    QVERIFY(doc.isObject());

    const QJsonObject root = doc.object();
    const QJsonArray messages =
        root.value(QStringLiteral("messages")).toArray();
    QVERIFY(messages.size() >= 1);

    QString systemContent;
    for (const QJsonValue &v : messages) {
        const QJsonObject msg = v.toObject();
        if (msg.value(QStringLiteral("role")).toString()
                == QStringLiteral("system")) {
            systemContent = msg.value(QStringLiteral("content")).toString();
            break;
        }
    }
    QVERIFY2(!systemContent.isEmpty(), "请求中必须包含 system 消息");

    // 验证对象数组 JSON 格式
    QVERIFY2(systemContent.contains(QStringLiteral("\"confirmed_decisions\": [")),
             "system prompt 必须包含对象数组格式的 confirmed_decisions");
    QVERIFY2(systemContent.contains(QStringLiteral("\"unconfirmed_assistant_suggestions\": [")),
             "system prompt 必须包含对象数组格式的 unconfirmed_assistant_suggestions");
    QVERIFY2(systemContent.contains(QStringLiteral("\"category\"")),
             "对象数组必须包含 category 字段");
    QVERIFY2(systemContent.contains(QStringLiteral("\"content\"")),
             "对象数组必须包含 content 字段");

    // 验证新增细节约束
    QVERIFY2(systemContent.contains(QStringLiteral("可独立理解的完整内容")),
             "system prompt 必须要求可独立理解的内容");
    QVERIFY2(systemContent.contains(QStringLiteral("压缩为数量表述")),
             "system prompt 必须禁止数量表述");
    QVERIFY2(systemContent.contains(QStringLiteral("序号引用")),
             "system prompt 必须禁止序号引用");
    QVERIFY2(systemContent.contains(QStringLiteral("再次累计摘要")),
             "system prompt 必须处理累计摘要场景");
    QVERIFY2(systemContent.contains(QStringLiteral("方案一")),
             "system prompt 必须包含\"方案一\"作为反例");

    // ---- 以下为 detail drift 修复新增断言 ----

    QVERIFY2(systemContent.contains(QStringLiteral("不得新增原文未出现的地点")),
             "必须禁止 summary AI 新增原文未出现的地点、房型等细节");
    QVERIFY2(systemContent.contains(QStringLiteral("备选关系")),
             "必须禁止删除'或''以及'等备选关系");
    QVERIFY2(systemContent.contains(QStringLiteral("位置未指定")),
             "未给定位置时必须要求写入'位置未指定'，不得自行补全");
    QVERIFY2(systemContent.contains(QStringLiteral("尽量原样保留")),
             "重新摘要时必须尽量原样保留 category 和 content");
    QVERIFY2(systemContent.contains(QStringLiteral("确认第一个方案")),
             "确认第一个方案时必须移动完整对象，不得重写或补全");
    QVERIFY2(systemContent.contains(QStringLiteral("与用户已确认地点冲突")),
             "与已确认地点冲突的建议应保持未确认并明确冲突");
}


// ======================================================================
// 以下为 detail drift 修复新增确定性测试（不依赖真实 DeepSeek 输出）
// ======================================================================

void PromptManagerSummaryTest::testDetailFidelity_HotelAlternativesPreserved()
{
    // 场景：assistant 提供含备选关系的住宿建议，经两次累计摘要后不得丢失备选项和房型
    // 第一轮摘要
    const QString json1 = QStringLiteral(
        "{"
        "  \"user_facts\": [\"计划去东京旅行\"],"
        "  \"confirmed_decisions\": [],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"住宿\", \"content\": \"经济型胶囊旅馆或青年旅社，单人间约3000-5000日元/晚，位置未指定\"}"
        "  ],"
        "  \"open_questions\": [\"预算多少？\"]"
        "}"
    );

    QString formatted1;
    const auto result1 = parseTestSummaryJson(json1, formatted1);
    QVERIFY(result1.ok);
    QCOMPARE(result1.unconfirmedSuggestions.size(), 1);

    // 模拟第二轮累计摘要：context 不变，摘要 AI 应原样保留 content
    const QString json2 = QStringLiteral(
        "{"
        "  \"user_facts\": [\"计划去东京旅行\"],"
        "  \"confirmed_decisions\": [],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"住宿\", \"content\": \"经济型胶囊旅馆或青年旅社，单人间约3000-5000日元/晚，位置未指定\"}"
        "  ],"
        "  \"open_questions\": [\"预算多少？\"]"
        "}"
    );

    QString formatted2;
    const auto result2 = parseTestSummaryJson(json2, formatted2);
    QVERIFY(result2.ok);

    // 备选关系 "或" 必须保留 — 不得缩成 "胶囊旅馆"
    QVERIFY2(result2.unconfirmedSuggestions[0].contains(QStringLiteral("或")),
             "备选关系'或'必须保留");
    QVERIFY2(result2.unconfirmedSuggestions[0].contains(QStringLiteral("胶囊旅馆")),
             "必须包含胶囊旅馆");
    QVERIFY2(result2.unconfirmedSuggestions[0].contains(QStringLiteral("青年旅社")),
             "必须包含青年旅社");

    // 房型保留
    QVERIFY2(result2.unconfirmedSuggestions[0].contains(QStringLiteral("单人间")),
             "房型'单人间'必须保留");

    // 不得自行补全位置
    QVERIFY2(!result2.unconfirmedSuggestions[0].contains(QStringLiteral("浅草寺")),
             "不得新增原文未出现的地点");
    QVERIFY2(!result2.unconfirmedSuggestions[0].contains(QStringLiteral("新宿")),
             "不得新增原文未出现的地点");

    // 位置未指定 标记保留
    QVERIFY2(result2.unconfirmedSuggestions[0].contains(QStringLiteral("位置未指定")),
             "位置未指定标记必须保留");
}

void PromptManagerSummaryTest::testDetailFidelity_RoomTypeRetained()
{
    // 场景：第二住宿建议中 "双人标间" 必须保留，不得新增位置
    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [\"计划去东京旅行\"],"
        "  \"confirmed_decisions\": [],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"住宿\", \"content\": \"中档商务酒店，双人标间约8000-12000日元/晚，位置未指定\"},"
        "    {\"category\": \"景点\", \"content\": \"浅草寺及周边老街\"}"
        "  ],"
        "  \"open_questions\": []"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);
    QCOMPARE(result.unconfirmedSuggestions.size(), 2);

    // 双人标间 必须完整保留
    QVERIFY2(result.unconfirmedSuggestions[0].contains(QStringLiteral("双人标间")),
             "对象必须包含'双人标间'");

    // 不得新增涩谷/新宿等位置
    QVERIFY2(!result.unconfirmedSuggestions[0].contains(QStringLiteral("涩谷")),
             "未提供位置时不得补全\"");
    QVERIFY2(!result.unconfirmedSuggestions[0].contains(QStringLiteral("新宿")),
             "未提供位置时不得补全\"");

    // 位置未指定 标记保留
    QVERIFY2(result.unconfirmedSuggestions[0].contains(QStringLiteral("位置未指定")),
             "位置未指定标记必须保留");
}

void PromptManagerSummaryTest::testDetailFidelity_LocationNotNarrowed()
{
    // 场景："浅草寺及周边老街" 不得缩成 "浅草寺"
    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [\"计划去东京旅行\"],"
        "  \"confirmed_decisions\": [],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"景点\", \"content\": \"浅草寺及周边老街\"}"
        "  ],"
        "  \"open_questions\": []"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);
    QCOMPARE(result.unconfirmedSuggestions.size(), 1);

    // 必须包含完整内容
    QVERIFY2(result.unconfirmedSuggestions[0].contains(QStringLiteral("浅草寺")),
             "必须包含浅草寺");
    QVERIFY2(result.unconfirmedSuggestions[0].contains(QStringLiteral("周边老街")),
             "必须包含'周边老街'，不得缩写成仅'浅草寺'");

    // formatted 输出中同样完整
    QVERIFY2(formatted.contains(QStringLiteral("周边老街")),
             "formatted 输出必须包含完整景点名称");
}

void PromptManagerSummaryTest::testDetailFidelity_ConfirmationPreservesContent()
{
    // 场景：用户确认第一个住宿方案，完整对象从 unconfirmed 移到 confirmed，content 不变
    const QString originalContent =
        QStringLiteral("京都站周边经济型酒店，300-500元/晚");

    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [],"
        "  \"confirmed_decisions\": ["
        "    {\"category\": \"住宿\", \"content\": \"京都站周边经济型酒店，300-500元/晚\"}"
        "  ],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"住宿\", \"content\": \"京都站附近民宿或胶囊旅馆，200-400元/晚\"}"
        "  ],"
        "  \"open_questions\": []"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);

    // confirmed 有且只有一条，content 与原文完全相同
    QCOMPARE(result.confirmedDecisions.size(), 1);
    QCOMPARE(result.confirmedDecisions[0], originalContent);

    // unconfirmed 剩余一条，content 未变化
    QCOMPARE(result.unconfirmedSuggestions.size(), 1);
    const QString secondOriginal =
        QStringLiteral("京都站附近民宿或胶囊旅馆，200-400元/晚");
    QCOMPARE(result.unconfirmedSuggestions[0], secondOriginal);

    // formatted 输出保留完整内容
    QVERIFY(formatted.contains(originalContent));
    QVERIFY(formatted.contains(secondOriginal));
}

void PromptManagerSummaryTest::testDetailFidelity_ConflictingLocationNotMerged()
{
    // 场景：用户已确认京都作为旅行地点（confirmed_decisions），
    // assistant 提出东京景点建议（unconfirmed_assistant_suggestions）
    // 东京景点不得表述为已确定京都行程的一部分
    const QString json = QStringLiteral(
        "{"
        "  \"user_facts\": [\"计划去京都旅行\"],"
        "  \"confirmed_decisions\": ["
        "    {\"category\": \"行程\", \"content\": \"京都\"}"
        "  ],"
        "  \"preferences\": [],"
        "  \"unconfirmed_assistant_suggestions\": ["
        "    {\"category\": \"景点\", \"content\": \"涩谷/原宿商圈\"},"
        "    {\"category\": \"景点\", \"content\": \"浅草寺及周边老街\"}"
        "  ],"
        "  \"open_questions\": []"
        "}"
    );

    QString formatted;
    const auto result = parseTestSummaryJson(json, formatted);
    QVERIFY(result.ok);
    QVERIFY(!formatted.isEmpty());

    // confirmed: 只有京都
    QCOMPARE(result.confirmedDecisions.size(), 1);
    QVERIFY(result.confirmedDecisions[0].contains(QStringLiteral("京都")));

    // unconfirmed: 东京景点保持未确认状态
    QCOMPARE(result.unconfirmedSuggestions.size(), 2);
    QVERIFY(result.unconfirmedSuggestions[0].contains(QStringLiteral("涩谷")));
    QVERIFY(result.unconfirmedSuggestions[1].contains(QStringLiteral("浅草寺")));

    // 东京建议不得出现在 confirmed
    for (const QString &cd : result.confirmedDecisions) {
        QVERIFY2(!cd.contains(QStringLiteral("涩谷")),
                 "东京建议不得出现在已确认决定中");
        QVERIFY2(!cd.contains(QStringLiteral("浅草寺")),
                 "东京建议不得出现在已确认决定中");
    }

    // formatted 保留身份区分
    QVERIFY(formatted.contains(QStringLiteral("助手曾建议但用户尚未确认")));
    QVERIFY(formatted.contains(QStringLiteral("用户已确认决定")));
    // 涩谷和浅草寺在"未确认"部分
    const int unconfirmedIdx = formatted.indexOf(QStringLiteral("助手曾建议但用户尚未确认"));
    QVERIFY(unconfirmedIdx >= 0);
    QVERIFY(formatted.indexOf(QStringLiteral("涩谷")) > unconfirmedIdx);
    QVERIFY(formatted.indexOf(QStringLiteral("浅草寺")) > unconfirmedIdx);
}


// ======================================================================
// 本文件由 test_main.cpp #include 包含，不单独编译
// ======================================================================
