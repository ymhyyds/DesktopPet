#include "LipSyncConfig.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QtMath>
#include <QDebug>
#include <cmath>

// ====================================================================
// parseLipSyncConfig — 从 QByteArray 解析 JSON 配置
// ====================================================================

LipSyncConfig parseLipSyncConfig(const QByteArray& json, QString* errorOut)
{
    LipSyncConfig config;

    // 空或全空白输入 → 文件存在但内容为空，不是有效配置
    if (json.trimmed().isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("LipSync configuration is empty");
        return config;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);

    // JSON 损坏
    if (parseError.error != QJsonParseError::NoError) {
        if (errorOut) {
            *errorOut = QStringLiteral("JSON 解析错误: %1")
                            .arg(parseError.errorString());
        }
        return config;
    }

    // 根节点不是对象
    if (!doc.isObject()) {
        if (errorOut)
            *errorOut = QStringLiteral("根节点不是 JSON 对象");
        return config;
    }

    const QJsonObject root = doc.object();

    // ── version 校验 ──
    if (!root.contains(QStringLiteral("version"))) {
        if (errorOut)
            *errorOut = QStringLiteral("缺少 version 字段");
        return config;
    }

    const QJsonValue versionVal = root.value(QStringLiteral("version"));
    if (!versionVal.isDouble()) {
        if (errorOut)
            *errorOut = QStringLiteral("version 类型错误，期望整数");
        return config;
    }

    const int version = versionVal.toInt();
    // 检查是否确实为整数（排除小数）
    if (static_cast<double>(version) != versionVal.toDouble()) {
        if (errorOut)
            *errorOut = QStringLiteral("version 不是整数");
        return config;
    }

    if (version != 1) {
        if (errorOut)
            *errorOut = QStringLiteral("不支持的 version: %1 (仅支持 1)").arg(version);
        return config;
    }

    config.version = version;

    // ── parameters 校验 ──
    if (!root.contains(QStringLiteral("parameters"))) {
        if (errorOut)
            *errorOut = QStringLiteral("缺少 parameters 字段");
        return config;
    }

    const QJsonValue paramsVal = root.value(QStringLiteral("parameters"));
    if (!paramsVal.isArray()) {
        if (errorOut)
            *errorOut = QStringLiteral("parameters 不是数组");
        return config;
    }

    const QJsonArray paramsArray = paramsVal.toArray();

    // parameters 为空 → 配置有效但无参数
    if (paramsArray.isEmpty()) {
        if (errorOut) errorOut->clear();
        return config;
    }

    // ── 解析每个参数项 ──
    QSet<QString> seenIds;
    for (int i = 0; i < paramsArray.size(); ++i) {
        const QJsonValue itemVal = paramsArray.at(i);

        // 参数项不是对象 → 跳过并警告
        if (!itemVal.isObject()) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数项 [%1] 不是对象，跳过").arg(i);
            continue;
        }

        const QJsonObject item = itemVal.toObject();

        // id 缺失、不是字符串或 trim 后为空
        if (!item.contains(QStringLiteral("id"))) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数项 [%1] 缺少 id，跳过").arg(i);
            continue;
        }
        const QJsonValue idVal = item.value(QStringLiteral("id"));
        if (!idVal.isString()) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数项 [%1] id 不是字符串，跳过").arg(i);
            continue;
        }
        QString id = idVal.toString().trimmed();
        if (id.isEmpty()) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数项 [%1] id 为空，跳过").arg(i);
            continue;
        }

        // 重复 ID 检查：保留第一个
        if (seenIds.contains(id)) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 重复 ID \"%1\"，跳过").arg(id);
            continue;
        }

        // closed 缺失或不是有限数值
        if (!item.contains(QStringLiteral("closed"))) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数 \"%1\" 缺少 closed，跳过").arg(id);
            continue;
        }
        const QJsonValue closedVal = item.value(QStringLiteral("closed"));
        if (!closedVal.isDouble() || !std::isfinite(closedVal.toDouble())) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数 \"%1\" closed 不是有限数值，跳过").arg(id);
            continue;
        }

        // open 缺失或不是有限数值
        if (!item.contains(QStringLiteral("open"))) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数 \"%1\" 缺少 open，跳过").arg(id);
            continue;
        }
        const QJsonValue openVal = item.value(QStringLiteral("open"));
        if (!openVal.isDouble() || !std::isfinite(openVal.toDouble())) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数 \"%1\" open 不是有限数值，跳过").arg(id);
            continue;
        }

        // weight 默认 1.0
        double weight = 1.0;
        if (item.contains(QStringLiteral("weight"))) {
            const QJsonValue wVal = item.value(QStringLiteral("weight"));
            if (!wVal.isDouble() || !std::isfinite(wVal.toDouble())) {
                qWarning().noquote()
                    << QStringLiteral("[LipSyncConfig] 参数 \"%1\" weight 不是有限数值，跳过").arg(id);
                continue;
            }
            weight = wVal.toDouble();
        }

        // weight <= 0 → 跳过
        if (weight <= 0.0) {
            qWarning().noquote()
                << QStringLiteral("[LipSyncConfig] 参数 \"%1\" weight=%2 <= 0，跳过")
                       .arg(id).arg(weight);
            continue;
        }

        LipSyncParameterConfig param;
        param.id     = id;
        param.closed = closedVal.toDouble();
        param.open   = openVal.toDouble();
        param.weight = weight;

        config.parameters.append(param);
        seenIds.insert(id);
    }

    if (errorOut) errorOut->clear();
    return config;
}

// ====================================================================
// deriveLipSyncSidecarPath
// ====================================================================

QString deriveLipSyncSidecarPath(const QString& model3Dir,
                                  const QString& model3BaseName)
{
    // 使用 QDir::cleanPath 统一分隔符，再添加后缀
    const QString base = QDir::cleanPath(model3Dir);
    const QString separator = base.endsWith(QLatin1Char('/')) ? QString()
                                                              : QStringLiteral("/");
    return base + separator + model3BaseName + QStringLiteral(".lipsync.json");
}

// ====================================================================
// selectLipSyncSource
// ====================================================================

LipSyncParameterSource selectLipSyncSource(
    int standardGroupCount,
    int validStandardCount,
    bool sidecarExists,
    bool sidecarHasValidParams)
{
    // 标准组至少一个有效真实参数 → 优先使用标准组
    if (validStandardCount > 0)
        return LipSyncParameterSource::StandardGroup;

    // sidecar 存在且有有效参数 → 使用 sidecar
    if (sidecarExists && sidecarHasValidParams)
        return LipSyncParameterSource::ExternalConfig;

    return LipSyncParameterSource::Disabled;
}
