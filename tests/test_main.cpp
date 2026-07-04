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
};

// ── Mock server helper ─────────────────────────────────────────────────────
struct MockServer {
    QProcess *proc = nullptr;
    int port = 0;

    bool start(const QString &scriptPath);
    void stop();
    void setDelay(int ms);
    void setError(int httpCode, const QString &target = QStringLiteral("both"));
    void setResponseType(const QString &target, const QString &type);
    void clearError();
    QString baseUrl() const;
};

// ── Include all test implementations ───────────────────────────────────────
#include "VoiceVoxClientTest.cpp"
#include "TtsEngineTest.cpp"
#include "MockServerHelper.cpp"

// ── Main ───────────────────────────────────────────────────────────────────
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    int status = 0;
    int total = 0, passed = 0, failed = 0;

    qDebug() << "\n==========================================";
    qDebug() << "  TTS Unit Tests — Mock VoiceVox Server";
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

    qDebug() << "==========================================";
    qDebug() << "  Results:" << total << "suites,"
             << passed << "passed," << failed << "failed";
    qDebug() << "==========================================";

    return status;
}

#include "test_main.moc"
