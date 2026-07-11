#pragma once

#include <QObject>

#include "MockServer.h"

class WeatherManager;

// ── Forward declarations so test_main.cpp sees Q_OBJECT macros ──────────
// AUTOMOC scans header files for Q_OBJECT; it does NOT scan .cpp files
// that are #included by other .cpp files, so the class declarations must
// be in a .h for the meta-object compiler to pick them up.

class WeatherManagerTest : public QObject {
    Q_OBJECT

public:
    explicit WeatherManagerTest() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // 正常流程
    void testNormalJson();
    void testNonJson();
    void testHttpError();
    void testTimeout();
    void testMissingCurrentCondition();
    void testMissingWeatherDesc();
    void testEmptyBody();

    // 缓存
    void testCacheHit();
    void testCacheExpiry();
    void testFailureWithStaleCache();

    // 缓存恢复（restoreFromCache）
    void testRestoreFromCacheFresh();
    void testRestoreFromCacheStale();
    void testRestoreFromCacheInvalidTimestamp();
    void testRestoreFromCacheFutureTimestamp();
    void testRestoreFromCacheStrippedStaleMarker();
    void testRestoreFromCacheRefreshClearsStale();

    // 自动刷新
    void testAutoRefreshStartsImmediately();
    void testAutoRefreshTriggersSecondRequest();
    void testAutoRefreshNoConcurrent();
    void testAutoRefreshStop();
    void testAutoRefreshFailureKeepsStaleCache();
    void testAutoRefreshSuccessClearsExpired();
    void testDestructorNoTimerCallback();

private:
    // 发起一次异步请求并等待信号返回（超时 8 秒）
    enum class WaitResult { Updated, Failed, Timeout };
    WaitResult waitForWeatherResult(int timeoutMs = 8000);
    WaitResult waitForWeatherResultFor(WeatherManager *mgr, int timeoutMs = 8000);

    MockServer m_server;
    WeatherManager *m_mgr = nullptr;
};

class PromptManagerWeatherTest : public QObject {
    Q_OBJECT

public:
    explicit PromptManagerWeatherTest() = default;

private slots:
    // ── 环境上下文 ──
    void testBuildEnvironmentContextWithWeather();
    void testBuildEnvironmentContextWithoutWeather();
    void testBuildEnvironmentContextDisabled();
    void testEnvironmentContextInContextHistory();
    void testProactivePromptHasEnvironment();
    void testNoLongTermPollution();
    void testWeatherResetOnEmptyContext();

    // ── AI 说话风格 ──
    void testDefaultStyleInContextHistory();
    void testDefaultToCustomStyle();
    void testCustomAToCustomB();
    void testCustomToDefaultStyle();
    void testSameStyleNoop();
    void testStyleNormalization();
    void testContextHistoryNoStyleLeak();
};
