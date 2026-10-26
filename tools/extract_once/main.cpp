// extract_once — 无界面跑一次「自动抽取」，用于验证归因校验。
//
// 直接复用 MemoryExtractor::extractFromRecentMessages()（与桌宠每 6 轮 /
// 闲置 3 分钟的自动抽取同一条代码路径、同一个 prompt），把最近 N 条消息
// 发给模型，观察哪些卡片被 `丢弃无主人依据的卡片` 拦下。
//
// 建议在数据库副本上跑，避免污染真实记忆：
//   extract_once.exe --db E:/DesktopPet/test_copy.db --character xiaoyue- --limit 20
//   Key 默认取环境变量 DEEPSEEK_API_KEY，其次读 AppConfig 下的 deepseek_api_key.txt。

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>

#include "memory/MemoryDB.h"
#include "memory/MemoryExtractor.h"

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    static bool initialized = false;
    if (!initialized) {
        stream.setEncoding(QStringConverter::Utf8);
        initialized = true;
    }
    return stream;
}

void say(const QString &text)
{
    out() << text << Qt::endl;
}

QString loadApiKey()
{
    QString key = QProcessEnvironment::systemEnvironment()
                      .value(QStringLiteral("DEEPSEEK_API_KEY"));
    if (!key.isEmpty()) return key;

    const QString filePath = QStandardPaths::writableLocation(
                                 QStandardPaths::AppConfigLocation)
        + QStringLiteral("/deepseek_api_key.txt");
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        key = QString::fromUtf8(file.readLine()).trimmed();
    }
    return key;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("DesktopPet"));
    QCoreApplication::setOrganizationName(QStringLiteral("DesktopPetProject"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DesktopPet 自动抽取单次验证"));
    parser.addHelpOption();

    const QCommandLineOption dbOption(
        QStringLiteral("db"), QStringLiteral("数据库路径"), QStringLiteral("path"),
        QStringLiteral("E:/DesktopPet/DesktopPet/database/pet_memory.db"));
    const QCommandLineOption characterOption(
        QStringLiteral("character"), QStringLiteral("角色名（character_name）"),
        QStringLiteral("name"), QStringLiteral("xiaoyue-"));
    const QCommandLineOption limitOption(
        QStringLiteral("limit"), QStringLiteral("最近消息条数"), QStringLiteral("n"),
        QStringLiteral("20"));
    const QCommandLineOption keyOption(
        QStringLiteral("key"), QStringLiteral("API Key（默认读环境变量）"), QStringLiteral("key"),
        QString());
    parser.addOption(dbOption);
    parser.addOption(characterOption);
    parser.addOption(limitOption);
    parser.addOption(keyOption);
    parser.process(app);

    const QString dbPath = parser.value(dbOption);
    const QString character = parser.value(characterOption);
    const int limit = parser.value(limitOption).toInt();
    const QString apiKey =
        parser.value(keyOption).isEmpty() ? loadApiKey() : parser.value(keyOption);

    if (apiKey.isEmpty()) {
        qWarning() << "[extract_once] 没有 API Key（环境变量 DEEPSEEK_API_KEY 或配置文件）";
        return 2;
    }

    MemoryDB db;
    if (!db.open(dbPath)) {
        qWarning() << "[extract_once] 打开数据库失败：" << dbPath;
        return 2;
    }
    say(QStringLiteral("数据库：%1").arg(dbPath));
    say(QStringLiteral("角色：%1，最近 %2 条消息").arg(character).arg(limit));
    say(QStringLiteral("抽取前卡片数：%1").arg(db.memoryCount(character)));

    MemoryExtractor extractor(&db);
    extractor.setApiConfig(apiKey, QStringLiteral("https://api.deepseek.com/chat/completions"),
                           QStringLiteral("deepseek-v4-flash"));

    QObject::connect(&extractor, &MemoryExtractor::extractionFinished,
                     &app, [&](int added, int updated, int deleted) {
                         say(QStringLiteral("完成：新增 %1，更新 %2，删除 %3")
                                 .arg(added).arg(updated).arg(deleted));
                         say(QStringLiteral("抽取后卡片数：%1").arg(db.memoryCount(character)));
                         app.exit(0);
                     });
    QObject::connect(&extractor, &MemoryExtractor::extractionFailed,
                     &app, [&](const QString &error) {
                         say(QStringLiteral("请求失败：%1").arg(error));
                         app.exit(3);
                     });

    if (!extractor.extractFromRecentMessages(character, limit)) {
        say(QStringLiteral("抽取未能启动（没有消息 / 有在途请求 / 未配置 Key）"));
        return 2;
    }
    say(QStringLiteral("已发出请求，等待结果…"));

    QTimer::singleShot(120000, &app, []() {
        qWarning() << "[extract_once] 超时退出";
        qApp->exit(5);
    });
    return app.exec();
}
