// ── LipSync Fallback Config unit tests ─────────────────────────────
// Pure logic tests for LipSyncConfig parsing, mapping, and source selection.
//   - No Cubism SDK
//   - No OpenGL / Qt Multimedia
//   - No audio device or mock server
//   - Only Qt6::Test + Qt6::Core
//
// Coverage: 41+ test cases matching the spec section 9.

#include <QObject>
#include <QTest>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <limits>

#include "pet/LipSyncConfig.h"

// ═════════════════════════════════════════════════════════════════════════

class LipSyncFallbackTest : public QObject
{
    Q_OBJECT

private slots:
    // ── 1-2: 有效单/多参数配置 ──
    void test_validSingleParameter();
    void test_validMultipleParameters();

    // ── 3-4: 映射 ──
    void test_mapForward();
    void test_mapInverted();

    // ── 5-8: weight 缩放与钳制 ──
    void test_mapWeightLessThanOne();
    void test_mapWeightGreaterThanOneClamped();
    void test_mapEnvelopeZero();
    void test_mapEnvelopeOne();

    // ── 9-10: envelope 边界 ──
    void test_mapEnvelopeBelowZero();
    void test_mapEnvelopeAboveOne();

    // ── 11-12: 路径推导 ──
    void test_unicodeModel3Path();
    void test_windowsStylePath();

    // ── 13-22: 解析错误处理 + 空/空白 ──
    void test_parseEmptyInput();
    void test_parseWhitespaceOnly();
    void test_parseInvalidJson();
    void test_parseRootNotObject();
    void test_parseMissingVersion();
    void test_parseVersionWrongType();
    void test_parseVersionUnsupported();
    void test_parseMissingParameters();
    void test_parseParametersWrongType();
    void test_parseParametersEmpty();

    // ── 23-32: 参数条目校验 ──
    void test_skipItemNotObject();
    void test_skipMissingId();
    void test_skipEmptyId();
    void test_skipMissingClosed();
    void test_skipMissingOpen();
    void test_skipMissingWeight();
    void test_skipWeightZero();
    void test_skipWeightNegative();
    void test_skipNonFiniteValue();
    void test_skipDuplicateIdKeepFirst();

    // ── 33-34: 混合有效/无效 ──
    void test_partialValid();
    void test_allParamsInvalid();

    // ── 35-38: Source selection ──
    void test_standardGroupValid();
    void test_standardGroupPartialValid();
    void test_standardGroupAllInvalid();
    void test_standardAndFallbackNotBoth();

    // ── 39-42: Speaking/write logic ──
    void test_speakingTrueEnvelopeZeroWritesClosed();
    void test_speakingFalseEnvelopeZeroNoWrite();
    void test_speakingFalseEnvelopeDecayStillWrites();
    void test_lipSyncShouldWriteEdgeCases();

    // ── LipSyncConfig struct defaults ──
    void test_configStructDefaults();

    // ── 43: 参数存在性查找 ──
    void test_parameterExistenceLookup();

private:
    QByteArray makeConfig(const QJsonArray& params, int version = 1);
};


// ═════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════

QByteArray LipSyncFallbackTest::makeConfig(const QJsonArray& params, int version)
{
    QJsonObject root;
    root[QStringLiteral("version")] = version;
    root[QStringLiteral("parameters")] = params;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// ═════════════════════════════════════════════════════════════════════════
// 1. 有效单参数配置
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_validSingleParameter()
{
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
    p[QStringLiteral("closed")] = 0.0;
    p[QStringLiteral("open")]   = 1.0;
    p[QStringLiteral("weight")] = 1.0;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.version, 1);
    QCOMPARE(cfg.parameters.size(), 1);
    QCOMPARE(cfg.parameters[0].id, QStringLiteral("ParamMouthOpenY"));
    QCOMPARE(cfg.parameters[0].closed, 0.0);
    QCOMPARE(cfg.parameters[0].open, 1.0);
    QCOMPARE(cfg.parameters[0].weight, 1.0);
}

// ═════════════════════════════════════════════════════════════════════════
// 2. 有效多参数配置
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_validMultipleParameters()
{
    QJsonArray params;
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
        p[QStringLiteral("closed")] = 0.0;
        p[QStringLiteral("open")]   = 1.0;
        params.append(p);
    }
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamA");
        p[QStringLiteral("closed")] = 0.5;
        p[QStringLiteral("open")]   = 0.8;
        p[QStringLiteral("weight")] = 0.7;
        params.append(p);
    }

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 2);
    QCOMPARE(cfg.parameters[0].id, QStringLiteral("ParamMouthOpenY"));
    QCOMPARE(cfg.parameters[1].id, QStringLiteral("ParamA"));
    QCOMPARE(cfg.parameters[1].weight, 0.7);
}

// ═════════════════════════════════════════════════════════════════════════
// 3. 正向映射（closed < open）
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapForward()
{
    LipSyncParameterConfig cfg;
    cfg.id     = QStringLiteral("ParamMouthOpenY");
    cfg.closed = 0.0;
    cfg.open   = 1.0;
    cfg.weight = 1.0;

    QCOMPARE(mapLipSyncEnvelope(0.0f, cfg), 0.0f);
    QCOMPARE(mapLipSyncEnvelope(0.5f, cfg), 0.5f);
    QCOMPARE(mapLipSyncEnvelope(1.0f, cfg), 1.0f);

    // Non-zero closed
    cfg.closed = 0.2;
    cfg.open   = 0.8;
    QCOMPARE(mapLipSyncEnvelope(0.0f, cfg), 0.2f);
    QCOMPARE(mapLipSyncEnvelope(0.5f, cfg), 0.5f);  // 0.2 + 0.5*(0.8-0.2) = 0.5
    QCOMPARE(mapLipSyncEnvelope(1.0f, cfg), 0.8f);
}

// ═════════════════════════════════════════════════════════════════════════
// 4. 反向映射（closed > open → natural inverted)
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapInverted()
{
    LipSyncParameterConfig cfg;
    cfg.id     = QStringLiteral("ParamMouthForm");
    cfg.closed = 1.0;
    cfg.open   = 0.0;
    cfg.weight = 1.0;

    QCOMPARE(mapLipSyncEnvelope(0.0f, cfg), 1.0f);
    QCOMPARE(mapLipSyncEnvelope(0.5f, cfg), 0.5f);
    QCOMPARE(mapLipSyncEnvelope(1.0f, cfg), 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════
// 5. weight < 1 缩放
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapWeightLessThanOne()
{
    LipSyncParameterConfig cfg;
    cfg.id     = QStringLiteral("ParamMouthOpenY");
    cfg.closed = 0.0;
    cfg.open   = 1.0;
    cfg.weight = 0.5;

    // envelope=1.0 → u = clamp(1.0*0.5, 0, 1) = 0.5 → 0.0 + 0.5*(1.0-0.0) = 0.5
    QCOMPARE(mapLipSyncEnvelope(1.0f, cfg), 0.5f);
    QCOMPARE(mapLipSyncEnvelope(0.0f, cfg), 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════
// 6. weight > 1 → clamp to 1
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapWeightGreaterThanOneClamped()
{
    LipSyncParameterConfig cfg;
    cfg.id     = QStringLiteral("ParamMouthOpenY");
    cfg.closed = 0.0;
    cfg.open   = 1.0;
    cfg.weight = 2.0;

    // envelope=0.6 → u = clamp(0.6*2.0, 0, 1) = 1.0 → value = 1.0
    QCOMPARE(mapLipSyncEnvelope(0.6f, cfg), 1.0f);
    // envelope=0.3 → u = clamp(0.3*2.0, 0, 1) = 0.6 → value = 0.6
    QCOMPARE(mapLipSyncEnvelope(0.3f, cfg), 0.6f);
}

// ═════════════════════════════════════════════════════════════════════════
// 7. envelope = 0
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapEnvelopeZero()
{
    LipSyncParameterConfig cfg;
    cfg.id     = QStringLiteral("ParamMouthOpenY");
    cfg.closed = 0.3;
    cfg.open   = 0.9;
    cfg.weight = 1.0;
    QCOMPARE(mapLipSyncEnvelope(0.0f, cfg), 0.3f);
}

// ═════════════════════════════════════════════════════════════════════════
// 8. envelope = 1
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapEnvelopeOne()
{
    LipSyncParameterConfig cfg;
    cfg.id     = QStringLiteral("ParamMouthOpenY");
    cfg.closed = 0.3;
    cfg.open   = 0.9;
    cfg.weight = 1.0;
    QCOMPARE(mapLipSyncEnvelope(1.0f, cfg), 0.9f);
}

// ═════════════════════════════════════════════════════════════════════════
// 9. envelope < 0 → clamped to 0
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapEnvelopeBelowZero()
{
    LipSyncParameterConfig cfg;
    cfg.closed = 0.0;
    cfg.open   = 1.0;
    cfg.weight = 1.0;
    QCOMPARE(mapLipSyncEnvelope(-0.5f, cfg), 0.0f);
    QCOMPARE(mapLipSyncEnvelope(-1.0f, cfg), 0.0f);
}

// ═════════════════════════════════════════════════════════════════════════
// 10. envelope > 1 → clamped to 1
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_mapEnvelopeAboveOne()
{
    LipSyncParameterConfig cfg;
    cfg.closed = 0.0;
    cfg.open   = 1.0;
    cfg.weight = 1.0;
    QCOMPARE(mapLipSyncEnvelope(1.5f, cfg), 1.0f);
    QCOMPARE(mapLipSyncEnvelope(2.0f, cfg), 1.0f);
}

// ═════════════════════════════════════════════════════════════════════════
// 11. Unicode model3 基名路径推导
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_unicodeModel3Path()
{
    const QString path = deriveLipSyncSidecarPath(
        QStringLiteral("/characters/草神/"),
        QStringLiteral("草神"));
    QVERIFY(path.contains(QStringLiteral("草神.lipsync.json")));

    // Also test without trailing slash
    const QString path2 = deriveLipSyncSidecarPath(
        QStringLiteral("/characters/草神"),
        QStringLiteral("草神"));
    QVERIFY(path2.contains(QStringLiteral("草神/草神.lipsync.json")));
}

// ═════════════════════════════════════════════════════════════════════════
// 12. Windows 风格路径推导
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_windowsStylePath()
{
    const QString path = deriveLipSyncSidecarPath(
        QStringLiteral("C:\\characters\\Hiyori\\"),
        QStringLiteral("Hiyori"));
    QVERIFY(path.contains(QStringLiteral("Hiyori.lipsync.json")));

    const QString path2 = deriveLipSyncSidecarPath(
        QStringLiteral("C:\\characters\\Hiyori"),
        QStringLiteral("Hiyori"));
    QVERIFY(path2.contains(QStringLiteral("Hiyori/Hiyori.lipsync.json")));
}

// ═════════════════════════════════════════════════════════════════════════
// 13. 空输入（空 QByteArray → parse 返回 empty 错误）
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseEmptyInput()
{
    // 文件不存在由 QFileInfo::exists 在 caller 侧处理，不调用 parseLipSyncConfig。
    // 若 parseLipSyncConfig 被传入空数据（防御性），应返回解析失败而非静默成功。
    QString error;
    const auto cfg = parseLipSyncConfig(QByteArray(), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("empty"), Qt::CaseInsensitive));
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 14. 全空白内容（仅有空格和换行）
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseWhitespaceOnly()
{
    // Only spaces and newlines — not valid JSON content
    {
        QString error;
        const auto cfg = parseLipSyncConfig(QByteArray("   \n  \t  "), &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("empty"), Qt::CaseInsensitive));
        QCOMPARE(cfg.parameters.size(), 0);
    }

    // Multiple newlines only
    {
        QString error;
        const auto cfg = parseLipSyncConfig(QByteArray("\n\n\n\n"), &error);
        QVERIFY(!error.isEmpty());
        QVERIFY(error.contains(QStringLiteral("empty"), Qt::CaseInsensitive));
        QCOMPARE(cfg.parameters.size(), 0);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 15. JSON 损坏
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseInvalidJson()
{
    QString error;
    const auto cfg = parseLipSyncConfig(QByteArray("{invalid json}"), &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("JSON")));
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 16. 根节点不是对象
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseRootNotObject()
{
    QString error;
    const auto cfg = parseLipSyncConfig(QByteArray(R"([1,2,3])"), &error);
    QVERIFY(!error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 17. version 缺失
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseMissingVersion()
{
    QJsonObject root;
    root[QStringLiteral("parameters")] = QJsonArray();
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QString error;
    const auto cfg = parseLipSyncConfig(json, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("version")));
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 18. version 类型错误
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseVersionWrongType()
{
    QJsonObject root;
    root[QStringLiteral("version")] = QStringLiteral("1");
    root[QStringLiteral("parameters")] = QJsonArray();
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QString error;
    const auto cfg = parseLipSyncConfig(json, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("version")));
}

// ═════════════════════════════════════════════════════════════════════════
// 19. version 不支持（≠ 1）
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseVersionUnsupported()
{
    QJsonObject root;
    root[QStringLiteral("version")] = 2;
    root[QStringLiteral("parameters")] = QJsonArray();
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QString error;
    const auto cfg = parseLipSyncConfig(json, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("version")));
}

// ═════════════════════════════════════════════════════════════════════════
// 20. parameters 缺失
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseMissingParameters()
{
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QString error;
    const auto cfg = parseLipSyncConfig(json, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("parameters")));
}

// ═════════════════════════════════════════════════════════════════════════
// 21. parameters 类型错误
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseParametersWrongType()
{
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("parameters")] = QStringLiteral("not_an_array");
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QString error;
    const auto cfg = parseLipSyncConfig(json, &error);
    QVERIFY(!error.isEmpty());
    QVERIFY(error.contains(QStringLiteral("parameters")));
}

// ═════════════════════════════════════════════════════════════════════════
// 22. parameters 为空
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parseParametersEmpty()
{
    QJsonArray empty;
    const QByteArray json = makeConfig(empty);

    QString error;
    const auto cfg = parseLipSyncConfig(json, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 23. 参数项不是对象
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipItemNotObject()
{
    QJsonArray params;
    params.append(QStringLiteral("string_item"));
    QJsonObject valid;
    valid[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
    valid[QStringLiteral("closed")] = 0.0;
    valid[QStringLiteral("open")]   = 1.0;
    params.append(valid);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    // Only the valid item should be kept
    QCOMPARE(cfg.parameters.size(), 1);
    QCOMPARE(cfg.parameters[0].id, QStringLiteral("ParamMouthOpenY"));
}

// ═════════════════════════════════════════════════════════════════════════
// 24. id 缺失
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipMissingId()
{
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("closed")] = 0.0;
    p[QStringLiteral("open")]   = 1.0;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 25. id 为空
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipEmptyId()
{
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("id")]     = QStringLiteral("");
    p[QStringLiteral("closed")] = 0.0;
    p[QStringLiteral("open")]   = 1.0;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 26. closed 缺失
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipMissingClosed()
{
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("id")]   = QStringLiteral("ParamMouthOpenY");
    p[QStringLiteral("open")] = 1.0;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 27. open 缺失
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipMissingOpen()
{
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
    p[QStringLiteral("closed")] = 0.0;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 28. weight 缺失（weight 可选，默认 1.0）
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipMissingWeight()
{
    // Weight is OPTIONAL, defaults to 1.0. This should NOT skip the item.
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
    p[QStringLiteral("closed")] = 0.0;
    p[QStringLiteral("open")]   = 1.0;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 1);
    QCOMPARE(cfg.parameters[0].weight, 1.0);
}

// ═════════════════════════════════════════════════════════════════════════
// 29. weight = 0
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipWeightZero()
{
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
    p[QStringLiteral("closed")] = 0.0;
    p[QStringLiteral("open")]   = 1.0;
    p[QStringLiteral("weight")] = 0.0;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 30. weight < 0
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipWeightNegative()
{
    QJsonArray params;
    QJsonObject p;
    p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
    p[QStringLiteral("closed")] = 0.0;
    p[QStringLiteral("open")]   = 1.0;
    p[QStringLiteral("weight")] = -0.5;
    params.append(p);

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 31. 非有限数值（NaN / Inf）
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipNonFiniteValue()
{
    // NaN closed
    {
        QJsonArray params;
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
        p[QStringLiteral("closed")] = QStringLiteral("NaN");
        p[QStringLiteral("open")]   = 1.0;
        params.append(p);

        QString error;
        const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(cfg.parameters.size(), 0);
    }

    // Inf open
    {
        QJsonArray params;
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
        p[QStringLiteral("closed")] = 0.0;
        p[QStringLiteral("open")]   = QStringLiteral("Infinity");
        params.append(p);

        QString error;
        const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(cfg.parameters.size(), 0);
    }
}

// ═════════════════════════════════════════════════════════════════════════
// 32. 重复 ID 保留第一个
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_skipDuplicateIdKeepFirst()
{
    QJsonArray params;
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
        p[QStringLiteral("closed")] = 0.0;
        p[QStringLiteral("open")]   = 1.0;
        params.append(p);
    }
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
        p[QStringLiteral("closed")] = 0.5;  // different closed
        p[QStringLiteral("open")]   = 1.0;
        params.append(p);
    }

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 1);
    QCOMPARE(cfg.parameters[0].closed, 0.0); // first one kept
}

// ═════════════════════════════════════════════════════════════════════════
// 33. 部分有效部分无效
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_partialValid()
{
    QJsonArray params;
    // Invalid: missing open
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamInvalid");
        p[QStringLiteral("closed")] = 0.0;
        params.append(p);
    }
    // Valid
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamMouthOpenY");
        p[QStringLiteral("closed")] = 0.0;
        p[QStringLiteral("open")]   = 1.0;
        params.append(p);
    }
    // Invalid: weight = 0
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamZeroWeight");
        p[QStringLiteral("closed")] = 0.0;
        p[QStringLiteral("open")]   = 1.0;
        p[QStringLiteral("weight")] = 0.0;
        params.append(p);
    }

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 1);
    QCOMPARE(cfg.parameters[0].id, QStringLiteral("ParamMouthOpenY"));
}

// ═════════════════════════════════════════════════════════════════════════
// 34. 全部参数无效
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_allParamsInvalid()
{
    QJsonArray params;
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamA");
        p[QStringLiteral("closed")] = 0.0;
        // missing open → invalid
        params.append(p);
    }
    {
        QJsonObject p;
        p[QStringLiteral("id")]     = QStringLiteral("ParamB");
        p[QStringLiteral("closed")] = 0.0;
        p[QStringLiteral("open")]   = 1.0;
        p[QStringLiteral("weight")] = 0.0;  // weight <= 0 → invalid
        params.append(p);
    }

    QString error;
    const auto cfg = parseLipSyncConfig(makeConfig(params), &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(cfg.parameters.size(), 0);
}

// ═════════════════════════════════════════════════════════════════════════
// 35. 标准参数有效时选 StandardGroup
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_standardGroupValid()
{
    const auto source = selectLipSyncSource(
        2,    // declared
        2,    // valid
        true, // sidecar exists
        true  // sidecar has valid params
    );
    QCOMPARE(source, LipSyncParameterSource::StandardGroup);
}

// ═════════════════════════════════════════════════════════════════════════
// 36. 标准参数部分有效时仍选 StandardGroup
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_standardGroupPartialValid()
{
    const auto source = selectLipSyncSource(
        3,    // declared
        1,    // valid (at least one)
        true, // sidecar exists
        true  // sidecar has valid params
    );
    QCOMPARE(source, LipSyncParameterSource::StandardGroup);
}

// ═════════════════════════════════════════════════════════════════════════
// 37. 标准参数全部无效时允许 ExternalConfig
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_standardGroupAllInvalid()
{
    const auto source = selectLipSyncSource(
        2,    // declared
        0,    // valid (all invalid)
        true, // sidecar exists
        true  // sidecar has valid params
    );
    QCOMPARE(source, LipSyncParameterSource::ExternalConfig);
}

// ═════════════════════════════════════════════════════════════════════════
// 38. 标准和 fallback 不同时启用
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_standardAndFallbackNotBoth()
{
    // When standard group has valid params → StandardGroup, never ExternalConfig
    const auto s1 = selectLipSyncSource(1, 1, false, false);
    QCOMPARE(s1, LipSyncParameterSource::StandardGroup);

    const auto s2 = selectLipSyncSource(1, 1, true, true);
    QCOMPARE(s2, LipSyncParameterSource::StandardGroup);

    // When standard is all invalid → ExternalConfig if sidecar available
    const auto s3 = selectLipSyncSource(0, 0, true, true);
    QCOMPARE(s3, LipSyncParameterSource::ExternalConfig);

    // When nothing available → Disabled
    const auto s4 = selectLipSyncSource(0, 0, false, false);
    QCOMPARE(s4, LipSyncParameterSource::Disabled);
}

// ═════════════════════════════════════════════════════════════════════════
// 39. speaking=true, envelope=0 → 写入 closed 值
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_speakingTrueEnvelopeZeroWritesClosed()
{
    // lipSyncShouldWrite should return true when speaking even if envelope is 0
    QVERIFY(lipSyncShouldWrite(true, 0.0f));
    QVERIFY(lipSyncShouldWrite(true, 0.0f, 0.005f));
    QVERIFY(lipSyncShouldWrite(true, -0.001f));
}

// ═════════════════════════════════════════════════════════════════════════
// 40. speaking=false, envelope=0 → 不写入
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_speakingFalseEnvelopeZeroNoWrite()
{
    QVERIFY(!lipSyncShouldWrite(false, 0.0f));
    QVERIFY(!lipSyncShouldWrite(false, 0.0f, 0.005f));
}

// ═════════════════════════════════════════════════════════════════════════
// 41. speaking=false 但 envelope 衰减阶段仍写入
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_speakingFalseEnvelopeDecayStillWrites()
{
    QVERIFY(lipSyncShouldWrite(false, 0.1f));
    QVERIFY(lipSyncShouldWrite(false, 0.006f));
    QVERIFY(!lipSyncShouldWrite(false, 0.004f));  // below epsilon
}

// ═════════════════════════════════════════════════════════════════════════
// 42. lipSyncShouldWrite 边界情况
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_lipSyncShouldWriteEdgeCases()
{
    // Exactly at epsilon boundary
    QVERIFY(!lipSyncShouldWrite(false, 0.005f));
    QVERIFY(lipSyncShouldWrite(false, 0.0051f));
    QVERIFY(lipSyncShouldWrite(false, 0.006f));

    // Custom epsilon — strictly greater-than boundary
    QVERIFY(!lipSyncShouldWrite(false, 0.5f, 0.6f));
    QVERIFY(!lipSyncShouldWrite(false, 0.6f, 0.6f));  // == epsilon → not writing
    QVERIFY(lipSyncShouldWrite(false, 0.6001f, 0.6f)); // > epsilon → writing
    QVERIFY(lipSyncShouldWrite(false, 0.7f, 0.6f));

    // Speaking overrides everything
    QVERIFY(lipSyncShouldWrite(true, 0.0f, 1.0f));
}

// ═════════════════════════════════════════════════════════════════════════
// Config struct defaults
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_configStructDefaults()
{
    LipSyncParameterConfig p;
    QCOMPARE(p.closed, 0.0);
    QCOMPARE(p.open, 1.0);
    QCOMPARE(p.weight, 1.0);

    LipSyncConfig c;
    QCOMPARE(c.version, 1);
    QVERIFY(c.parameters.isEmpty());

    ResolvedLipSyncParam r;
    QCOMPARE(r.index, -1);
    QCOMPARE(r.closed, 0.0f);
    QCOMPARE(r.open, 1.0f);
    QCOMPARE(r.weight, 1.0f);
}

// ═════════════════════════════════════════════════════════════════════════
// 43. 参数存在性查找（纯指针比较，不依赖 Cubism SDK）
// ═════════════════════════════════════════════════════════════════════════

void LipSyncFallbackTest::test_parameterExistenceLookup()
{
    // 模拟模型实际参数：三个唯一定址的变量代表 CubismIdHandle 指针
    int paramMouthOpenY, paramMouthForm, paramCheek;
    QHash<const void*, int> actualMap;
    actualMap[&paramMouthOpenY] = 3;   // index 3
    actualMap[&paramMouthForm]  = 12;  // index 12
    actualMap[&paramCheek]      = 7;   // index 7

    // 查找存在的参数
    QCOMPARE(findParamIndexInMap(actualMap, &paramMouthOpenY), 3);
    QCOMPARE(findParamIndexInMap(actualMap, &paramMouthForm),  12);
    QCOMPARE(findParamIndexInMap(actualMap, &paramCheek),      7);

    // 查找不存在的参数 → -1
    int paramNonExistent;
    QCOMPARE(findParamIndexInMap(actualMap, &paramNonExistent), -1);

    // 空映射 → 任何查找都返回 -1
    QHash<const void*, int> emptyMap;
    QCOMPARE(findParamIndexInMap(emptyMap, &paramMouthOpenY), -1);
}

// ═════════════════════════════════════════════════════════════════════════

QTEST_MAIN(LipSyncFallbackTest)
#include "LipSyncFallbackTest.moc"
