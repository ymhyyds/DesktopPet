#include <QApplication>
#include <QTest>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include "VoiceVoxClient.h"
#include "TtsEngine.h"

// ── Forward declarations of test classes ──────────────────────────────────
class VoiceVoxClientTest : public QObject {
    Q_OBJECT
public:
    explicit VoiceVoxClientTest() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testSingleSynthesis();
    void testCancelThenSynthesize();
    void testCancelPendingRequests();
    void testAudioQueryError();
    void testSynthesisError();
    void testConnectionRefused();
    void testAudioQueryInvalidJson();
    void testSynthesisEmptyWav();
    void testThreeConsecutiveRequests();
};

class TtsEngineTest : public QObject {
    Q_OBJECT
public:
    explicit TtsEngineTest() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testStopDuringSynthesis();
    void testDisableDuringSynthesis();
    void testDisableThenSpeak();
    void testLatestWins();
    void testThreeConsecutiveRequests();
    void testDestructorCleanup();

    // ── Speaking state transitions ──
    void testStopOnIdleEngine();
    void testInterruptOnStop();
    void testInterruptOnDisable();
    void testInterruptOnBackendSwitch();
    void testConsecutiveSpeakNoDuplicateInterrupt();
    void testSpeakAfterStopNoExtraInterrupt();
};

class EdgeTtsEngineTest : public QObject {
    Q_OBJECT
public:
    explicit EdgeTtsEngineTest() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testNormalSynthesis();
    void testPluginNotFound();
    void testProcessStartFailed();
    void testNonZeroExit();
    void testEmptyOutput();
    void testTimeout();
    void testLatestWins();
    void testSwitchToVoiceVoxDuringEdge();
    void testSwitchFromVoiceVoxToEdge();
    void testDisabledThenSpeak();
    void testStop();
    void testDestructorCleanup();
    void testChineseAndSpecialChars();
    void testSpacesInPath();
};

// ── Thunder From一 ──
#include "WeatherManagerTest.h"

// ── Evidence Manager MVP ──
#include "EvidenceManagerTest.h"

// ── DeepSeekClient API Key 持久化 ──
#include "DeepSeekClientKeyPersistenceTest.h"

// ── Mock server helper ─────────────────────────────────────────────────────
#include "MockServer.h"

// ── Include all test implementations ───────────────────────────────────────
#include "VoiceVoxClientTest.cpp"
#include "TtsEngineTest.cpp"
#include "EdgeTtsEngineTest.cpp"
#include "MockServerHelper.cpp"
#include "WeatherManagerTest.cpp"
#include "EvidenceManagerTest.cpp"

// ── Main ───────────────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    int status = 0;
    int total = 0, passed = 0, failed = 0;

    qDebug() << "\n==========================================";
    qDebug() << "  TTS Unit Tests — VoiceVox + Edge TTS Mocks";
    qDebug() << "==========================================\n";

    auto runSuite = [&](const QString &name, QObject &testObj) {
        qDebug() << "── " << name << " ──";
        int result = QTest::qExec(&testObj, argc, argv);
        if (result == 0) {
            qDebug() << "  ✓ Suite passed\n";
            passed++;
        } else {
            qDebug() << "  ✗ Suite FAILED (exit:" << result << ")\n";
            failed++;
        }
        status |= result;
        total++;
    };

    {
        VoiceVoxClientTest tc;
        runSuite(QStringLiteral("VoiceVoxClient"), tc);
    }
    {
        TtsEngineTest tc;
        runSuite(QStringLiteral("TtsEngine"), tc);
    }
    {
        EdgeTtsEngineTest tc;
        runSuite(QStringLiteral("EdgeTtsEngine"), tc);
    }
    {
        WeatherManagerTest tc;
        runSuite(QStringLiteral("WeatherManager"), tc);
    }
    {
        PromptManagerWeatherTest tc;
        runSuite(QStringLiteral("PromptManager-Weather"), tc);
    }
    {
        PromptManagerSummaryTest tc;
        runSuite(QStringLiteral("PromptManager-Summary"), tc);
    }
    {
        EvidenceManagerTest tc;
        runSuite(QStringLiteral("EvidenceManager"), tc);
    }
    {
        DeepSeekClientKeyPersistenceTest tc;
        runSuite(QStringLiteral("DeepSeekClient-KeyPersistence"), tc);
    }

    qDebug() << "==========================================";
    qDebug() << "  Results:" << total << "suites,"
             << passed << "passed," << failed << "failed";
    qDebug() << "==========================================";

    return status;
}

#include "test_main.moc"
