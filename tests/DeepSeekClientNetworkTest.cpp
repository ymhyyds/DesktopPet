#include "DeepSeekClientNetworkTest.h"

#include <QSignalSpy>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QTimer>

#include "ai/DeepSeekClient.h"
#include "MockServer.h"

// ── Per-test-class fixtures ────────────────────────────────────────────────
static MockServer s_dsMock;
static DeepSeekClient *s_client = nullptr;

// ── Helper: wait for signal (shared between test methods) ───────────────────
static int waitForSpy(QSignalSpy &spy, int timeoutMs)
{
    if (spy.count() > 0) return spy.count();
    return spy.wait(timeoutMs) ? spy.count() : -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// Test lifecycle
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::initTestCase()
{
    // Start mock server
    QString scriptPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/MockDeepSeekServer.py");
    if (!QFile::exists(scriptPath)) {
        scriptPath = QStringLiteral("MockDeepSeekServer.py");
    }
    QVERIFY2(s_dsMock.start(scriptPath),
             qPrintable(QStringLiteral("Failed to start mock DeepSeek server: %1")
                            .arg(s_dsMock.lastErrorString())));
}

void DeepSeekClientNetworkTest::cleanupTestCase()
{
    s_dsMock.stop();
}

void DeepSeekClientNetworkTest::init()
{
    QVERIFY2(s_dsMock.clearError(),
             qPrintable(QStringLiteral("clearError failed: %1").arg(s_dsMock.lastErrorString())));
    QVERIFY2(s_dsMock.setDelay(0),
             qPrintable(QStringLiteral("setDelay failed: %1").arg(s_dsMock.lastErrorString())));
    QVERIFY2(s_dsMock.setResponseType("both", QStringLiteral("streaming_ok")),
             qPrintable(QStringLiteral("setResponseType failed: %1").arg(s_dsMock.lastErrorString())));
    QVERIFY2(s_dsMock.waitUntilReady(),
             qPrintable(QStringLiteral("waitUntilReady failed: %1").arg(s_dsMock.lastErrorString())));

    s_client = new DeepSeekClient();
    s_client->setApiKey(QStringLiteral("sk-test-mock-key"));
    s_client->setEndpoint(s_dsMock.baseUrl() + QStringLiteral("/chat/completions"));
    s_client->setRequestTimeoutMs(300);  // 快速超时以便测试
}

void DeepSeekClientNetworkTest::cleanup()
{
    if (s_client) {
        s_client->cancelMessage();
        s_client->cancelSummary();
        delete s_client;
        s_client = nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Helpers
// ═══════════════════════════════════════════════════════════════════════════

QString DeepSeekClientNetworkTest::waitForReply(int timeoutMs)
{
    QSignalSpy spy(s_client, &DeepSeekClient::replyReceived);
    if (waitForSpy(spy, timeoutMs) > 0) {
        return spy.at(0).at(0).toString();
    }
    return {};
}

QString DeepSeekClientNetworkTest::waitForError(int timeoutMs)
{
    QSignalSpy spy(s_client, &DeepSeekClient::errorOccurred);
    if (waitForSpy(spy, timeoutMs) > 0) {
        return spy.at(0).at(0).toString();
    }
    return {};
}

QPair<QString, QString> DeepSeekClientNetworkTest::waitForSummaryReceived(int timeoutMs)
{
    QSignalSpy spy(s_client, &DeepSeekClient::summaryReceived);
    if (waitForSpy(spy, timeoutMs) > 0) {
        return {spy.at(0).at(0).toString(), spy.at(0).at(1).toString()};
    }
    return {};
}

QPair<QString, QString> DeepSeekClientNetworkTest::waitForSummaryFailed(int timeoutMs)
{
    QSignalSpy spy(s_client, &DeepSeekClient::summaryFailed);
    if (waitForSpy(spy, timeoutMs) > 0) {
        return {spy.at(0).at(0).toString(), spy.at(0).at(1).toString()};
    }
    return {};
}

// ═══════════════════════════════════════════════════════════════════════════
// 1. 超时
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testMessageTimeout()
{
    // 模拟服务器永远不回复
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));

    s_client->setRequestTimeoutMs(200);  // 200ms 超时

    QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendMessage(QStringLiteral("你好"), history);

    int gotError = waitForSpy(errorSpy, 2000);
    QCOMPARE_GT(gotError, 0);

    QString errorMsg = errorSpy.at(0).at(0).toString();
    QVERIFY2(errorMsg.contains(QStringLiteral("超时")),
             qPrintable(QStringLiteral("超时错误消息应包含'超时'，实际: ") + errorMsg));

    // 确保 replyReceived 没有被发射
    QCOMPARE(waitForReply(200), QString());
}

void DeepSeekClientNetworkTest::testSummaryTimeout()
{
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));

    s_client->setRequestTimeoutMs(200);

    QSignalSpy failSpy(s_client, &DeepSeekClient::summaryFailed);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendSummaryRequest(history, QStringLiteral("test-summary-1"));

    int gotFail = waitForSpy(failSpy, 2000);
    QCOMPARE_GT(gotFail, 0);

    QString requestId = failSpy.at(0).at(0).toString();
    QString errorMsg = failSpy.at(0).at(1).toString();
    QCOMPARE(requestId, QStringLiteral("test-summary-1"));
    QVERIFY2(errorMsg.contains(QStringLiteral("超时")),
             qPrintable(QStringLiteral("超时错误消息应包含'超时'，实际: ") + errorMsg));
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. 主动取消
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testCancelMessage()
{
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));
    s_client->setRequestTimeoutMs(5000);  // 足够长，确保是取消而不是超时

    QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);
    QSignalSpy replySpy(s_client, &DeepSeekClient::replyReceived);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendMessage(QStringLiteral("你好"), history);

    // 立即取消
    s_client->cancelMessage();

    // 等待事件循环处理
    QTest::qWait(500);

    // 取消后不应有任何信号
    QCOMPARE(errorSpy.count(), 0);
    QCOMPARE(replySpy.count(), 0);
}

void DeepSeekClientNetworkTest::testCancelSummary()
{
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));
    s_client->setRequestTimeoutMs(5000);

    QSignalSpy failSpy(s_client, &DeepSeekClient::summaryFailed);
    QSignalSpy succSpy(s_client, &DeepSeekClient::summaryReceived);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendSummaryRequest(history, QStringLiteral("cancel-test"));

    // 立即取消
    s_client->cancelSummary();

    QTest::qWait(500);

    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(succSpy.count(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 3. 单次终结
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testMessageSingleFinalization()
{
    // 正常回复
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("streaming_ok")));
    s_client->setRequestTimeoutMs(300);

    QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);
    QSignalSpy replySpy(s_client, &DeepSeekClient::replyReceived);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendMessage(QStringLiteral("你好"), history);

    // 等待回复
    QVERIFY(waitForSpy(replySpy, 3000) > 0);

    // 确认只有一次终结：replyReceived 被发射一次，errorOccurred 没有被发射
    QCOMPARE(replySpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);

    // 确认回复内容非空
    QString reply = replySpy.at(0).at(0).toString();
    QVERIFY(!reply.isEmpty());
}

void DeepSeekClientNetworkTest::testSummarySingleFinalization()
{
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("nonstreaming_ok")));
    s_client->setRequestTimeoutMs(300);

    QSignalSpy failSpy(s_client, &DeepSeekClient::summaryFailed);
    QSignalSpy succSpy(s_client, &DeepSeekClient::summaryReceived);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendSummaryRequest(history, QStringLiteral("single-finalization"));

    // 等待成功回复
    QVERIFY(waitForSpy(succSpy, 3000) > 0);

    QCOMPARE(succSpy.count(), 1);
    QCOMPARE(failSpy.count(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. 迟到结果丢弃（模拟 App 层 summary 超时后迟到）
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testLateSummaryDiscarded()
{
    // 模拟：App 层在 15s 超时后取消摘要请求，
    // 但网络层已经发回结果，此时 DeepSeekClient 不应再发射任何信号。

    QSignalSpy failSpy(s_client, &DeepSeekClient::summaryFailed);
    QSignalSpy succSpy(s_client, &DeepSeekClient::summaryReceived);

    // 使用无延迟的回复
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("nonstreaming_ok")));
    s_client->setRequestTimeoutMs(5000);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendSummaryRequest(history, QStringLiteral("late-test"));

    // 模拟 App 层提前取消（像 15s 超时那样）
    s_client->cancelSummary();

    // 确保不会收到任何信号（取消后 cleanupRequest 已经清除状态）
    QTest::qWait(1000);

    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(succSpy.count(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 5. 失败恢复
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testMessageTimeoutThenResend()
{
    // 第一次请求：超时
    {
        QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));
        s_client->setRequestTimeoutMs(200);

        QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);

        QList<ChatMessage> history;
        history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("首次消息")));
        s_client->sendMessage(QStringLiteral("首次消息"), history);

        QVERIFY(waitForSpy(errorSpy, 2000) > 0);
        QCOMPARE(errorSpy.count(), 1);
    }

    // 第二次请求：正常回复（超时恢复后可再次发送）
    {
        QVERIFY(s_dsMock.setDelay(0));
        QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("streaming_ok")));
        s_client->setRequestTimeoutMs(3000);

        QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);
        QSignalSpy replySpy(s_client, &DeepSeekClient::replyReceived);

        QList<ChatMessage> history;
        history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("重发消息")));
        s_client->sendMessage(QStringLiteral("重发消息"), history);

        QVERIFY(waitForSpy(replySpy, 3000) > 0);
        QCOMPARE(replySpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        QString reply = replySpy.at(0).at(0).toString();
        QVERIFY(!reply.isEmpty());
        QVERIFY(reply.contains(QStringLiteral("你好")));
    }
}

void DeepSeekClientNetworkTest::testNetworkErrorThenResend()
{
    // 第一次请求：HTTP 500 错误
    {
        s_client->setRequestTimeoutMs(3000);
        QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("streaming_ok")));
        QVERIFY(s_dsMock.setError(500));

        QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);

        QList<ChatMessage> history;
        history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("首次消息")));
        s_client->sendMessage(QStringLiteral("首次消息"), history);

        QVERIFY(waitForSpy(errorSpy, 3000) > 0);
        QCOMPARE(errorSpy.count(), 1);
    }

    // 第二次请求：正常回复（错误恢复后可再次发送）
    {
        QVERIFY(s_dsMock.clearError());
        QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("streaming_ok")));

        QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);
        QSignalSpy replySpy(s_client, &DeepSeekClient::replyReceived);

        QList<ChatMessage> history;
        history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("重发消息")));
        s_client->sendMessage(QStringLiteral("重发消息"), history);

        QVERIFY(waitForSpy(replySpy, 3000) > 0);
        QCOMPARE(replySpy.count(), 1);
        QCOMPARE(errorSpy.count(), 0);

        QString reply = replySpy.at(0).at(0).toString();
        QVERIFY(!reply.isEmpty());
    }
}
