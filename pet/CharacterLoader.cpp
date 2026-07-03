#include "CharacterLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDebug>

bool CharacterLoader::scanCharacters(const QString &baseDir)
{
    m_characterDirs.clear();
    m_configCache.clear();
    m_autoDetected.clear();

    QDir root(baseDir);
    if (!root.exists()) {
        qWarning() << "[CharacterLoader] 角色根目录不存在：" << root.absolutePath();
        return false;
    }

    const QFileInfoList subDirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    qDebug() << "[CharacterLoader] 扫描目录" << root.absolutePath() << "找到" << subDirs.size() << "个子目录";
    for (const QFileInfo &dirInfo : subDirs) {
        qDebug() << "[CharacterLoader]   -" << dirInfo.fileName();
        const QString dirPath = dirInfo.absoluteFilePath();
        const QString configPath = QDir(dirPath).filePath(QStringLiteral("config.json"));

        if (QFile::exists(configPath)) {
            // 标准角色包：有 config.json
            const CharacterConfig cfg = parseConfig(dirPath);
            if (!cfg.isValid()) {
                qWarning() << "[CharacterLoader] 角色包配置无效，跳过：" << dirInfo.absoluteFilePath();
                continue;
            }
            m_characterDirs.insert(dirInfo.fileName(), dirPath);
            qDebug() << "[CharacterLoader]   ✓ 标准角色包：" << dirInfo.fileName();

        } else {
            // 没有 config.json → 尝试自动检测 Live2D 模型
            const CharacterConfig cfg = autoDetectModel(dirPath);
            if (cfg.isValid()) {
                m_characterDirs.insert(dirInfo.fileName(), dirPath);
                m_configCache.insert(dirInfo.fileName(), cfg);
                m_autoDetected.insert(dirInfo.fileName());
                qInfo() << "[CharacterLoader] 自动检测到 Live2D 模型：" << cfg.name;
            } else {
                qDebug() << "[CharacterLoader]   ✗ 未找到 .model3.json，跳过：" << dirInfo.fileName();
            }
        }
    }

    return !m_characterDirs.isEmpty();
}

CharacterConfig CharacterLoader::autoDetectModel(const QString &dirPath) const
{
    QDir dir(dirPath);
    const QStringList filters = { QStringLiteral("*.model3.json") };
    const QFileInfoList entries = dir.entryInfoList(filters, QDir::Files | QDir::NoDotAndDotDot);

    if (entries.isEmpty()) {
        return CharacterConfig();
    }

    CharacterConfig cfg;
    cfg.name        = dir.dirName();
    cfg.scale       = 1.0;
    cfg.modelScale  = 1.0;
    cfg.live2dModel = QStringLiteral(".");
    cfg.rootDir     = dirPath;

    return cfg;
}

CharacterConfig CharacterLoader::parseConfig(const QString &characterDir) const
{
    CharacterConfig cfg;

    const QString configPath = QDir(characterDir).filePath(QStringLiteral("config.json"));
    QFile file(configPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[CharacterLoader] 无法打开：" << configPath;
        return cfg;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[CharacterLoader] JSON 解析失败：" << configPath << parseError.errorString();
        return cfg;
    }

    const QJsonObject obj = doc.object();

    cfg.name        = obj.value(QStringLiteral("name")).toString();
    cfg.personality = obj.value(QStringLiteral("personality")).toString();
    cfg.scale       = obj.value(QStringLiteral("scale")).toDouble(1.0);
    cfg.modelScale  = obj.value(QStringLiteral("modelScale")).toDouble(1.0);
    cfg.live2dModel = obj.value(QStringLiteral("live2dModel")).toString();
    cfg.rootDir     = characterDir;

    // 解析多个人格（可选）
    cfg.personalities.clear();
    const QJsonObject personas = obj.value(QStringLiteral("personalities")).toObject();
    for (auto it = personas.begin(); it != personas.end(); ++it) {
        if (it.value().isString()) {
            cfg.personalities.append({ it.key(), it.value().toString() });
        }
    }

    if (cfg.name.isEmpty()) {
        qWarning() << "[CharacterLoader] config.json 缺少 name 字段：" << configPath;
        cfg.rootDir.clear();
        return cfg;
    }

    return cfg;
}

CharacterConfig CharacterLoader::loadCharacter(const QString &characterKey) const
{
    if (!m_characterDirs.contains(characterKey)) {
        qWarning() << "[CharacterLoader] 角色不存在：" << characterKey;
        return CharacterConfig();
    }

    auto it = m_configCache.constFind(characterKey);
    if (it != m_configCache.constEnd()) {
        return it.value();
    }

    // 缓存未命中：自动检测的角色重新生成配置，标准角色包重新解析 JSON
    if (m_autoDetected.contains(characterKey)) {
        const CharacterConfig cfg = autoDetectModel(m_characterDirs.value(characterKey));
        if (cfg.isValid()) {
            m_configCache.insert(characterKey, cfg);
        }
        return cfg;
    }

    const CharacterConfig cfg = parseConfig(m_characterDirs.value(characterKey));
    if (cfg.isValid()) {
        m_configCache.insert(characterKey, cfg);
    }
    return cfg;
}

void CharacterLoader::invalidateCache(const QString &characterKey)
{
    m_configCache.remove(characterKey);
}
