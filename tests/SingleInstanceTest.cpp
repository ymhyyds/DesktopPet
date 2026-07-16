#include <QTest>
#include <QSignalSpy>
#include <QUuid>
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>

#include "../core/SingleInstanceManager.h"

/**
 * @brief 单实例保护单元测试。
 *
 * 测试用例：
 *   - testFirstAcquireSucceeds:    首次获取应当成功。
 *   - testSecondAcquireFails:      第二次获取应当被拒绝。
 *   - testAcquireAfterRelease:     首实例释放后可再次获取。
 *   - testActivationSignal:        第二实例连接时首实例收到 activationRequested。
 *   - testConcurrentRace:          两个 QProcess 同时启动，严格只有一个成功。
 *
 * 每个用例使用独立的 UUID 服务名，互不干扰。
 */
class TestSingleInstance : public QObject
{
    Q_OBJECT

private:
    /// 生成测试用的唯一服务名（避免与真实服务或其他测试冲突）
    static QString uniqueServerName()
    {
        return QStringLiteral("test-SingleInstance-")
             + QUuid::createUuid().toString(QUuid::Id128);
    }

    /// 并发竞争测试 helper EXE 的路径
    static QString helperPath()
    {
        return QCoreApplication::applicationDirPath()
             + QStringLiteral("/concurrent_race_helper")
#ifdef Q_OS_WIN
             + QStringLiteral(".exe")
#endif
             ;
    }

private slots:
    void testFirstAcquireSucceeds();
    void testSecondAcquireFails();
    void testAcquireAfterRelease();
    void testActivationSignal();
    void testConcurrentRace();
};

// ---------------------------------------------------------------------------
// 测试 1：首次获取应当成功
// ---------------------------------------------------------------------------
void TestSingleInstance::testFirstAcquireSucceeds()
{
    const QString name = uniqueServerName();
    SingleInstanceManager mgr(name);
    QVERIFY(mgr.tryAcquire());
    mgr.releaseMutex();
}

// ---------------------------------------------------------------------------
// 测试 2：第二次获取应当被拒绝
// ---------------------------------------------------------------------------
void TestSingleInstance::testSecondAcquireFails()
{
    const QString name = uniqueServerName();

    SingleInstanceManager mgr1(name);
    QVERIFY(mgr1.tryAcquire()); // 首实例

    SingleInstanceManager mgr2(name);
    QVERIFY(!mgr2.tryAcquire()); // 第二实例 → 应被拒绝

    mgr1.releaseMutex();
}

// ---------------------------------------------------------------------------
// 测试 3：首实例释放后，再次获取应当成功
// ---------------------------------------------------------------------------
void TestSingleInstance::testAcquireAfterRelease()
{
    const QString name = uniqueServerName();

    // 首实例在局部作用域内获取并释放
    {
        SingleInstanceManager mgr1(name);
        QVERIFY(mgr1.tryAcquire());
        mgr1.releaseMutex();
    }
    // mgr1 已释放 Mutex

    QTest::qWait(200);

    // 重新获取应成功
    SingleInstanceManager mgr2(name);
    QVERIFY(mgr2.tryAcquire());
    mgr2.releaseMutex();
}

// ---------------------------------------------------------------------------
// 测试 4：第二实例连接时首实例收到 activationRequested 信号
// ---------------------------------------------------------------------------
void TestSingleInstance::testActivationSignal()
{
    const QString name = uniqueServerName();

    SingleInstanceManager mgr1(name);
    QVERIFY(mgr1.tryAcquire());

    QSignalSpy spy(&mgr1, &SingleInstanceManager::activationRequested);

    // 第二实例尝试获取 → 内部会连接并发送 "raise"
    {
        SingleInstanceManager mgr2(name);
        QVERIFY(!mgr2.tryAcquire());
    }

    // 处理事件循环让首实例的服务端读取管道中的 "raise" 消息
    QTest::qWait(500);

    // 验证 activationRequested 被发射了恰好一次
    QCOMPARE(spy.count(), 1);

    mgr1.releaseMutex();
}

// ---------------------------------------------------------------------------
// 测试 5：两个进程同时启动的竞争测试
// ---------------------------------------------------------------------------
void TestSingleInstance::testConcurrentRace()
{
    const QString name = uniqueServerName();
    const QString helper = helperPath();

    // 确认 helper 存在
    QVERIFY2(QFileInfo::exists(helper),
             qPrintable(QStringLiteral("Helper 不存在: ") + helper));

    // 并发启动两个 helper 进程，传给同一个服务名
    QProcess p1, p2;
    p1.setProgram(helper);
    p1.setArguments({name});
    p2.setProgram(helper);
    p2.setArguments({name});

    // 尽可能同时启动
    p1.start();
    p2.start();

    // 等待完成
    QVERIFY(p1.waitForFinished(10000));
    QVERIFY(p2.waitForFinished(10000));

    int exit1 = p1.exitCode();
    int exit2 = p2.exitCode();

    qInfo() << "[ConcurrentRace] p1 exit:" << exit1 << " p2 exit:" << exit2;

    // 严格只有一个获取成功 (exit 0)，另一个被拒绝 (exit 1)
    QVERIFY(exit1 != exit2);
    QVERIFY((exit1 == 0 && exit2 == 1) || (exit1 == 1 && exit2 == 0));

    // 确认 Mutex 已释放，可再次获取
    SingleInstanceManager mgr3(name);
    QVERIFY(mgr3.tryAcquire());
    mgr3.releaseMutex();
}

// ---------------------------------------------------------------------------
// 主函数
// ---------------------------------------------------------------------------
QTEST_GUILESS_MAIN(TestSingleInstance)
#include "SingleInstanceTest.moc"
