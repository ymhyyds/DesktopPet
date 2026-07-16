#include "DeepSeekClientKeyPersistenceTest.h"

// ── helpers ──

QString DeepSeekClientKeyPersistenceTest::expectedKeyFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/deepseek_api_key.txt");
}

void DeepSeekClientKeyPersistenceTest::createKeyFileAtAppConfig(const QString &content)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    QFile file(dir + QStringLiteral("/deepseek_api_key.txt"));
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << content;
    }
}

void DeepSeekClientKeyPersistenceTest::removeKeyFileAtAppConfig()
{
    QFile::remove(expectedKeyFilePath());
}

// ── test lifecycle ──

void DeepSeekClientKeyPersistenceTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("DesktopPetProject"));
    QCoreApplication::setApplicationName(QStringLiteral("DesktopPet"));
    removeKeyFileAtAppConfig();
}

void DeepSeekClientKeyPersistenceTest::cleanupTestCase()
{
    removeKeyFileAtAppConfig();
    QFile::remove(QStringLiteral("deepseek_api_key.txt"));
}

void DeepSeekClientKeyPersistenceTest::init()
{
    removeKeyFileAtAppConfig();
    QFile::remove(QStringLiteral("deepseek_api_key.txt"));
}

void DeepSeekClientKeyPersistenceTest::cleanup()
{
}

// ── test cases ──

void DeepSeekClientKeyPersistenceTest::testSetAndPersistWritesToAppConfigLocation()
{
    const QString testKey = QStringLiteral("sk-test-persist-write-location");

    DeepSeekClient client;
    QVERIFY(client.setApiKeyAndPersist(testKey));
    QVERIFY(client.hasApiKey());

    const QString filePath = expectedKeyFilePath();
    QVERIFY2(QFile::exists(filePath),
             qPrintable(QStringLiteral("Key 文件应存在于 %1").arg(filePath)));

    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&file);
    QCOMPARE(stream.readLine().trimmed(), testKey);
}

void DeepSeekClientKeyPersistenceTest::testReadbackMatchesWrittenContent()
{
    const QString testKey = QStringLiteral("sk-special-chars-test-key");

    DeepSeekClient client;
    QVERIFY(client.setApiKeyAndPersist(testKey));

    const QString filePath = expectedKeyFilePath();
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&file);
    QCOMPARE(stream.readAll().trimmed(), testKey);
}

void DeepSeekClientKeyPersistenceTest::testEmptyKeyRemovesFile()
{
    DeepSeekClient client;
    QVERIFY(client.setApiKeyAndPersist(QStringLiteral("sk-temp-key")));

    const QString filePath = expectedKeyFilePath();
    QVERIFY(QFile::exists(filePath));

    QVERIFY(client.setApiKeyAndPersist(QString()));
    QVERIFY(!client.hasApiKey());
    QVERIFY2(!QFile::exists(filePath),
             qPrintable(QStringLiteral("空 Key 后文件应被删除: %1").arg(filePath)));
    QVERIFY(!QFile::exists(QStringLiteral("deepseek_api_key.txt")));
}

void DeepSeekClientKeyPersistenceTest::testConstructorLoadsFromAppConfigLocation()
{
    const QString testKey = QStringLiteral("sk-constructor-load-test");
    createKeyFileAtAppConfig(testKey);

    const QByteArray originalEnv = qgetenv("DEEPSEEK_API_KEY");
    qunsetenv("DEEPSEEK_API_KEY");

    {
        DeepSeekClient client;
        QVERIFY2(client.hasApiKey(),
                 "构造函数应从 AppConfigLocation 加载 Key 文件");
    }

    if (!originalEnv.isEmpty())
        qputenv("DEEPSEEK_API_KEY", originalEnv);
}

void DeepSeekClientKeyPersistenceTest::testConstructorEnvVarHasPriority()
{
    createKeyFileAtAppConfig(QStringLiteral("sk-file-key"));

    const QByteArray envKey = QStringLiteral("sk-env-var-priority-key").toUtf8();
    qputenv("DEEPSEEK_API_KEY", envKey);

    {
        DeepSeekClient client;
        QVERIFY(client.hasApiKey());
        QVERIFY(client.setApiKeyAndPersist(QStringLiteral("sk-override")));
    }

    qunsetenv("DEEPSEEK_API_KEY");
    removeKeyFileAtAppConfig();
}

void DeepSeekClientKeyPersistenceTest::testOldLocationDeletedAfterNewWrite()
{
    {
        QFile oldFile(QStringLiteral("deepseek_api_key.txt"));
        QVERIFY(oldFile.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream stream(&oldFile);
        stream << "sk-old-location-key";
    }
    QVERIFY(QFile::exists(QStringLiteral("deepseek_api_key.txt")));

    DeepSeekClient client;
    QVERIFY(client.setApiKeyAndPersist(QStringLiteral("sk-new-location-key")));

    QVERIFY2(!QFile::exists(QStringLiteral("deepseek_api_key.txt")),
             "setApiKeyAndPersist 成功后应删除旧程序目录的 Key 文件");

    const QString filePath = expectedKeyFilePath();
    QVERIFY(QFile::exists(filePath));
    QFile file(filePath);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&file);
    QCOMPARE(stream.readLine().trimmed(), QStringLiteral("sk-new-location-key"));
}

void DeepSeekClientKeyPersistenceTest::testSetApiKeyInMemoryOnly()
{
    DeepSeekClient client;
    client.setApiKey(QStringLiteral("sk-memory-only"));

    QVERIFY(client.hasApiKey());
    QVERIFY(!QFile::exists(expectedKeyFilePath()));
    QVERIFY(!QFile::exists(QStringLiteral("deepseek_api_key.txt")));
}
