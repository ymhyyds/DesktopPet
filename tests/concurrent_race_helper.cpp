/**
 * @brief 单实例竞争测试辅助程序。
 *
 * 被 SingleInstanceTest::testConcurrentRace 通过 QProcess 调用。
 * 从 argv[1] 读取服务名，尝试获取 → exit 0（成功）或 exit 1（被拒绝）。
 *
 * 用法: concurrent_race_helper <serverName>
 */

#include <QCoreApplication>
#include <QTimer>

#include "../core/SingleInstanceManager.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 2) {
        qCritical("用法: concurrent_race_helper <serverName>");
        return 2;
    }

    const QString serverName = QString::fromUtf8(argv[1]);

    SingleInstanceManager mgr(serverName);
    const bool acquired = mgr.tryAcquire();

    // 若成功获取，等待 300ms 再释放（模拟首实例占用）
    if (acquired) {
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &app, &QCoreApplication::quit);
        timer.setSingleShot(true);
        timer.start(300);
        app.exec();
    }

    return acquired ? 0 : 1;
}
