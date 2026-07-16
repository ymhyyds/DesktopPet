#pragma once

/**
 * @brief DeepSeekClient 网络超时与错误恢复单元测试
 *
 * 验证：
 *   - 30 秒超时 QTimer 正常触发并发射错误信号
 *   - 主动取消不发射错误信号
 *   - 请求结束后只终结一次（无重复信号）
 *   - 超时/错误后清空不完整回复
 *   - 摘要请求超时兜底+迟到结果丢弃
 *   - 失败后仍可再次发送请求
 */

#include <QObject>
#include <QTest>
#include <QDebug>

class DeepSeekClientNetworkTest : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekClientNetworkTest() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    // ── 超时 ──
    void testMessageTimeout();
    void testSummaryTimeout();

    // ── 主动取消 ──
    void testCancelMessage();
    void testCancelSummary();

    // ── 单次终结 ──
    void testMessageSingleFinalization();
    void testSummarySingleFinalization();

    // ── 迟到结果丢弃 ──
    void testLateSummaryDiscarded();

    // ── 失败恢复 ──
    void testMessageTimeoutThenResend();
    void testNetworkErrorThenResend();

private:
    /// 从 DeepSeekClient 的信号接收完整回复（阻塞等待）。
    QString waitForReply(int timeoutMs = 5000);

    /// 等待 errorOccurred 信号。
    QString waitForError(int timeoutMs = 5000);

    /// 等待 summaryReceived 信号。
    QPair<QString, QString> waitForSummaryReceived(int timeoutMs = 5000);

    /// 等待 summaryFailed 信号。
    QPair<QString, QString> waitForSummaryFailed(int timeoutMs = 5000);
};
