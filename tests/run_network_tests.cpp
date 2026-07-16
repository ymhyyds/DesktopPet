/**
 * @brief DeepSeekClient 网络超时与错误恢复单元测试 — 独立可执行文件
 *
 * 构建（在 build_release_readiness 目录下运行 cmake 后）：
 *   cmake --build . --target test_ds_network --config Debug
 * 运行：
 *   ctest -C Debug -R test_ds_network --output-on-failure
 */

#include <QApplication>
#include <QTest>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <QProcess>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include "ai/DeepSeekClient.h"
#include "MockServer.h"
#include "MockServerHelper.cpp"  // MockServer method implementations

// ═══════════════════════════════════════════════════════════════════════════
// Test class
// ═══════════════════════════════════════════════════════════════════════════

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
    static MockServer s_dsMock;
    static DeepSeekClient *s_client;
};

MockServer DeepSeekClientNetworkTest::s_dsMock;
DeepSeekClient *DeepSeekClientNetworkTest::s_client = nullptr;

// ── Helper: wait for signal ───────────────────────────────────────────────
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
    QString scriptPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/MockDeepSeekServer.py");
    if (!QFile::exists(scriptPath)) {
        scriptPath = QStringLiteral("MockDeepSeekServer.py");
    }
    QVERIFY2(s_dsMock.start(scriptPath),
             qPrintable(QStringLiteral("Failed to start mock server: %1")
                            .arg(s_dsMock.lastErrorString())));

    s_client = new DeepSeekClient();
}

void DeepSeekClientNetworkTest::cleanupTestCase()
{
    delete s_client;
    s_client = nullptr;
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

    s_client->setApiKey(QStringLiteral("sk-test-mock-key"));
    s_client->setEndpoint(s_dsMock.baseUrl() + QStringLiteral("/chat/completions"));
    s_client->setRequestTimeoutMs(300);
}

void DeepSeekClientNetworkTest::cleanup()
{
    if (s_client) {
        s_client->cancelMessage();
        s_client->cancelSummary();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// 1. 超时
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testMessageTimeout()
{
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));
    s_client->setRequestTimeoutMs(200);

    QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendMessage(QStringLiteral("你好"), history);

    int gotError = waitForSpy(errorSpy, 3000);
    QCOMPARE_GT(gotError, 0);

    QString errorMsg = errorSpy.at(0).at(0).toString();
    QVERIFY(errorMsg.contains(QStringLiteral("超时")));

    // 确保只终结一次（只有一个 errorOccurred 信号）
    QCOMPARE(errorSpy.count(), 1);
}

void DeepSeekClientNetworkTest::testSummaryTimeout()
{
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));
    s_client->setRequestTimeoutMs(200);

    QSignalSpy failSpy(s_client, &DeepSeekClient::summaryFailed);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendSummaryRequest(history, QStringLiteral("test-summary-1"));

    int gotFail = waitForSpy(failSpy, 3000);
    QCOMPARE_GT(gotFail, 0);

    QString requestId = failSpy.at(0).at(0).toString();
    QString errorMsg = failSpy.at(0).at(1).toString();
    QCOMPARE(requestId, QStringLiteral("test-summary-1"));
    QVERIFY(errorMsg.contains(QStringLiteral("超时")));

    QCOMPARE(failSpy.count(), 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// 2. 主动取消
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testCancelMessage()
{
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("timeout")));
    s_client->setRequestTimeoutMs(5000);

    QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);
    QSignalSpy replySpy(s_client, &DeepSeekClient::replyReceived);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendMessage(QStringLiteral("你好"), history);

    // 立即取消
    s_client->cancelMessage();

    // 给事件循环时间以处理 abort
    QTest::qWait(500);

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
    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("streaming_ok")));
    s_client->setRequestTimeoutMs(300);

    QSignalSpy errorSpy(s_client, &DeepSeekClient::errorOccurred);
    QSignalSpy replySpy(s_client, &DeepSeekClient::replyReceived);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendMessage(QStringLiteral("你好"), history);

    // 等待回复
    QVERIFY(waitForSpy(replySpy, 5000) > 0);

    QCOMPARE(replySpy.count(), 1);
    QCOMPARE(errorSpy.count(), 0);

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

    QVERIFY(waitForSpy(succSpy, 5000) > 0);

    QCOMPARE(succSpy.count(), 1);
    QCOMPARE(failSpy.count(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// 4. 迟到结果丢弃
// ═══════════════════════════════════════════════════════════════════════════

void DeepSeekClientNetworkTest::testLateSummaryDiscarded()
{
    QSignalSpy failSpy(s_client, &DeepSeekClient::summaryFailed);
    QSignalSpy succSpy(s_client, &DeepSeekClient::summaryReceived);

    QVERIFY(s_dsMock.setResponseType("both", QStringLiteral("nonstreaming_ok")));
    s_client->setRequestTimeoutMs(5000);

    QList<ChatMessage> history;
    history.append(ChatMessage(QStringLiteral("user"), QStringLiteral("你好")));
    s_client->sendSummaryRequest(history, QStringLiteral("late-test"));

    // 模拟 App 层提前取消（像 15s 超时那样）
    s_client->cancelSummary();

    // 取消后不应收到任何信号
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

        QVERIFY(waitForSpy(errorSpy, 3000) > 0);
        QCOMPARE(errorSpy.count(), 1);
    }

    // 第二次请求：超时恢复后可再次发送
    {
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
    // 第一次请求：HTTP 500
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

    // 第二次请求：正常回复
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

// ═══════════════════════════════════════════════════════════════════════════
// Main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    DeepSeekClientNetworkTest tc;
    int result = QTest::qExec(&tc, argc, argv);

    qDebug() << "\n==========================================";
    qDebug() << "DeepSeekClient-Network tests:"
             << (result == 0 ? "ALL PASSED" : "FAILED");
    qDebug() << "==========================================\n";

    return result;
}

#include "run_network_tests.moc"
