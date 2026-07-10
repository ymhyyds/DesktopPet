// ── VoiceVoxClient unit tests (included by test_main.cpp) ──────────────────

#include <QTest>
#include <QSignalSpy>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QCoreApplication>
#include <QDebug>

// ── Per-test-class fixtures ────────────────────────────────────────────────
static MockServer s_vvMock;
static VoiceVoxClient *s_vvClient = nullptr;

void VoiceVoxClientTest::initTestCase()
{
    // Start mock server
    QString scriptPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/MockVoiceVoxServer.py");
    if (!QFile::exists(scriptPath)) {
        // Try alongside test source
        scriptPath = QStringLiteral("MockVoiceVoxServer.py");
    }
    QVERIFY2(s_vvMock.start(scriptPath), "Failed to start mock VoiceVox server");
}

void VoiceVoxClientTest::cleanupTestCase()
{
    s_vvMock.stop();
}

void VoiceVoxClientTest::init()
{
    QVERIFY2(s_vvMock.clearError(),
             qPrintable(QStringLiteral("clearError failed: %1").arg(s_vvMock.lastErrorString())));
    QVERIFY2(s_vvMock.setDelay(0),
             qPrintable(QStringLiteral("setDelay failed: %1").arg(s_vvMock.lastErrorString())));
    QVERIFY2(s_vvMock.waitUntilReady(),
             qPrintable(QStringLiteral("waitUntilReady failed: %1").arg(s_vvMock.lastErrorString())));

    s_vvClient = new VoiceVoxClient();
    s_vvClient->setBaseUrl(s_vvMock.baseUrl());
}

void VoiceVoxClientTest::cleanup()
{
    if (s_vvClient) {
        s_vvClient->cancelPendingRequests();
        delete s_vvClient;
        s_vvClient = nullptr;
    }
}

// ── Helper: wait for signal with timeout ───────────────────────────────────
template<typename T>
static int waitForSignal(T *obj, void (T::*signal)(), int timeoutMs = 5000)
{
    QSignalSpy spy(obj, signal);
    if (spy.count() > 0) return spy.count();
    return spy.wait(timeoutMs) ? spy.count() : -1;
}

static int waitForSignal(QSignalSpy &spy, int timeoutMs = 5000)
{
    if (spy.count() > 0) return spy.count();
    return spy.wait(timeoutMs) ? spy.count() : -1;
}

// ═══════════════════════════════════════════════════════════════════════════
// A. Single synthesis
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testSingleSynthesis()
{
    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    s_vvClient->synthesize(QStringLiteral("你好"), 1, 42);

    int gotAudio = waitForSignal(audioSpy, 5000);

    QCOMPARE_GT(gotAudio, 0);  // audioReady fired
    QCOMPARE(failSpy.count(), 0);  // no failure

    // Verify requestId and data
    QList<QVariant> args = audioSpy.at(0);
    QCOMPARE(args.at(0).toInt(), 42);  // requestId matches
    QByteArray wav = args.at(1).toByteArray();
    QVERIFY(!wav.isEmpty());  // non-empty WAV data
    QVERIFY(wav.size() > 44);  // WAV header + data
    QVERIFY(wav.startsWith("RIFF"));  // valid WAV magic
}

// ═══════════════════════════════════════════════════════════════════════════
// B. Cancel-then-synthesize (simulates TtsEngine latest-wins pattern)
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testCancelThenSynthesize()
{
    s_vvMock.setDelay(400);  // slow first request

    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    // Start request 1 — will be delayed by mock
    s_vvClient->synthesize(QStringLiteral("request one"), 1, 1);

    // Immediately cancel (simulating latest-wins at TtsEngine level)
    s_vvClient->cancelPendingRequests();

    s_vvMock.setDelay(0);  // second request immediate

    // Start request 2
    s_vvClient->synthesize(QStringLiteral("request two"), 1, 2);

    int gotAudio = waitForSignal(audioSpy, 5000);

    QCOMPARE_GT(gotAudio, 0);  // at least one audioReady

    int request2Count = 0;
    int request1Count = 0;
    for (int i = 0; i < audioSpy.count(); ++i) {
        int rid = audioSpy.at(i).at(0).toInt();
        if (rid == 2) request2Count++;
        if (rid == 1) request1Count++;
    }

    QCOMPARE_GT(request2Count, 0);      // request 2 completed
    QCOMPARE(request1Count, 0);         // request 1 was cancelled — no audioReady
}

// ═══════════════════════════════════════════════════════════════════════════
// C. Cancel pending requests
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testCancelPendingRequests()
{
    s_vvMock.setDelay(300);

    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    s_vvClient->synthesize(QStringLiteral("cancel me"), 1, 100);
    s_vvClient->cancelPendingRequests();

    // Wait a bit — no callbacks should fire after cancel
    QTest::qWait(800);

    QCOMPARE(audioSpy.count(), 0);  // no audioReady
    // requestFailed may fire for aborted requests — that's OK

    // Ensure mock server has fully recovered from the aborted connection
    // before the next test's init() tries to configure it.
    QVERIFY2(s_vvMock.waitUntilReady(),
             qPrintable(QStringLiteral("Mock server not ready after cancel: %1")
                            .arg(s_vvMock.lastErrorString())));
}

// ═══════════════════════════════════════════════════════════════════════════
// E1. HTTP 500 on audio_query
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testAudioQueryError()
{
    QVERIFY2(s_vvMock.setError(500, QStringLiteral("audio_query")),
             qPrintable(QStringLiteral("setError(500, audio_query) failed: %1")
                            .arg(s_vvMock.lastErrorString())));

    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    s_vvClient->synthesize(QStringLiteral("error test"), 1, 200);

    int gotFail = waitForSignal(failSpy, 5000);

    QCOMPARE_GT(gotFail, 0);        // requestFailed fired
    QCOMPARE(audioSpy.count(), 0);  // no audioReady
    QCOMPARE(failSpy.at(0).at(0).toInt(), 200);  // requestId preserved
}

// ═══════════════════════════════════════════════════════════════════════════
// E2. HTTP 500 on synthesis
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testSynthesisError()
{
    s_vvMock.setError(500, QStringLiteral("synthesis"));

    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    s_vvClient->synthesize(QStringLiteral("synth error"), 1, 300);

    int gotFail = waitForSignal(failSpy, 5000);

    QCOMPARE_GT(gotFail, 0);        // requestFailed fired
    QCOMPARE(audioSpy.count(), 0);  // no audioReady
    QCOMPARE(failSpy.at(0).at(0).toInt(), 300);  // requestId preserved
}

// ═══════════════════════════════════════════════════════════════════════════
// E3. Connection refused (no server on port)
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testConnectionRefused()
{
    auto *client = new VoiceVoxClient(this);
    client->setBaseUrl(QStringLiteral("http://127.0.0.1:1"));  // unlikely to be listening

    QSignalSpy failSpy(client, &VoiceVoxClient::requestFailed);

    client->synthesize(QStringLiteral("no server"), 1, 400);

    int gotFail = waitForSignal(failSpy, 10000);  // longer timeout for connection refused

    QCOMPARE_GT(gotFail, 0);   // requestFailed fired
    QCOMPARE(failSpy.at(0).at(0).toInt(), 400);  // requestId preserved

    delete client;
}

// ═══════════════════════════════════════════════════════════════════════════
// E4. Invalid JSON from audio_query
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testAudioQueryInvalidJson()
{
    // VoiceVoxClient does NOT parse the audio_query JSON — it forwards it
    // verbatim to synthesis. An invalid-JSON response from the query endpoint
    // would just be forwarded, and the synthesis endpoint (which gets non-JSON
    // in the body) may then fail. This test verifies the client does not crash
    // when receiving invalid JSON from audio_query.
    s_vvMock.setResponseType("audio_query", "invalid_json");

    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    s_vvClient->synthesize(QStringLiteral("test"), 1, 200);

    // At least one of audioReady or requestFailed must fire eventually
    // (The synthesis endpoint receives "this is not valid json output" as JSON,
    // which it processes as bytes — but our mock synthesis returns valid WAV
    // regardless of body content.)
    bool anyFired = waitForSignal(audioSpy, 5000) > 0
                 || waitForSignal(failSpy, 5000) > 0;
    QVERIFY(anyFired);
    // No crash means the test passes
}

// ═══════════════════════════════════════════════════════════════════════════
// E5. Empty WAV from synthesis
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testSynthesisEmptyWav()
{
    s_vvMock.setResponseType("synthesis", "empty");

    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    s_vvClient->synthesize(QStringLiteral("test"), 1, 300);

    int gotFail = waitForSignal(failSpy, 5000);

    QCOMPARE_GT(gotFail, 0);        // requestFailed fired (empty data)
    QCOMPARE(audioSpy.count(), 0);  // no audioReady
    QCOMPARE(failSpy.at(0).at(0).toInt(), 300);  // requestId preserved
}

// ═══════════════════════════════════════════════════════════════════════════
// F. Three consecutive requests (non-overlapping)
// ═══════════════════════════════════════════════════════════════════════════
void VoiceVoxClientTest::testThreeConsecutiveRequests()
{
    QSignalSpy audioSpy(s_vvClient, &VoiceVoxClient::audioReady);
    QSignalSpy failSpy(s_vvClient, &VoiceVoxClient::requestFailed);

    // Request 1
    s_vvClient->synthesize(QStringLiteral("first"), 1, 10);
    QVERIFY(waitForSignal(audioSpy, 5000) > 0);
    QCOMPARE(failSpy.count(), 0);
    QCOMPARE(audioSpy.at(0).at(0).toInt(), 10);
    audioSpy.clear();

    // Request 2
    s_vvClient->synthesize(QStringLiteral("second"), 1, 20);
    QVERIFY(waitForSignal(audioSpy, 5000) > 0);
    QCOMPARE(audioSpy.at(0).at(0).toInt(), 20);
    audioSpy.clear();

    // Request 3
    s_vvClient->synthesize(QStringLiteral("third"), 1, 30);
    QVERIFY(waitForSignal(audioSpy, 5000) > 0);
    QCOMPARE(audioSpy.at(0).at(0).toInt(), 30);

    // All three completed successfully with no failures
    QCOMPARE(failSpy.count(), 0);
}
