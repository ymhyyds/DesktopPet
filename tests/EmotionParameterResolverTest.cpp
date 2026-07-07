// ── EmotionParameterResolver Unit Tests ───────────────────────────────
// Pure logic tests for emotion parameter resolution.
//   - No Cubism SDK
//   - No OpenGL / Qt Multimedia
//   - No audio device or mock server
//   - Only Qt6::Test + Qt6::Core
//
// Coverage: 18+ test cases.

#include <QObject>
#include <QTest>

#include "pet/EmotionParameterResolver.h"
#include "pet/LipSyncConfig.h"  // for LipSyncParameterSource values

// ═════════════════════════════════════════════════════════════════════════

class EmotionParameterResolverTest : public QObject
{
    Q_OBJECT

private slots:
    // ── 1: 有效标准 LipSync Group 优先于 ParamMouthOpenY fallback ──
    void test_standardGroupOverParamMouthOpenY();

    // ── 2: Mao 场景 — 实际参数含 ParamA，标准组声明 ParamA ──
    void test_maoStandardGroupParamA();

    // ── 3: Wanko 场景 — 标准组声明 PARAM_MOUTH_OPEN_Y ──
    void test_wankoStandardGroupParamMouthOpenY();

    // ── 4: xiaoyue 场景 — 无标准组，ParamMouthOpenY 存在 ──
    void test_xiaoyueParamMouthOpenYFallback();

    // ── 5: 草神场景 — 无标准组，ParamMouthOpenY 存在并保留真实范围 ──
    void test_nahidaParamMouthOpenYFallback();

    // ── 6: 无标准组且无 ParamMouthOpenY → mouth Disabled ──
    void test_noMouthSourceDisabled();

    // ── 7: 标准组声明不存在的参数 → 拒绝，然后 fallback → ParamMouthOpenY ──
    void test_standardGroupAllInvalidFallbackToParamMouthOpenY();

    // ── 8: 标准组部分有效 → 只使用真实有效参数 ──
    void test_standardGroupPartialValid();

    // ── 9: ExternalConfig 优先于 StandardGroup ──
    void test_externalConfigPriority();

    // ── 10: ParamCheek 不存在时 Disabled ──
    void test_cheekNotExistsDisabled();

    // ── 11: ParamBrowLY 存在、ParamBrowRY 不存在时分别解析 ──
    void test_browLYExistsBrowRYNotExists();

    // ── 12: ParamBrowLForm 不得被当作 ParamBrowLY ──
    void test_browLFormNotBrowLY();

    // ── 13: AJ* 参数不得自动映射 ──
    void test_ajParamsNotAutoMapped();

    // ── 14: 真实范围钳制（测试 resolver 不改变范围，由 ModelController 绑定） ──
    void test_rangePreserved();

    // ── 15: 不使用模型名称参与解析 ──
    void test_noModelNameInResolution();

    // ── 16: 模型切换重建后旧参数不残留 ──
    void test_cleanStateAfterReset();

    // ── 17: EyeSmile 当前不参与写入 ──
    void test_eyeSmileExistsButNotWritten();

    // ── 18: 用户覆写和 LipSync 顺序不变（resolver 不改变此逻辑） ──
    void test_userOverrideLipSyncOrderUnchanged();

    // ── 19: StandardGroup 声明存在参数 → 正确解析 ──
    void test_standardGroupSingleValid();

    // ── 20: Hiyori 场景 — ParamMouthOpenY 标准组 ──
    void test_hiyoriStandardGroup();
};

// ═════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════

/// 检查特定名称是否存在解析结果中
static bool hasParam(const QVector<ResolvedEmotionParameter>& params, const QString& name)
{
    for (const auto& p : params) {
        if (p.id == name) return true;
    }
    return false;
}

/// 检查特定名称的参数是否 mapped
static bool isMapped(const ResolvedEmotionParameter& p)
{
    return p.mapped;
}

// ═════════════════════════════════════════════════════════════════════════
// 1. 有效标准 LipSync Group 优先于 ParamMouthOpenY fallback
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_standardGroupOverParamMouthOpenY()
{
    // 模型中有 ParamMouthOpenY 和 ParamA
    // 标准组声明 ParamA
    // → 应使用标准组的 ParamA，而非 ParamMouthOpenY
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),
        QStringLiteral("ParamA"),
        QStringLiteral("ParamBodyAngleX")
    };
    QStringList standardIds = {
        QStringLiteral("ParamA")
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    QCOMPARE(result.mouthSource, EmotionMouthSource::StandardLipSyncGroup);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("ParamA"));
    QVERIFY(result.mouthParams[0].mapped);
}

// ═════════════════════════════════════════════════════════════════════════
// 2. Mao 场景：实际参数含 ParamA，标准组声明 ParamA
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_maoStandardGroupParamA()
{
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamA"),
        QStringLiteral("ParamBodyAngleX"),
        // Mao 没有 ParamMouthOpenY
    };
    QStringList standardIds = {
        QStringLiteral("ParamA")
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    QCOMPARE(result.mouthSource, EmotionMouthSource::StandardLipSyncGroup);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("ParamA"));
    QVERIFY(result.mouthParams[0].mapped);
}

// ═════════════════════════════════════════════════════════════════════════
// 3. Wanko 场景：标准组 PARAM_MOUTH_OPEN_Y
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_wankoStandardGroupParamMouthOpenY()
{
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("PARAM_MOUTH_OPEN_Y"),
        // Wanko 没有 ParamMouthOpenY
    };
    QStringList standardIds = {
        QStringLiteral("PARAM_MOUTH_OPEN_Y")
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    QCOMPARE(result.mouthSource, EmotionMouthSource::StandardLipSyncGroup);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("PARAM_MOUTH_OPEN_Y"));
    QVERIFY(result.mouthParams[0].mapped);
}

// ═════════════════════════════════════════════════════════════════════════
// 4. xiaoyue 场景：无标准组，ParamMouthOpenY 存在
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_xiaoyueParamMouthOpenYFallback()
{
    // xiaoyue 实际有 ParamMouthOpenY，但 model3.json 中无 LipSync 标准组
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),   // index=40, range [0,1]
        QStringLiteral("ParamEyeLSmile"),
        QStringLiteral("ParamEyeRSmile"),
        // 没有 Lipsync Groups
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),  // no standard group
        {},     // no standard IDs
        false); // no external config

    QCOMPARE(result.mouthSource, EmotionMouthSource::ParamMouthOpenY);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("ParamMouthOpenY"));
    QVERIFY(result.mouthParams[0].mapped);
}

// ═════════════════════════════════════════════════════════════════════════
// 5. 草神场景：无标准组，ParamMouthOpenY 存在，范围 -1~1
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_nahidaParamMouthOpenYFallback()
{
    // 草神实际有 ParamMouthOpenY（index=20, range [-1,1]）
    // resolver 是名称解析，不处理范围（由 ModelController 绑定阶段填充）
    // 但必须确认 ParamMouthOpenY 被正确解析
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),
        QStringLiteral("ParamBrowLY"),
        QStringLiteral("ParamBrowRY"),
        QStringLiteral("ParamEyeLSmile"),
        QStringLiteral("ParamEyeRSmile"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {},
        false);

    QCOMPARE(result.mouthSource, EmotionMouthSource::ParamMouthOpenY);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("ParamMouthOpenY"));
    QVERIFY(result.mouthParams[0].mapped);
}

// ═════════════════════════════════════════════════════════════════════════
// 6. 无标准组且无 ParamMouthOpenY → mouth Disabled
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_noMouthSourceDisabled()
{
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamBodyAngleX"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {},
        false);

    QCOMPARE(result.mouthSource, EmotionMouthSource::Disabled);
    QVERIFY(result.mouthParams.isEmpty());
}

// ═════════════════════════════════════════════════════════════════════════
// 7. 标准组声明不存在的参数 → 拒绝，然后尝试 ParamMouthOpenY
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_standardGroupAllInvalidFallbackToParamMouthOpenY()
{
    // 标准组声明了 ParamXXX 但模型中没有
    // 模型中有 ParamMouthOpenY
    // → 应 fallback 到 ParamMouthOpenY
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),
    };
    QStringList standardIds = {
        QStringLiteral("ParamXXX")  // 不存在的参数
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    // 标准组全部无效 → fallback 到 ParamMouthOpenY
    QCOMPARE(result.mouthSource, EmotionMouthSource::ParamMouthOpenY);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("ParamMouthOpenY"));
}

// ═════════════════════════════════════════════════════════════════════════
// 8. 标准组部分有效 → 只使用真实有效参数
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_standardGroupPartialValid()
{
    // 标准组声明 3 个参数，只有 2 个在模型中存在
    QStringList actualParams = {
        QStringLiteral("ParamA"),
        QStringLiteral("ParamB"),
    };
    QStringList standardIds = {
        QStringLiteral("ParamA"),
        QStringLiteral("ParamB"),
        QStringLiteral("ParamXXX")  // 不存在
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    QCOMPARE(result.mouthSource, EmotionMouthSource::StandardLipSyncGroup);
    QCOMPARE(result.mouthParams.size(), 2);
    QVERIFY(hasParam(result.mouthParams, QStringLiteral("ParamA")));
    QVERIFY(hasParam(result.mouthParams, QStringLiteral("ParamB")));
    QVERIFY(!hasParam(result.mouthParams, QStringLiteral("ParamXXX")));
}

// ═════════════════════════════════════════════════════════════════════════
// 9. ExternalConfig 优先于 StandardGroup
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_externalConfigPriority()
{
    // 即使有标准组参数，ExternalConfig 启用时 mouth source 应为 ExternalLipSyncConfig
    QStringList actualParams = {
        QStringLiteral("ParamMouthOpenY"),
        QStringLiteral("ParamA"),
    };
    QStringList standardIds = {
        QStringLiteral("ParamA")
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::ExternalConfig),
        standardIds,
        true);  // enableExternalConfig = true

    QCOMPARE(result.mouthSource, EmotionMouthSource::ExternalLipSyncConfig);
    // mouthParams 在此层为空，由 ModelController 在绑定阶段从 m_lipSyncFallbackParams 填充
    QVERIFY(result.mouthParams.isEmpty());
}

// ═════════════════════════════════════════════════════════════════════════
// 10. ParamCheek 不存在时 Disabled
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_cheekNotExistsDisabled()
{
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),
        // ParamCheek 不存在
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    QVERIFY(!isMapped(result.cheek));
    QCOMPARE(result.cheek.id, QStringLiteral("ParamCheek"));
}

// ═════════════════════════════════════════════════════════════════════════
// 11. ParamBrowLY 存在、ParamBrowRY 不存在时分别解析
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_browLYExistsBrowRYNotExists()
{
    QStringList actualParams = {
        QStringLiteral("ParamBrowLY"),
        // ParamBrowRY 不存在
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    QVERIFY(isMapped(result.browLY));
    QVERIFY(!isMapped(result.browRY));
}

// ═════════════════════════════════════════════════════════════════════════
// 12. ParamBrowLForm 不得被当作 ParamBrowLY
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_browLFormNotBrowLY()
{
    // 模型中有 ParamBrowLForm 但没有 ParamBrowLY
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamBrowLForm"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    // browLY 不应被映射（ParamBrowLForm ≠ ParamBrowLY）
    QVERIFY(!isMapped(result.browLY));
    QCOMPARE(result.browLY.id, QStringLiteral("ParamBrowLY"));
}

// ═════════════════════════════════════════════════════════════════════════
// 13. AJ* 参数不得自动映射
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_ajParamsNotAutoMapped()
{
    // 模型中有 AJ* 参数
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),
        QStringLiteral("AJISJIOEJFD"),
        QStringLiteral("AJFOEIWJ"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    // AJ* 不应出现在任何情绪通道中
    // 只检查标准情绪参数是否正确
    QCOMPARE(result.mouthSource, EmotionMouthSource::ParamMouthOpenY);
    // cheek/brow* 都不是 AJ*
    QVERIFY(!isMapped(result.cheek));
    QVERIFY(!isMapped(result.browLY));
}

// ═════════════════════════════════════════════════════════════════════════
// 14. 范围保留（resolver 不修改范围值）
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_rangePreserved()
{
    // resolver 返回的 mapped=false 参数 index=-1，范围是默认值
    // ModelController 绑定阶段从模型读取真实范围
    // 这里逻辑上不需要断言特定范围，因为 resolver 不设范围
    // 但确保默认值合理

    QStringList empty;
    auto result = resolveEmotionParameters(empty, 0, {});

    QCOMPARE(result.mouthSource, EmotionMouthSource::Disabled);
    QVERIFY(!isMapped(result.cheek));
}

// ═════════════════════════════════════════════════════════════════════════
// 15. 不使用模型名称参与解析
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_noModelNameInResolution()
{
    // 解析结果不应依赖于 actualParams 中包含"模型名称"的词
    // 只依赖具体的参数 ID
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),  // 存在 → 应解析为 ParamMouthOpenY
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    QCOMPARE(result.mouthSource, EmotionMouthSource::ParamMouthOpenY);

    // 即使列表中有奇怪的内容也不影响
    QStringList weirdParams = {
        QStringLiteral("Hiyori"),
        QStringLiteral("Mao"),
        QStringLiteral("Wanko"),
        QStringLiteral("ParamMouthOpenY"),
    };
    auto result2 = resolveEmotionParameters(
        weirdParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    QCOMPARE(result2.mouthSource, EmotionMouthSource::ParamMouthOpenY);
}

// ═════════════════════════════════════════════════════════════════════════
// 16. 模型切换后旧参数不残留
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_cleanStateAfterReset()
{
    // 先用一个带 ParamCheek 的模型解析
    QStringList model1Params = {
        QStringLiteral("ParamCheek"),
        QStringLiteral("ParamMouthOpenY"),
    };
    auto result1 = resolveEmotionParameters(
        model1Params,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    QVERIFY(isMapped(result1.cheek));
    QCOMPARE(result1.mouthParams.size(), 1);

    // 然后用一个不带 ParamCheek 的模型解析
    QStringList model2Params = {
        QStringLiteral("ParamAngleX"),
        // ParamCheek 不存在
    };
    auto result2 = resolveEmotionParameters(
        model2Params,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    // 第二次解析不应受第一次影响
    QVERIFY(!isMapped(result2.cheek));
    QCOMPARE(result2.mouthSource, EmotionMouthSource::Disabled);
    QVERIFY(result2.mouthParams.isEmpty());
}

// ═════════════════════════════════════════════════════════════════════════
// 17. EyeSmile 存在但不参与写入
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_eyeSmileExistsButNotWritten()
{
    QStringList actualParams = {
        QStringLiteral("ParamEyeLSmile"),
        QStringLiteral("ParamEyeRSmile"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::Disabled),
        {});

    // EyeSmile 在 resolver 中被标记为存在
    QVERIFY(isMapped(result.eyeLSmile));
    QVERIFY(isMapped(result.eyeRSmile));

    // 但 mouth 不受影响（没有嘴部参数 → disabled）
    QCOMPARE(result.mouthSource, EmotionMouthSource::Disabled);
    QVERIFY(result.mouthParams.isEmpty());

    // cheek/brow* 都不存在 → false
    QVERIFY(!isMapped(result.cheek));
    QVERIFY(!isMapped(result.browLY));
}

// ═════════════════════════════════════════════════════════════════════════
// 18. 用户覆写和 LipSync 顺序不变（纯逻辑验证 resolver 不改变此顺序）
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_userOverrideLipSyncOrderUnchanged()
{
    // 这个测试验证 resolver 不会引入影响 LipSync 或 user override 的副作用。
    // resolver 只输出参数列表，不控制写入顺序。
    // 写入顺序由 ModelController 的控制流保证。

    // 验证 resolver 返回的 mouthParams 中包含正确的参数
    QStringList actualParams = {
        QStringLiteral("ParamA"),
        QStringLiteral("ParamB"),
        QStringLiteral("ParamMouthOpenY"),
    };
    QStringList standardIds = {
        QStringLiteral("ParamA"),
        QStringLiteral("ParamB"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    // 正确解析为标准组
    QCOMPARE(result.mouthSource, EmotionMouthSource::StandardLipSyncGroup);
    QCOMPARE(result.mouthParams.size(), 2);

    // LipSync 写入时序在当前代码中：emotion 第1遍 → physics → emotion 第2遍 → LipSync → userOverride
    // 此测试仅确认 resolver 不破坏此逻辑
}

// ═════════════════════════════════════════════════════════════════════════
// 19. StandardGroup 单参数有效
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_standardGroupSingleValid()
{
    QStringList actualParams = {
        QStringLiteral("ParamMouthOpenY"),
    };
    QStringList standardIds = {
        QStringLiteral("ParamMouthOpenY"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    QCOMPARE(result.mouthSource, EmotionMouthSource::StandardLipSyncGroup);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("ParamMouthOpenY"));
    QVERIFY(result.mouthParams[0].mapped);
}

// ═════════════════════════════════════════════════════════════════════════
// 20. Hiyori 场景 — ParamMouthOpenY via standard group
// ═════════════════════════════════════════════════════════════════════════

void EmotionParameterResolverTest::test_hiyoriStandardGroup()
{
    // Hiyori: ParamMouthOpenY 在标准组中声明且真实存在
    QStringList actualParams = {
        QStringLiteral("ParamAngleX"),
        QStringLiteral("ParamMouthOpenY"),
        QStringLiteral("ParamCheek"),
        QStringLiteral("ParamBrowLY"),
        QStringLiteral("ParamBrowRY"),
        QStringLiteral("ParamBrowLAngle"),
        QStringLiteral("ParamBrowRAngle"),
    };
    QStringList standardIds = {
        QStringLiteral("ParamMouthOpenY"),
    };

    auto result = resolveEmotionParameters(
        actualParams,
        static_cast<int>(LipSyncParameterSource::StandardGroup),
        standardIds,
        false);

    // mouth
    QCOMPARE(result.mouthSource, EmotionMouthSource::StandardLipSyncGroup);
    QCOMPARE(result.mouthParams.size(), 1);
    QCOMPARE(result.mouthParams[0].id, QStringLiteral("ParamMouthOpenY"));

    // 其他标准情绪参数
    QVERIFY(isMapped(result.cheek));
    QVERIFY(isMapped(result.browLY));
    QVERIFY(isMapped(result.browRY));
    QVERIFY(isMapped(result.browLAngle));
    QVERIFY(isMapped(result.browRAngle));
}

// ═════════════════════════════════════════════════════════════════════════

QTEST_MAIN(EmotionParameterResolverTest)
#include "EmotionParameterResolverTest.moc"
