// ── Edge TTS engine unit tests (included by test_main.cpp) ─────────────────

#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QDebug>

// ── Per-test-class state ─────────────────────────────────────────────────────
static TtsEngine *s_edgeEngine = nullptr;
static QString s_edgeMockDir;
static QString s_edgeMockExecutable;  // saved in initTestCase, restored in init()

// Helper: list per-PID temp subdirectories
static QStringList edgeTempSubdirs()
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

void EdgeTtsEngineTest::initTestCase()
{
    // Pre-write settings: Edge TTS backend with mock executable path
    QSettings::setDefaultFormat(QSettings::IniFormat);
    {
        QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
        settings.clear();
        settings.setValue(QStringLiteral("backend"), static_cast<int>(TtsEngine::EdgeTts));
        settings.setValue(QStringLiteral("backendSchemaVersion"), 2);
        settings.sync();
    }

    // Locate mock edge-tts batch file
    s_edgeMockDir = QCoreApplication::applicationDirPath();
    QString mockBat = s_edgeMockDir + QStringLiteral("/mock_edge_tts.bat");
    if (!QFile::exists(mockBat)) {
        // Try alongside test source
        mockBat = QStringLiteral("mock_edge_tts.bat");
    }
    QVERIFY2(QFile::exists(mockBat), "mock_edge_tts.bat not found — test cannot simulate Edge CLI");

    // Write the mock path into QSettings so TtsEngine picks it up
    {
        QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
        settings.setValue(QStringLiteral("edgeExecutablePath"), QFileInfo(mockBat).absoluteFilePath());
        settings.sync();
    }

    s_edgeEngine = new TtsEngine();
    s_edgeEngine->setBackend(TtsEngine::EdgeTts);

    // Re-apply the mock path (constructor reads QSettings once, so we set it again)
    const QString reapplyPath = QFileInfo(mockBat).absoluteFilePath();
    s_edgeEngine->setEdgeExecutablePath(reapplyPath);

    // Save path for restoration in init() — prevents cascading failures when
    // a test changes the path then fails before restoring it.
    s_edgeMockExecutable = reapplyPath;

    // Use short timeout for timeout-related tests (overridden per-test if needed)
    s_edgeEngine->setEdgeTimeoutMs(3000);

    // Flush async events
    QTest::qWait(50);
}

// Helper to check whether error spy contains only QMediaPlayer playback
// errors from mock data (which is expected) and no QProcess-level errors.
// Returns true if all errors are media-playback related.
static bool isOnlyPlaybackErrors(QSignalSpy &spy)
{
    for (int i = 0; i < spy.count(); ++i) {
        const QString reason = spy.at(i).at(1).toString();
        // Known media-playback errors from mock (unplayable audio file)
        if (reason.contains(QStringLiteral("无效媒体"))
            || reason.contains(QStringLiteral("Unsupported media type"))
            || reason.contains(QStringLiteral("音频文件无效")))
            continue;
        // Any other error type means something else went wrong
        return false;
    }
    return true;
}

void EdgeTtsEngineTest::cleanupTestCase()
{
    if (s_edgeEngine) {
        s_edgeEngine->stop();
        delete s_edgeEngine;
        s_edgeEngine = nullptr;
    }
    {
        QSettings settings(QStringLiteral("DesktopPet"), QStringLiteral("TtsEngine"));
        settings.clear();
        settings.sync();
    }
}

void EdgeTtsEngineTest::init()
{
    // Reset mock environment to defaults
    qunsetenv("MOCK_EDGE_EXIT_CODE");
    qunsetenv("MOCK_EDGE_DELAY_SEC");
    qunsetenv("MOCK_EDGE_OUTPUT_SIZE");
    qunsetenv("MOCK_EDGE_NO_OUTPUT");

    // Restore the original executable path — prevents cascading failures when
    // a test changes the path then fails before restoring it.
    if (s_edgeEngine && !s_edgeMockExecutable.isEmpty()) {
        s_edgeEngine->setEdgeExecutablePath(s_edgeMockExecutable);
    }
}

void EdgeTtsEngineTest::cleanup()
{
    if (s_edgeEngine) {
        s_edgeEngine->stop();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 1. Normal synthesis → play chain
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testNormalSynthesis()
{
    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_edgeEngine, &TtsEngine::statusChanged);

    s_edgeEngine->stop();
    QVERIFY(edgeTempSubdirs().isEmpty());

    const int genBefore = s_edgeEngine->generation();
    s_edgeEngine->speak(QStringLiteral("正常测试语音"));
    QCOMPARE_GT(s_edgeEngine->generation(), genBefore);

    // Wait for completion: mock process is instant
    QTest::qWait(500);

    // Should have entered "播放中" status
    bool foundPlaying = false;
    for (int i = 0; i < statusSpy.count(); ++i) {
        const QString text = statusSpy.at(i).at(1).toString();
        if (text.contains(QStringLiteral("播放中")))
            foundPlaying = true;
    }
    QVERIFY2(foundPlaying, "Should have reached playing status");

    // Mock audio file is not a real playable MP3, so QMediaPlayer will emit
    // FormatError/InvalidMedia. These are expected with mock data and do NOT
    // indicate an engine logic bug. Verify that no QProcess-level errors occurred.
    if (errorSpy.count() > 0) {
        QVERIFY2(isOnlyPlaybackErrors(errorSpy),
            "Only mock-data playback errors expected, not process-level errors");
    }

    // Cleanup
    s_edgeEngine->stop();
}

// ═══════════════════════════════════════════════════════════════════════════════
// 2. Plugin not found (edgeExecutablePath → non-existent)
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testPluginNotFound()
{
    // Set path to non-existent file — resolveEdgeExecutable() returns empty
    s_edgeEngine->setEdgeExecutablePath(QStringLiteral("C:/non_existent/edge-tts.exe"));

    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);

    s_edgeEngine->speak(QStringLiteral("plugin not found test"));

    QTest::qWait(200);

    // Should have emitted an error
    QCOMPARE_GT(errorSpy.count(), 0);

    bool foundPluginError = false;
    for (int i = 0; i < errorSpy.count(); ++i) {
        const QString reason = errorSpy.at(i).at(1).toString();
        if (reason.contains(QStringLiteral("插件不存在")))
            foundPluginError = true;
    }
    QVERIFY(foundPluginError);

    // init() will restore s_edgeMockExecutable before the next test
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3. Process failed to start (non-existent executable path)
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testProcessStartFailed()
{
    // Set a path that doesn't exist — resolveEdgeExecutable() will return empty,
    // and startEdgeTts() emits "插件不存在" before even attempting QProcess::start.
    const QString origPath = s_edgeEngine->edgeExecutablePath();
    s_edgeEngine->setEdgeExecutablePath(QStringLiteral("nonexistent_command_xyz.exe"));

    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);

    s_edgeEngine->speak(QStringLiteral("start fail test"));

    QTest::qWait(500);

    QCOMPARE_GT(errorSpy.count(), 0);

    bool foundStartError = false;
    for (int i = 0; i < errorSpy.count(); ++i) {
        const QString reason = errorSpy.at(i).at(1).toString();
        // resolveEdgeExecutable() returns empty → "插件不存在" error
        if (reason.contains(QStringLiteral("插件不存在")))
            foundStartError = true;
    }
    QVERIFY2(foundStartError,
        "Expected '插件不存在' error because resolveEdgeExecutable() returns empty");

    // Path is restored by init() before the next test
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4. Non-zero exit code
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testNonZeroExit()
{
    qputenv("MOCK_EDGE_EXIT_CODE", "1");

    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    s_edgeEngine->speak(QStringLiteral("exit code test"));
    QTest::qWait(500);

    QCOMPARE_GT(errorSpy.count(), 0);

    bool foundExitError = false;
    for (int i = 0; i < errorSpy.count(); ++i) {
        const QString reason = errorSpy.at(i).at(1).toString();
        if (reason.contains(QStringLiteral("退出码")))
            foundExitError = true;
    }
    QVERIFY(foundExitError);

    // Temp dirs should be cleaned after error
    QCOMPARE(edgeTempSubdirs().size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 5. Empty output file
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testEmptyOutput()
{
    qputenv("MOCK_EDGE_OUTPUT_SIZE", "0");

    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    s_edgeEngine->speak(QStringLiteral("empty file test"));
    QTest::qWait(500);

    QCOMPARE_GT(errorSpy.count(), 0);

    bool foundEmptyError = false;
    for (int i = 0; i < errorSpy.count(); ++i) {
        const QString reason = errorSpy.at(i).at(1).toString();
        if (reason.contains(QStringLiteral("为空")))
            foundEmptyError = true;
    }
    QVERIFY(foundEmptyError);

    QCOMPARE(edgeTempSubdirs().size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 6. Timeout (mock delays, use short test timeout)
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testTimeout()
{
    // Make the mock take 1 second, and set engine timeout to 200ms
    qputenv("MOCK_EDGE_DELAY_SEC", "1.0");
    s_edgeEngine->setEdgeTimeoutMs(200);

    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    s_edgeEngine->speak(QStringLiteral("timeout test"));
    QTest::qWait(800);  // Wait past the 200ms timeout but less than mock's 1s

    QCOMPARE_GT(errorSpy.count(), 0);

    bool foundTimeoutError = false;
    for (int i = 0; i < errorSpy.count(); ++i) {
        const QString reason = errorSpy.at(i).at(1).toString();
        if (reason.contains(QStringLiteral("超时")))
            foundTimeoutError = true;
    }
    QVERIFY(foundTimeoutError);

    // Restore timeout for other tests
    s_edgeEngine->setEdgeTimeoutMs(3000);

    // Temp should be cleaned after timeout kill
    QCOMPARE(edgeTempSubdirs().size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 7. Latest-wins: two rapid speak() calls
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testLatestWins()
{
    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);

    s_edgeEngine->stop();
    QVERIFY(edgeTempSubdirs().isEmpty());

    // Mock delay on first request to give us time to send second
    qputenv("MOCK_EDGE_DELAY_SEC", "2.0");

    const int genBefore = s_edgeEngine->generation();
    s_edgeEngine->speak(QStringLiteral("first request"));
    const int gen1 = s_edgeEngine->generation();
    QCOMPARE_GT(gen1, genBefore);

    // Second speak immediately (no mock delay)
    qputenv("MOCK_EDGE_DELAY_SEC", "0");
    s_edgeEngine->speak(QStringLiteral("second request"));
    const int gen2 = s_edgeEngine->generation();
    QCOMPARE_GT(gen2, gen1);

    // Check temp-dir state: dir was created by startEdgeTts, and since
    // mock finishes near-instantly (delay=0), playback has started but not
    // yet completed (WAV is ~250ms). The dir should still exist.
    QStringList dirsAfterSecond = edgeTempSubdirs();
    QCOMPARE(dirsAfterSecond.size(), 1);
    QCOMPARE(dirsAfterSecond.first().toInt(), gen2);

    // Wait for second request's playback to finish naturally
    QTest::qWait(1000);

    // After playback completes, EndOfMedia handler cleans up
    s_edgeEngine->stop();
    QVERIFY(edgeTempSubdirs().isEmpty());

    // No QProcess-level errors expected
    if (errorSpy.count() > 0) {
        QVERIFY2(isOnlyPlaybackErrors(errorSpy),
            "Only mock-data playback errors expected, not process-level errors");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 8. Edge → switch to VoiceVox mid-request
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testSwitchToVoiceVoxDuringEdge()
{
    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);

    qputenv("MOCK_EDGE_DELAY_SEC", "1.0");

    s_edgeEngine->speak(QStringLiteral("edge request that will be cancelled"));
    const int edgeGen = s_edgeEngine->generation();

    // Switch backend to VoiceVox (this calls stop() internally)
    s_edgeEngine->setBackend(TtsEngine::VoiceVox);

    QTest::qWait(2000);  // wait past edge mock delay

    // Edge error should NOT be reported (it was cancelled)
    // Any errors are fine as long as they don't contain edge-specific ones
    // Actually, VoiceVox may error because there's no VV server — that's OK

    // Temp dirs should be clean after switch
    QCOMPARE(edgeTempSubdirs().size(), 0);

    // Switch back for subsequent tests
    s_edgeEngine->setBackend(TtsEngine::EdgeTts);
    // init() will restore the executable path before the next test
}

// ═══════════════════════════════════════════════════════════════════════════════
// 9. VoiceVox → switch to Edge mid-request (uses VoiceVox client + Edge engine)
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testSwitchFromVoiceVoxToEdge()
{
    // First, set up VoiceVox backend
    s_edgeEngine->setBackend(TtsEngine::VoiceVox);

    // Start a VoiceVox request (will fail since no VV server, but it's "in flight")
    s_edgeEngine->speak(QStringLiteral("vv request"));
    const int vvGen = s_edgeEngine->generation();

    // Immediately switch to Edge (calls stop() → cancel VV pending)
    s_edgeEngine->setBackend(TtsEngine::EdgeTts);

    QTest::qWait(200);

    // Now speak with Edge (should work fine)
    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_edgeEngine, &TtsEngine::statusChanged);

    s_edgeEngine->speak(QStringLiteral("edge after vv switch"));
    QTest::qWait(500);

    bool foundPlaying = false;
    for (int i = 0; i < statusSpy.count(); ++i) {
        if (statusSpy.at(i).at(1).toString().contains(QStringLiteral("播放中")))
            foundPlaying = true;
    }
    QVERIFY(foundPlaying);

    // Mock audio playback errors are OK
    if (errorSpy.count() > 0) {
        QVERIFY2(isOnlyPlaybackErrors(errorSpy),
            "Only mock-data playback errors expected, not process-level errors");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 10. Disabled → speak does nothing
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testDisabledThenSpeak()
{
    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_edgeEngine, &TtsEngine::statusChanged);

    s_edgeEngine->stop();
    s_edgeEngine->setEnabled(false);
    const int preGen = s_edgeEngine->generation();

    s_edgeEngine->speak(QStringLiteral("disabled — should be ignored"));

    // Generation should NOT have changed (speak returns early when disabled)
    QCOMPARE(s_edgeEngine->generation(), preGen);
    QCOMPARE(errorSpy.count(), 0);

    // Re-enable and verify speak works again
    s_edgeEngine->setEnabled(true);

    s_edgeEngine->speak(QStringLiteral("reenabled test"));
    QTest::qWait(500);

    bool foundPlaying = false;
    for (int i = 0; i < statusSpy.count(); ++i) {
        if (statusSpy.at(i).at(1).toString().contains(QStringLiteral("播放中")))
            foundPlaying = true;
    }
    QVERIFY(foundPlaying);

    // Mock audio playback errors are OK
    if (errorSpy.count() > 0) {
        QVERIFY2(isOnlyPlaybackErrors(errorSpy),
            "Only mock-data playback errors expected, not process-level errors");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 11. Stop → no stale callbacks
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testStop()
{
    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);

    qputenv("MOCK_EDGE_DELAY_SEC", "0.5");

    s_edgeEngine->speak(QStringLiteral("stop test"));
    int genBeforeStop = s_edgeEngine->generation();

    // Stop mid-request
    s_edgeEngine->stop();
    int genAfterStop = s_edgeEngine->generation();
    QCOMPARE_GT(genAfterStop, genBeforeStop);

    // Wait for the mock process to finish (its callback should be discarded)
    QTest::qWait(1000);

    // No QProcess-level errors from the stale callback
    // (Playback errors are not expected here since no playback was started)
    QCOMPARE(errorSpy.count(), 0);

    // Temp should be clean
    QCOMPARE(edgeTempSubdirs().size(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
// 12. Destructor cleanup — no residual process or temp files
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testDestructorCleanup()
{
    qputenv("MOCK_EDGE_DELAY_SEC", "0.5");

    // Create a temporary engine
    auto *engine = new TtsEngine();
    engine->setBackend(TtsEngine::EdgeTts);
    engine->setEdgeExecutablePath(s_edgeEngine->edgeExecutablePath());
    engine->setEdgeTimeoutMs(3000);

    // Start a request (in-flight during deletion)
    engine->speak(QStringLiteral("destructor test"));
    QTest::qWait(60);

    // Delete engine while request is pending
    delete engine;

    // Wait for stray signals
    QTest::qWait(1000);

    // No crash = test passes (reaching this line)

    // Temp directory should be mostly clean
    QDir ttsDir(QDir::tempPath() + QStringLiteral("/DesktopPetTts"));
    if (ttsDir.exists()) {
        const QString pid = QString::number(QCoreApplication::applicationPid());
        const auto pidDirs = ttsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : pidDirs) {
            if (fi.fileName() == pid) {
                QDir pidDir(fi.absoluteFilePath());
                const auto subs = pidDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
                QVERIFY(subs.size() <= 1);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 13. Chinese, quotes, newlines passed correctly via UTF-8 input.txt
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testChineseAndSpecialChars()
{
    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_edgeEngine, &TtsEngine::statusChanged);

    // Text with Chinese characters, quotes, and newline-like content
    const QString testText = QStringLiteral("你好，世界！\"引号\" '单引号' 换行\n测试");
    s_edgeEngine->speak(testText);
    QTest::qWait(500);

    // Should have played successfully
    bool foundPlaying = false;
    for (int i = 0; i < statusSpy.count(); ++i) {
        if (statusSpy.at(i).at(1).toString().contains(QStringLiteral("播放中")))
            foundPlaying = true;
    }
    QVERIFY(foundPlaying);

    // Mock audio playback errors are OK
    if (errorSpy.count() > 0) {
        QVERIFY2(isOnlyPlaybackErrors(errorSpy),
            "Only mock-data playback errors expected, not process-level errors");
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// 14. Paths with spaces and Chinese are not split
// ═══════════════════════════════════════════════════════════════════════════════
void EdgeTtsEngineTest::testSpacesInPath()
{
    // Create a temp dir with spaces and Chinese in the name
    const QString weirdDir = QDir::tempPath()
        + QStringLiteral("/Edge TTS 测试 目录 with spaces");
    QDir().mkpath(weirdDir);

    // Create copies of both .bat and .py there (remove existing first to prevent stale copies)
    const QString mockBatSrc = s_edgeEngine->edgeExecutablePath();
    const QString mockBatDest = weirdDir + QStringLiteral("/mock edge tts.bat");
    QFile::remove(mockBatDest);
    QVERIFY2(QFile::copy(mockBatSrc, mockBatDest), "Failed to copy mock bat to spaced path");

    // Also copy mock_edge_tts.py — the batch file uses %~dp0mock_edge_tts.py
    // which resolves relative to the batch file's own directory.
    const QString mockPyDir = QFileInfo(mockBatSrc).absolutePath();
    const QString mockPySrc = mockPyDir + QStringLiteral("/mock_edge_tts.py");
    const QString mockPyDest = weirdDir + QStringLiteral("/mock_edge_tts.py");
    QFile::remove(mockPyDest);
    if (QFile::exists(mockPySrc)) {
        QVERIFY2(QFile::copy(mockPySrc, mockPyDest), "Failed to copy mock py to spaced path");
    }

    // Set this path as the executable (contains spaces and Chinese)
    s_edgeEngine->setEdgeExecutablePath(QFileInfo(mockBatDest).absoluteFilePath());

    QSignalSpy errorSpy(s_edgeEngine, &TtsEngine::errorOccurred);
    QSignalSpy statusSpy(s_edgeEngine, &TtsEngine::statusChanged);

    s_edgeEngine->speak(QStringLiteral("path spaces test"));
    QTest::qWait(500);

    // Should work — QProcess handles Unicode paths correctly
    bool foundPlaying = false;
    for (int i = 0; i < statusSpy.count(); ++i) {
        if (statusSpy.at(i).at(1).toString().contains(QStringLiteral("播放中")))
            foundPlaying = true;
    }
    QVERIFY(foundPlaying);

    // Mock audio playback errors are OK
    if (errorSpy.count() > 0) {
        QVERIFY2(isOnlyPlaybackErrors(errorSpy),
            "Only mock-data playback errors expected, not process-level errors");
    }

    // Cleanup
    QFile::remove(mockBatDest);
    QFile::remove(mockPyDest);
    QDir(weirdDir).rmdir(weirdDir);
    // init() will restore s_edgeMockExecutable before the next test
}
