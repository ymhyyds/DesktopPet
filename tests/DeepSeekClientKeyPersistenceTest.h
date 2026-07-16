#pragma once

/**
 * @brief DeepSeekClient API Key 持久化单元测试
 *
 * 验证：
 *   - setApiKeyAndPersist() 写入 QStandardPaths::AppConfigLocation
 *   - QSaveFile 原子写入（写入过程中崩溃不留下不完整文件）
 *   - 空 Key 时删除持久化文件
 *   - 写入验证后才删除旧程序目录的 Key 文件
 *   - 环境变量 DEEPSEEK_API_KEY 优先级最高
 *   - 构造函数从 AppConfigLocation 加载
 *   - 向后兼容旧程序目录下的 deepseek_api_key.txt
 */

#include <QObject>
#include <QTest>
#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QProcessEnvironment>
#include <QDebug>

#include "ai/DeepSeekClient.h"

class DeepSeekClientKeyPersistenceTest : public QObject
{
    Q_OBJECT

public:
    explicit DeepSeekClientKeyPersistenceTest() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void testSetAndPersistWritesToAppConfigLocation();
    void testReadbackMatchesWrittenContent();
    void testEmptyKeyRemovesFile();
    void testConstructorLoadsFromAppConfigLocation();
    void testConstructorEnvVarHasPriority();
    void testOldLocationDeletedAfterNewWrite();
    void testSetApiKeyInMemoryOnly();

private:
    /// 返回 DeepSeekClient 期望的持久化文件路径。
    static QString expectedKeyFilePath();

    /// 在 AppConfigLocation 创建 Key 文件（用于测试构造函数加载）。
    static void createKeyFileAtAppConfig(const QString &content);

    /// 清理 AppConfigLocation 下的 Key 文件。
    static void removeKeyFileAtAppConfig();
};
