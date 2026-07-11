// ── WeatherManager 单元测试 ──────────────────────────────────────────────
// 使用 MockWeatherServer.py 模拟 wttr.in j1 JSON 接口。
// 所有测试不访问真实网络。

#include <QTest>
#include <QDebug>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QProcess>

#include "core/WeatherManager.h"
#include "ai/PromptManager.h"

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
// 本文件由 test_main.cpp #include 包含，不单独编译
// ======================================================================
