#pragma once

#include <QString>
#include <QStringList>
#include <QHash>
#include <QSet>
#include <QDir>

/**
 * @brief 单个人格条目（用于多角色切换增强——同个模型只换人格，不重载模型）。
 */
struct PersonalityEntry
{
    QString name;       // 人格显示名，如 "默认模式"、"傲娇模式"
    QString prompt;     // 人格描述文本，拼进 system prompt
};

/**
 * @brief 单个角色包的配置数据，对应 characters/<name>/config.json。
 *
 * 字段含义见 config.json：
 *  name        角色显示名
 *  personality 默认人格描述，会被 PromptManager 拼进系统 Prompt
 *  personalities 多个人格映射（key=人格名, value=人格Prompt），可选
 *  scale       窗口缩放比例
 *  modelScale  Live2D 模型缩放
 *  live2dModel Live2D 模型路径（相对角色目录或 SDK 内置）
 *  rootDir     该角色包所在的绝对目录
 */
struct CharacterConfig
{
    QString name;
    QString personality;
    double  scale = 1.0;
    double  modelScale = 1.0;
    QString live2dModel;
    QString rootDir;

    /// 多个人格列表（来自 config.json 的 personalities 字段），可选。
    /// 如果非空，personality 字段作为默认人格。
    QList<PersonalityEntry> personalities;

    bool isValid() const { return !name.isEmpty() && !rootDir.isEmpty(); }
};

/**
 * @brief CharacterLoader 负责扫描 characters/ 目录，解析每个角色包的 config.json。
 *
 * 支持两种模式：
 *  - 标准角色包：目录下有 config.json，可配名字、人格、模型路径等
 *  - 自动检测：目录下没有 config.json 但有 .model3.json，直接用目录名做角色名
 *
 * 新增角色只需要把整个模型文件夹丢进 characters/，不需要写任何配置文件。
 */
class CharacterLoader
{
public:
    CharacterLoader() = default;

    /**
     * @brief 扫描指定目录下的所有角色包子目录。
     * @param baseDir 角色包根目录（通常是 "characters"）
     * @return 是否至少找到一个合法角色包
     */
    bool scanCharacters(const QString &baseDir);

    /// 当前已扫描到的所有合法角色名（目录名）。
    QStringList availableCharacterNames() const { return m_characterDirs.keys(); }

    /// 加载（解析）指定角色的完整配置。如果角色不存在，返回的 CharacterConfig::isValid() 为 false。
    /// 内部会缓存解析结果，多次调用不会重复解析 JSON 文件。
    CharacterConfig loadCharacter(const QString &characterKey) const;

    /// 强制重新解析配置（角色配置可能已变更）。调用后下次 loadCharacter 会重新读文件。
    void invalidateCache(const QString &characterKey);

private:
    /// 解析单个角色目录下的 config.json，失败返回无效配置。
    CharacterConfig parseConfig(const QString &characterDir) const;

    /// 自动检测 Live2D 模型目录（无 config.json，但有 .model3.json）。
    CharacterConfig autoDetectModel(const QString &dirPath) const;

private:
    // key: 角色目录名（也作为角色的唯一标识），value: 角色包绝对路径
    QHash<QString, QString> m_characterDirs;

    // 缓存的解析结果，避免每次切换角色都重新解析 JSON
    mutable QHash<QString, CharacterConfig> m_configCache;

    // 自动检测的角色 key 集合（没有 config.json，由 autoDetectModel 生成）
    mutable QSet<QString> m_autoDetected;
};
