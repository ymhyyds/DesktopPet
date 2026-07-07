#include "EmotionParameterResolver.h"

#include <QSet>

// ====================================================================
// resolveEmotionParameters
// ====================================================================

ResolvedEmotionParameters resolveEmotionParameters(
    const QStringList& actualParameterNames,
    int lipSyncSource,
    const QStringList& standardLipSyncIds,
    bool enableExternalConfig)
{
    ResolvedEmotionParameters result;

    // ── 构建实际参数集合（O(1) 查找） ──
    const QSet<QString> actualSet(actualParameterNames.begin(), actualParameterNames.end());

    auto exists = [&](const QString& name) -> bool {
        return actualSet.contains(name);
    };

    auto makeStub = [&](const QString& name) -> ResolvedEmotionParameter {
        ResolvedEmotionParameter p;
        p.id = name;
        p.mapped = exists(name);
        return p;
    };

    // ══════════════════════════════════════════════════════════
    // 1. mouth 参数来源 — 固定优先级
    // ══════════════════════════════════════════════════════════

    // Priority 1: ExternalConfig
    if (enableExternalConfig) {
        result.mouthSource = EmotionMouthSource::ExternalLipSyncConfig;
        // mouthParams 由 ModelController 在绑定阶段从 m_lipSyncFallbackParams 填充
        //（不在此填充，因为只有 ModelController 有具体 closed/open/weight 值）
    }
    // Priority 2: StandardGroup
    else if (!standardLipSyncIds.isEmpty()) {
        // 只接受在模型中真实存在的标准组参数
        QStringList validIds;
        for (const auto& id : standardLipSyncIds) {
            if (exists(id)) {
                validIds.append(id);
            } else {
                // 标准组声明了不存在的参数 → 跳过
            }
        }

        if (!validIds.isEmpty()) {
            result.mouthSource = EmotionMouthSource::StandardLipSyncGroup;
            for (const auto& id : validIds) {
                auto p = makeStub(id);
                p.isMouth = true;
                p.closed = 0.0f;
                p.open   = 1.0f;
                result.mouthParams.append(p);
            }
        }
        // 标准组存在但全部无效 → 继续尝试 ParamMouthOpenY
    }

    // Priority 3: ParamMouthOpenY fallback
    // 只在没有更优先来源时尝试
    if (result.mouthSource == EmotionMouthSource::Disabled) {
        if (exists(QStringLiteral("ParamMouthOpenY"))) {
            result.mouthSource = EmotionMouthSource::ParamMouthOpenY;
            auto p = makeStub(QStringLiteral("ParamMouthOpenY"));
            p.isMouth = true;
            p.closed = 0.0f;
            p.open   = 1.0f;
            result.mouthParams.append(p);
        }
        // 没有 ParamMouthOpenY → 保持 Disabled
    }

    // ══════════════════════════════════════════════════════════
    // 2. 其他情绪参数 — 只在实际存在时 mapped=true
    // ══════════════════════════════════════════════════════════
    // ParamCheek、ParamBrowLY、ParamBrowRY、ParamBrowLAngle、ParamBrowRAngle
    // 按精确名称匹配，不猜测 ParamBrowLForm 等别名。
    result.cheek      = makeStub(QStringLiteral("ParamCheek"));
    result.browLY     = makeStub(QStringLiteral("ParamBrowLY"));
    result.browRY     = makeStub(QStringLiteral("ParamBrowRY"));
    result.browLAngle = makeStub(QStringLiteral("ParamBrowLAngle"));
    result.browRAngle = makeStub(QStringLiteral("ParamBrowRAngle"));

    // ── EyeSmile 当前不参与写入，仅记录存在性 ──
    result.eyeLSmile = makeStub(QStringLiteral("ParamEyeLSmile"));
    result.eyeRSmile = makeStub(QStringLiteral("ParamEyeRSmile"));

    return result;
}
