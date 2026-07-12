#pragma once

#include <QObject>
#include <QStringList>

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

class PromptManagerSummaryTest : public QObject {
    Q_OBJECT

public:
    explicit PromptManagerSummaryTest() = default;

private slots:
    // 1. 有 user + assistant 历史时切换风格：摘要包含话题/结论，不含旧风格
    void testSummaryPreservesTopicAndFacts();
    void testSummaryReplacesOldStyle();

    // 2. assistant 有效结论可延续
    void testAssistantConclusionPreserved();

    // 4. 连续快速切换三次 → 只有最后一次风格和摘要生效
    void testRapidConsecutiveSwitches();

    // 6. 摘要失败：user-only fallback
    void testFallbackUserMessages();

    // 7. 相同风格不摘要不清空
    void testSameStyleNoTransition();

    // 8. 角色切换清除 summary 和 history
    void testRoleSwitchClearsSummary();

    // 9. contextHistory 多次调用只注入一条 summary，不膨胀
    void testSummaryNotDuplicatedOnRepeatedContextHistory();
    void testSummaryInjectionPosition();

    // 10. beginStyleTransition 快照不可变
    void testSnapshotImmutability();

    // 11. begin + complete 不双重递增 generation
    void testGenerationNotDoubleIncremented();

    // 12. completeStyleTransitionWithFallback 恢复 user 消息
    void testFallbackRestoresUserMessages();

    // ──── 以下为本次新增 ────

    // 13. 快速切换 A→B→C 只创建一份快照，generation 只递增一次
    void testRapidSwitchOnlyOneSnapshot();

    // 14. 快速切换后 contextHistory 只包含最终风格 C 和摘要，不含 A/B
    void testRapidSwitchContextHistoryOnlyFinal();

    // 15. completeStyleTransition 不覆盖已有摘要
    void testCompleteStyleTransitionPreservesSummary();

    // 16. 无论 success/failure/timeout，PromptManager 层面保证 style 不泄漏
    void testStyleIsolationAfterAnyTransition();

    // ──── 以下为本次修复新增 ────

    // 17. 无历史但有摘要时，completeStyleTransition 保留摘要
    void testExistingSummaryPreservedWhenHistoryEmpty();

    // 18. 摘要完成后快速 A→B→C，最终风格为 C，原摘要仍存在
    void testRapidSwitchAfterSummaryKeepsContext();

    // 19. 旧摘要含旅行事实，新 history 含住宿偏好，beginStyleTransition 后旧摘要仍可获取
    void testCumulativeSummaryPreservedThroughBeginTransition();

    // 20. 无历史无摘要时直接切换不产生副作用
    void testEmptyHistoryAndEmptySummaryDirectSwitch();

    // 21. 角色切换（setPersonality）仍清除摘要
    void testRoleSwitchStillClearsSummary();

    // ──── 以下为本次可观测性 + 回归测试新增 ────

    // 22. fallback 后旧摘要不得丢失
    void testFallbackPreservesExistingSummary();

    // 23. 空 fallback 不得覆盖已有非空摘要
    void testEmptyFallbackNoOverwrite();

    // 24. 模拟 actuallyStartSummary 空快照路径：不得用空字符串覆盖已有摘要
    void testEmptySnapshotDoesNotOverwriteSummary();

    // 25. 五项事实经两次 style 切换仍完整保留
    void testFiveFactCumulativeThroughTwoSwitches();

    // 26. 未调用 beginStyleTransition 时 generation 不变
    void testNoBeginTransitionNoGenerationChange();

    // 27. 有效风格未变化时 generation 不变
    void testSameStyleNoGenerationChange();

    // ---- summary source contamination fix ----
    void testUnconfirmedSuggestionStaysUnconfirmed();
    void testPartialConfirmation();
    void testSilentStyleSwitchPreservesUnconfirmed();
    void testCumulativeNoAutoUpgrade();
    void testContextHistoryConstraintText();
    void testFiveFieldJsonFormat();
    void testIllegalFieldTypeTriggersFallback();
    void testSummaryPromptContainsRules();

    // ---- object array tests ----
    void testObjectArrayParsingRetainsAllDetails();
    void testObjectArrayStrictValidation();
    void testConfirmedDecisionMovesSpecificObject();
    void testContextHistoryFormattedContent();
    void testSummarySystemPromptDetailConstraint();

    // ---- detail drift fix tests ----
    void testDetailFidelity_HotelAlternativesPreserved();
    void testDetailFidelity_RoomTypeRetained();
    void testDetailFidelity_LocationNotNarrowed();
    void testDetailFidelity_ConfirmationPreservesContent();
    void testDetailFidelity_ConflictingLocationNotMerged();

    // ---- strict repetition 加固 ----
    void testStrictRepetitionRulesInContextHistory();
};
