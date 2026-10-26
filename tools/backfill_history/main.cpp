// backfill_history — 无界面历史回填工具。
//
// 直接复用 MemoryExtractor::startBackfill()（与设置页「从历史回填」按钮同一条代码路径），
// 便于在桌宠运行中或关闭时批量回填，输出进度到 stdout。
//
// 用法：
//   backfill_history.exe --db E:/DesktopPet/DesktopPet/database/pet_memory.db \
//       --character xiaoyue- --before 2026-09-01T00:00:00 [--after 2026-07-01T00:00:00]
//   Key 默认取环境变量 DEEPSEEK_API_KEY，其次读 AppConfig 下的 deepseek_api_key.txt。

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTextStream>
#include <QStringConverter>

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
    parser.setApplicationDescription(QStringLiteral("DesktopPet 长期记忆历史回填"));
    parser.addHelpOption();

    const QCommandLineOption dbOption(
        QStringLiteral("db"), QStringLiteral("数据库路径"), QStringLiteral("path"),
        QStringLiteral("E:/DesktopPet/DesktopPet/database/pet_memory.db"));
    const QCommandLineOption characterOption(
        QStringLiteral("character"), QStringLiteral("角色名（character_name）"),
        QStringLiteral("name"), QStringLiteral("xiaoyue-"));
    const QCommandLineOption afterOption(
        QStringLiteral("after"), QStringLiteral("起始（含），留空=最早"), QStringLiteral("iso"),
        QString());
    const QCommandLineOption beforeOption(
        QStringLiteral("before"), QStringLiteral("截止（不含）"), QStringLiteral("iso"),
        QStringLiteral("2026-09-01T00:00:00"));
    const QCommandLineOption keyOption(
        QStringLiteral("key"), QStringLiteral("API Key（默认读环境变量）"), QStringLiteral("key"),
        QString());
    parser.addOption(dbOption);
    parser.addOption(characterOption);
    parser.addOption(afterOption);
    parser.addOption(beforeOption);
    parser.addOption(keyOption);
    parser.process(app);

    const QString dbPath = parser.value(dbOption);
    const QString character = parser.value(characterOption);
    const QString afterIso = parser.value(afterOption).trimmed();
    const QString beforeIso = parser.value(beforeOption).trimmed();
    const QString apiKey =
        parser.value(keyOption).isEmpty() ? loadApiKey() : parser.value(keyOption);

    if (apiKey.isEmpty()) {
        qWarning() << "[backfill] 没有 API Key（环境变量 DEEPSEEK_API_KEY 或配置文件）";
        return 2;
    }

    MemoryDB db;
    if (!db.open(dbPath)) {
        qWarning() << "[backfill] 打开数据库失败：" << dbPath;
        return 2;
    }
    say(QStringLiteral("数据库：%1").arg(dbPath));
    say(QStringLiteral("角色：%1").arg(character));
    say(QStringLiteral("范围：%1 → %2（不含）")
            .arg(afterIso.isEmpty() ? QStringLiteral("最早") : afterIso, beforeIso));
    say(QStringLiteral("回填前卡片数：%1").arg(db.memoryCount(character)));

    MemoryExtractor extractor(&db);
    extractor.setApiConfig(apiKey, QStringLiteral("https://api.deepseek.com/chat/completions"),
                           QStringLiteral("deepseek-v4-flash"));

    QObject::connect(&extractor, &MemoryExtractor::backfillProgress,
                     &app, [](int done, int total) {
                         say(QStringLiteral("进度：%1 / %2").arg(done).arg(total));
                     });
    QObject::connect(&extractor, &MemoryExtractor::backfillFinished,
                     &app, [&](int added, int updated, int failed) {
                         say(QStringLiteral("完成：新增 %1，更新 %2，失败块 %3")
                                 .arg(added).arg(updated).arg(failed));
                         say(QStringLiteral("回填后卡片数：%1").arg(db.memoryCount(character)));
                         app.exit(failed > 0 ? 3 : 0);
                     });
    QObject::connect(&extractor, &MemoryExtractor::backfillCancelled,
                     &app, [&](int done, int total) {
                         say(QStringLiteral("已取消：%1 / %2").arg(done).arg(total));
                         app.exit(4);
                     });

    if (!extractor.startBackfill(character, afterIso, beforeIso)) {
        say(QStringLiteral("回填未能启动（没有消息 / 有在途请求 / 未配置 Key）"));
        return 2;
    }
    say(QStringLiteral("已开始，等待串行请求…"));
    return app.exec();
}
