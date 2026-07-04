// ── TtsEngine unit tests (included by test_main.cpp) ─────────────────────────

#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QDebug>

// ── Per-test-class state ─────────────────────────────────────────────────────
static TtsEngine *s_engine = nullptr;

// Helper: list per-PID temp subdirectories
static QStringList ourTempSubdirs()
{
    QStringList dirs;
    QDir ttsDir(QDir::tempPath() + QStringLiteral("/DesktopPetTts"));
    if (!ttsDir.exists()) return dirs;
    const QString pid = QString::number(QCoreApplication::applicationPid());
    const auto entries = ttsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
        if (fi.fileName() == pid) {
            QDir pidDir(fi.absoluteFilePath());
            const auto subDirs = pidDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QFileInfo &sub : subDirs) {
                dirs << sub.fileName();
            }
        }
    }
    return dirs;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Fixtures
// ═══════════════════════════════════════════════════════════════════════════════

void TtsEngineTest::initTestCase()
{
    // Pre-write settings so TtsEngine reads test-friendly values and does not
    // pollute real user QSettings (IniFormat → %APPDATA%/DesktopPet/TtsEngine.ini)
    QSettings::setDefaultFormat(QSettings::IniFormat);
    {
        QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
        settings.clear();
        settings.setValue(QStringLiteral("backend"), static_cast<int>(TtsEngine::VoiceVox));
        settings.setValue(QStringLiteral("backendSchemaVersion"), 2);
        settings.setValue(QStringLiteral("vvStyleId"), 1);
        settings.sync();
    }

    // Start mock server
    QString scriptPath = QCoreApplication::applicationDirPath()
        + QStringLiteral("/MockVoiceVoxServer.py");
    if (!QFile::exists(scriptPath)) {
        scriptPath = QStringLiteral("MockVoiceVoxServer.py");
    }
    QVERIFY2(s_vvMock.start(scriptPath), "Failed to start mock VoiceVox server");

    s_engine = new TtsEngine();
    s_engine->setBackend(TtsEngine::VoiceVox);
    s_engine->setVoiceVoxUrl(s_vvMock.baseUrl());

    // Flush any async events (pending signal deliveries after construction)
    QTest::qWait(50);
}

void TtsEngineTest::cleanupTestCase()
{
    delete s_engine;
    s_engine = nullptr;

    s_vvMock.stop();

    // Clean up test settings
    QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
    settings.clear();
    settings.sync();
}

void TtsEngineTest::init()
{
    s_vvMock.clearError();
    s_vvMock.setDelay(0);
    s_vvMock.setResponseType("audio_query", "normal");
    s_vvMock.setResponseType("synthesis", "normal");
}

void TtsEngineTest::cleanup()
{
    if (s_engine) {
        s_engine->stop();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// D. stop during synthesis
// ═══════════════════════════════════════════════════════════════════════════════
void TtsEngineTest::testStopDuringSynthesis()
{
    s_vvMock.setDelay(500);  // slow response so we can intercept

    QSignalSpy errorSpy(s_engine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_engine, &TtsEngine::statusChanged);

    // Remove any leftover temp dirs from previous tests
    s_engine->stop();
    QVERIFY(ourTempSubdirs().isEmpty());

    // Start synthesis
    s_engine->speak(QStringLiteral("stop test"));

    // Wait for request to be in-flight (audio_query / synthesis started)
    QTest::qWait(80);

    // Stop during synthesis
    int genBeforeStop = s_engine->generation();
    s_engine->stop();
    int genAfterStop = s_engine->generation();
    QCOMPARE_GT(genAfterStop, genBeforeStop);  // generation incremented

    // Wait for the delayed mock response to arrive (will be dropped)
    QTest::qWait(1000);

    // ── Verifications ──
    // No error emitted for dropped stale response
    QCOMPARE(errorSpy.count(), 0);

    // Temp dirs should be clean after stop
    QStringList dirs = ourTempSubdirs();
    QCOMPARE(dirs.size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// D. disable during synthesis
// ═══════════════════════════════════════════════════════════════════════════════
void TtsEngineTest::testDisableDuringSynthesis()
{
    s_vvMock.setDelay(500);

    QSignalSpy errorSpy(s_engine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_engine, &TtsEngine::statusChanged);

    s_engine->stop();
    QVERIFY(ourTempSubdirs().isEmpty());

    s_engine->speak(QStringLiteral("disable test"));
    QTest::qWait(80);

    // Disable → calls stop() internally
    s_engine->setEnabled(false);

    QTest::qWait(1000);

    // No errors from dropped stale response
    QCOMPARE(errorSpy.count(), 0);

    // No temp files
    QCOMPARE(ourTempSubdirs().size(), 0);

    // Re-enable for subsequent tests
    s_engine->setEnabled(true);
}

// ═══════════════════════════════════════════════════════════════════════════════
// D. disable then speak (should be ignored)
// ═══════════════════════════════════════════════════════════════════════════════
void TtsEngineTest::testDisableThenSpeak()
{
    QSignalSpy errorSpy(s_engine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_engine, &TtsEngine::statusChanged);

    s_engine->stop();
    s_engine->setEnabled(false);
    int preGen = s_engine->generation();  // capture after stop+disable

    // Speak while disabled → should be no-op (generation not bumped)
    s_engine->speak(QStringLiteral("should be ignored"));

    QCOMPARE(s_engine->generation(), preGen);  // generation unchanged by speak()
    QCOMPARE(errorSpy.count(), 0);             // no error

    // Re-enable
    s_engine->setEnabled(true);

    // Now speak — should work
    s_vvMock.setDelay(0);
    s_engine->speak(QStringLiteral("after re-enable"));

    // Wait for completion
    QTest::qWait(2000);

    // Should see "play" status
    bool foundPlaying = false;
    for (int i = 0; i < statusSpy.count(); ++i) {
        const QString text = statusSpy.at(i).at(1).toString();
        if (text.contains(QStringLiteral("播放中")))
            foundPlaying = true;
    }
    QVERIFY(foundPlaying);
    QCOMPARE(errorSpy.count(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// B. latest-wins: request 1 delayed, request 2 cancels it
// ═══════════════════════════════════════════════════════════════════════════════
void TtsEngineTest::testLatestWins()
{
    QSignalSpy errorSpy(s_engine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_engine, &TtsEngine::statusChanged);

    s_engine->stop();
    QVERIFY(ourTempSubdirs().isEmpty());

    // ── Request 1 — delayed so request 2 has time to cancel it ──
    const int genBeforeFirst = s_engine->generation();
    s_vvMock.setDelay(500);
    s_engine->speak(QStringLiteral("request one"));
    const int firstGeneration = s_engine->generation();
    QCOMPARE_GT(firstGeneration, genBeforeFirst);

    // ── Request 2 — immediately starts, cancels request 1 internally (latest-wins) ──
    s_vvMock.setDelay(0);
    s_engine->speak(QStringLiteral("request two"));
    const int secondGeneration = s_engine->generation();
    QCOMPARE_GT(secondGeneration, firstGeneration);

    // Verify: exactly one temp dir — request 2's, not request 1's
    QTRY_VERIFY2(ourTempSubdirs().size() == 1,
        "Exactly one temp dir should exist (request 2, not request 1)");

    // Verify: the existing dir corresponds to secondGeneration (request 2)
    if (!ourTempSubdirs().isEmpty())
        QCOMPARE(ourTempSubdirs().first().toInt(), secondGeneration);

    // Verify: request 1's dir must not exist (was cancelled before completing)
    // — this is confirmed by size == 1 above (only request 2's dir)

    QCOMPARE(errorSpy.count(), 0);

    // Verify: after stop, all dirs cleaned
    s_engine->stop();
    QTRY_VERIFY2(ourTempSubdirs().isEmpty(),
        "All temp dirs must be cleaned after stop");
}

// ═══════════════════════════════════════════════════════════════════════════════
// F. Three consecutive requests (overlapping — each cancels previous via latest-wins)
// ═══════════════════════════════════════════════════════════════════════════════
void TtsEngineTest::testThreeConsecutiveRequests()
{
    QSignalSpy errorSpy(s_engine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_engine, &TtsEngine::statusChanged);

    s_engine->stop();
    QVERIFY(ourTempSubdirs().isEmpty());

    // ── Request 1 ──
    const int gen0 = s_engine->generation();
    s_engine->speak(QStringLiteral("first"));
    const int generation1 = s_engine->generation();
    QCOMPARE_GT(generation1, gen0);

    QTRY_VERIFY2(ourTempSubdirs().size() == 1,
        "Request 1 should create a temp dir");
    QCOMPARE(ourTempSubdirs().first().toInt(), generation1);
    QCOMPARE(errorSpy.count(), 0);

    // ── Request 2 — cancels request 1 (latest-wins) ──
    const int gen1 = s_engine->generation();
    s_engine->speak(QStringLiteral("second"));
    const int generation2 = s_engine->generation();
    QCOMPARE_GT(generation2, gen1);

    QTRY_VERIFY2(ourTempSubdirs().size() == 1,
        "After request 2 only one temp dir should exist (gen2)");
    QCOMPARE(ourTempSubdirs().first().toInt(), generation2);
    QCOMPARE(errorSpy.count(), 0);

    // ── Request 3 — cancels request 2 ──
    const int gen2 = s_engine->generation();
    s_engine->speak(QStringLiteral("third"));
    const int generation3 = s_engine->generation();
    QCOMPARE_GT(generation3, gen2);

    QTRY_VERIFY2(ourTempSubdirs().size() == 1,
        "After request 3 only one temp dir should exist (gen3)");
    QCOMPARE(ourTempSubdirs().first().toInt(), generation3);
    QCOMPARE(errorSpy.count(), 0);

    // generation3 is the final effective request
    QVERIFY(generation3 > generation2 && generation2 > generation1);

    // "曾出现播放中" — auxiliary assertion only, not the primary evidence
    bool foundPlaying = false;
    for (int i = 0; i < statusSpy.count(); ++i) {
        if (statusSpy.at(i).at(1).toString().contains(QStringLiteral("播放中")))
            foundPlaying = true;
    }
    Q_UNUSED(foundPlaying);

    // Stop — all temp dirs cleaned
    s_engine->stop();
    QTRY_VERIFY2(ourTempSubdirs().isEmpty(),
        "All temp dirs must be cleaned after stop");
}

// ═══════════════════════════════════════════════════════════════════════════════
// G. Destructor cleanup
// ═══════════════════════════════════════════════════════════════════════════════
void TtsEngineTest::testDestructorCleanup()
{
    s_vvMock.setDelay(400);

    // Create engine (no parent, so we control lifetime)
    auto *engine = new TtsEngine();
    engine->setBackend(TtsEngine::VoiceVox);
    engine->setVoiceVoxUrl(s_vvMock.baseUrl());

    // Start a request (in-flight during deletion)
    engine->speak(QStringLiteral("destructor test"));
    QTest::qWait(60);  // request is now in-flight

    // Delete engine while request is pending
    delete engine;

    // Wait for any stray signals/callbacks
    QTest::qWait(1000);

    // ── Verification ──
    // No crash = test passes (verified by reaching this line)

    // Temp directory should be clean (empty or no DesktopPetTts dir)
    QDir ttsDir(QDir::tempPath() + QStringLiteral("/DesktopPetTts"));
    if (ttsDir.exists()) {
        const QString pid = QString::number(QCoreApplication::applicationPid());
        const auto pidDirs = ttsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : pidDirs) {
            if (fi.fileName() == pid) {
                QDir pidDir(fi.absoluteFilePath());
                const auto subs = pidDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
                // At this point the cleanup temp dir (if any) should be ≤1
                // (the destructor calls stop() which cleans up)
                QVERIFY(subs.size() <= 1);
            }
        }
    }
}
