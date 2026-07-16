#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "../core/AutoStartManager.h"

/**
 * @brief AutoStartTest — 开机自启注册表操作单元测试。
 *
 * 使用 QSettings(IniFormat) 写入临时 INI 文件替代真实注册表，
 * 验证 AutoStartManager 命名空间中所有函数的正确行为。
 */
class AutoStartTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // 启用 / 禁用
    void testEnableAutoStart();
    void testDisableAutoStart();
    void testEnableDisableEnable();
    void testOldPathResidualOnEnable();
    void testOldPathResidualOnDisable();

    // 保存与恢复
    void testSavePreviousWhenExists();
    void testSavePreviousWhenNotExists();
    void testRestorePreviousWhenExisted();
    void testRestorePreviousWhenNotExisted();

    // 精确验证
    void testVerifyExactPath();
    void testVerifyAbsence();
    void testVerifyRejectsWrongPath();

    // 恢复验证
    void testVerifyRestoreWhenExisted();
    void testVerifyRestoreWhenNotExisted();

    // 错误恢复
    void testRollbackAfterApplyFailure();
    void testFullRollbackWithRestoreVerification();

private:
    QTemporaryDir *m_tempDir = nullptr;
    QString m_iniPath;
    QString m_key;
    QString m_fakeExePath;

    QSettings createSettings() const;
};

// ═══════════════════════════════════════════════════════════════════════════
// 初始化
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::initTestCase()
{
    m_tempDir = new QTemporaryDir();
    QVERIFY(m_tempDir->isValid());
    m_iniPath = m_tempDir->filePath(QStringLiteral("test_autostart.ini"));
    m_key = QStringLiteral("DesktopPetTest");
    m_fakeExePath = QStringLiteral("C:\\Program Files\\DesktopPet\\DesktopPet.exe");
}

void AutoStartTest::cleanupTestCase()
{
    if (m_tempDir) {
        m_tempDir->remove();
        delete m_tempDir;
    }
}

QSettings AutoStartTest::createSettings() const
{
    return QSettings(m_iniPath, QSettings::IniFormat);
}

// ═══════════════════════════════════════════════════════════════════════════
// 启用 / 禁用
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testEnableAutoStart()
{
    // 初始：key 不存在
    {
        QSettings reg = createSettings();
        QVERIFY(!reg.contains(m_key));
    }

    // 启用
    {
        QSettings reg = createSettings();
        bool ok = AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath);
        QVERIFY(ok);
    }

    // 验证：存在且值正确
    {
        QSettings reg = createSettings();
        QVERIFY(reg.contains(m_key));
        QCOMPARE(reg.value(m_key).toString(),
                 QStringLiteral("\"%1\"").arg(m_fakeExePath));
        bool verified = AutoStartManager::verifyAutoStart(reg, m_key, true, m_fakeExePath);
        QVERIFY(verified);
    }

    // 清理
    {
        QSettings reg = createSettings();
        reg.remove(m_key);
        reg.sync();
    }
}

void AutoStartTest::testDisableAutoStart()
{
    // 先写入
    {
        QSettings reg = createSettings();
        AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath);
    }

    // 禁用
    {
        QSettings reg = createSettings();
        bool ok = AutoStartManager::applyAutoStart(reg, m_key, false, m_fakeExePath);
        QVERIFY(ok);
    }

    // 验证：已删除
    {
        QSettings reg = createSettings();
        QVERIFY(!reg.contains(m_key));
        bool verified = AutoStartManager::verifyAutoStart(reg, m_key, false, m_fakeExePath);
        QVERIFY(verified);
    }
}

void AutoStartTest::testEnableDisableEnable()
{
    const QString otherExe = QStringLiteral("D:\\Other\\app.exe");

    // 启用
    {
        QSettings reg = createSettings();
        QVERIFY(AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath));
    }

    // 禁用
    {
        QSettings reg = createSettings();
        QVERIFY(AutoStartManager::applyAutoStart(reg, m_key, false, otherExe));
        QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, false, otherExe));
    }

    // 再启用（不同路径）
    {
        QSettings reg = createSettings();
        QVERIFY(AutoStartManager::applyAutoStart(reg, m_key, true, otherExe));
        QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, true, otherExe));
    }

    // 清理
    {
        QSettings reg = createSettings();
        reg.remove(m_key);
        reg.sync();
    }
}

void AutoStartTest::testOldPathResidualOnEnable()
{
    // 预置旧路径（同 key，不同值 — 模拟之前安装留下的残留）
    const QString oldPath = QStringLiteral("\"C:\\Old\\Install\\DesktopPet.exe\"");
    {
        QSettings reg = createSettings();
        reg.setValue(m_key, oldPath);
        reg.sync();
    }

    // 保存旧值
    QSettings reg = createSettings();
    AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, m_key);
    QVERIFY(saved.existed);
    QCOMPARE(saved.value, oldPath);

    // 启用（应覆盖为当前 EXE 路径）
    bool ok = AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath);
    QVERIFY(ok);

    // 验证：已更新为新路径（不是旧残留）
    QVERIFY(reg.value(m_key).toString() != oldPath);
    QCOMPARE(reg.value(m_key).toString(),
             QStringLiteral("\"%1\"").arg(m_fakeExePath));
    QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, true, m_fakeExePath));
    QVERIFY(!AutoStartManager::verifyAutoStart(reg, m_key, true,
               QStringLiteral("C:\\Old\\Install\\DesktopPet.exe")));

    // 恢复原始状态
    AutoStartManager::restorePrevious(reg, m_key, saved);
    QCOMPARE(reg.value(m_key).toString(), oldPath);
    reg.remove(m_key);
    reg.sync();
}

void AutoStartTest::testOldPathResidualOnDisable()
{
    // 预置旧路径（模拟之前安装残留）
    const QString oldPath = QStringLiteral("\"C:\\Old\\Install\\DesktopPet.exe\"");
    {
        QSettings reg = createSettings();
        reg.setValue(m_key, oldPath);
        reg.sync();
    }

    // 保存旧值
    QSettings reg = createSettings();
    AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, m_key);
    QVERIFY(saved.existed);
    QCOMPARE(saved.value, oldPath);

    // 禁用（应删除 key，无论旧值是什么路径）
    bool ok = AutoStartManager::applyAutoStart(reg, m_key, false, m_fakeExePath);
    QVERIFY(ok);

    // 验证：key 已删除
    QVERIFY(!reg.contains(m_key));
    QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, false, m_fakeExePath));

    // 恢复原始状态
    AutoStartManager::restorePrevious(reg, m_key, saved);
    QCOMPARE(reg.value(m_key).toString(), oldPath);
    reg.remove(m_key);
    reg.sync();
}

// ═══════════════════════════════════════════════════════════════════════════
// 保存与恢复
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testSavePreviousWhenExists()
{
    const QString oldValue = QStringLiteral("\"C:\\Old\\Path\\old.exe\"");

    // 预置旧值
    {
        QSettings reg = createSettings();
        reg.setValue(m_key, oldValue);
        reg.sync();
    }

    // 保存
    QSettings reg = createSettings();
    AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, m_key);
    QVERIFY(saved.existed);
    QCOMPARE(saved.value, oldValue);

    // 清理
    reg.remove(m_key);
    reg.sync();
}

void AutoStartTest::testSavePreviousWhenNotExists()
{
    QSettings reg = createSettings();
    QVERIFY(!reg.contains(m_key));

    AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, m_key);
    QVERIFY(!saved.existed);
    QVERIFY(saved.value.isEmpty());
}

void AutoStartTest::testRestorePreviousWhenExisted()
{
    const QString oldValue = QStringLiteral("\"C:\\Old\\Path\\old.exe\"");

    // 预置旧值
    {
        QSettings reg = createSettings();
        reg.setValue(m_key, oldValue);
        reg.sync();
    }

    // 保存
    QSettings reg = createSettings();
    AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, m_key);

    // 覆盖
    reg.setValue(m_key, QStringLiteral("\"%1\"").arg(m_fakeExePath));
    reg.sync();

    // 恢复
    AutoStartManager::restorePrevious(reg, m_key, saved);

    // 验证：回到旧值
    QCOMPARE(reg.value(m_key).toString(), oldValue);

    // 清理
    reg.remove(m_key);
    reg.sync();
}

void AutoStartTest::testRestorePreviousWhenNotExisted()
{
    {
        QSettings reg = createSettings();
        QVERIFY(!reg.contains(m_key));
    }

    // 保存（不存在）
    QSettings reg = createSettings();
    AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, m_key);
    QVERIFY(!saved.existed);

    // 写入值
    reg.setValue(m_key, QStringLiteral("\"%1\"").arg(m_fakeExePath));
    reg.sync();

    // 恢复（应删除）
    AutoStartManager::restorePrevious(reg, m_key, saved);

    // 验证：已删除
    QVERIFY(!reg.contains(m_key));
}

// ═══════════════════════════════════════════════════════════════════════════
// 精确验证
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testVerifyExactPath()
{
    QSettings reg = createSettings();

    // 写入带引号的路径
    QVERIFY(AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath));

    // 正确路径验证通过
    QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, true, m_fakeExePath));

    // 值必须精准包含引号
    const QString withoutQuotes = m_fakeExePath;
    QVERIFY(reg.value(m_key).toString() != withoutQuotes);

    // 清理
    reg.remove(m_key);
    reg.sync();
}

void AutoStartTest::testVerifyAbsence()
{
    {
        QSettings reg = createSettings();
        reg.remove(m_key);
        reg.sync();
    }

    {
        QSettings reg = createSettings();
        // 空状态验证通过
        QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, false, m_fakeExePath));
    }

    // 写入再删除
    {
        QSettings reg = createSettings();
        AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath);
    }
    {
        QSettings reg = createSettings();
        QVERIFY(reg.contains(m_key));
    }
    {
        QSettings reg = createSettings();
        AutoStartManager::applyAutoStart(reg, m_key, false, m_fakeExePath);
    }
    {
        QSettings reg = createSettings();
        QVERIFY(!reg.contains(m_key));
        QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, false, m_fakeExePath));
    }
}

void AutoStartTest::testVerifyRejectsWrongPath()
{
    QSettings reg = createSettings();

    const QString wrongPath = QStringLiteral("E:\\Wrong\\app.exe");

    // 写入正确路径
    QVERIFY(AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath));

    // 用错误路径验证应失败
    QVERIFY(!AutoStartManager::verifyAutoStart(reg, m_key, true, wrongPath));

    // 用正确路径验证应通过
    QVERIFY(AutoStartManager::verifyAutoStart(reg, m_key, true, m_fakeExePath));

    reg.remove(m_key);
    reg.sync();
}

// ═══════════════════════════════════════════════════════════════════════════
// 恢复验证
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testVerifyRestoreWhenExisted()
{
    const QString orig = QStringLiteral("\"C:\\Original\\app.exe\"");

    // 预置原始值
    {
        QSettings reg = createSettings();
        reg.setValue(m_key, orig);
        reg.sync();
    }

    // 保存
    QSettings reg = createSettings();
    auto saved = AutoStartManager::savePrevious(reg, m_key);

    // 覆盖
    reg.setValue(m_key, QStringLiteral("\"%1\"").arg(m_fakeExePath));
    reg.sync();

    // restorePrevious + verifyRestore
    AutoStartManager::restorePrevious(reg, m_key, saved);
    QVERIFY(AutoStartManager::verifyRestore(reg, m_key, saved));

    // 清理
    reg.remove(m_key);
    reg.sync();
}

void AutoStartTest::testVerifyRestoreWhenNotExisted()
{
    // 保存（初始不存在）
    QSettings reg = createSettings();
    auto saved = AutoStartManager::savePrevious(reg, m_key);
    QVERIFY(!saved.existed);

    // 写入
    reg.setValue(m_key, QStringLiteral("\"%1\"").arg(m_fakeExePath));
    reg.sync();

    // 恢复（应删除）
    AutoStartManager::restorePrevious(reg, m_key, saved);

    // 验证恢复
    QVERIFY(!reg.contains(m_key));
    QVERIFY(AutoStartManager::verifyRestore(reg, m_key, saved));
}

// ═══════════════════════════════════════════════════════════════════════════
// 错误恢复
// ═══════════════════════════════════════════════════════════════════════════

void AutoStartTest::testRollbackAfterApplyFailure()
{
    // 该测试模拟"应用失败后恢复原值"模式。
    // 使用文件式 QSettings（IniFormat）时 QSettings::status 通常不会出错，
    // 所以我们模拟完整的安全操作序列。

    const QString oldValue = QStringLiteral("\"C:\\Production\\pet.exe\"");

    // 预置旧值
    {
        QSettings reg = createSettings();
        reg.setValue(m_key, oldValue);
        reg.sync();
    }

    // (1) 保存原值
    QSettings reg = createSettings();
    AutoStartManager::SavedRegValue saved = AutoStartManager::savePrevious(reg, m_key);
    QVERIFY(saved.existed);
    QCOMPARE(saved.value, oldValue);

    // (2) 模拟"启用自启"
    const bool applied = AutoStartManager::applyAutoStart(reg, m_key, true, m_fakeExePath);
    QVERIFY(applied);

    // (3) 模拟"验证失败"→ 恢复
    AutoStartManager::restorePrevious(reg, m_key, saved);

    // 验证：恢复到旧值
    QCOMPARE(reg.value(m_key).toString(), oldValue);
    QVERIFY(AutoStartManager::verifyRestore(reg, m_key, saved));

    reg.remove(m_key);
    reg.sync();
}

void AutoStartTest::testFullRollbackWithRestoreVerification()
{
    // 完整模拟 App::handleAutoStartToggle 中验证失败后的回滚路径

    const QString oldValue = QStringLiteral("\"C:\\Production\\pet.exe\"");

    // 预置旧值
    {
        QSettings reg = createSettings();
        reg.setValue(m_key, oldValue);
        reg.sync();
    }

    // === 模拟启用自启 (enabled=true) ===

    // (1) savePrevious
    QSettings reg1 = createSettings();
    auto saved = AutoStartManager::savePrevious(reg1, m_key);
    QVERIFY(saved.existed);
    QCOMPARE(saved.value, oldValue);

    // (2) applyAutoStart — 模拟写入新路径
    QVERIFY(AutoStartManager::applyAutoStart(reg1, m_key, true, m_fakeExePath));
    QVERIFY(AutoStartManager::verifyAutoStart(reg1, m_key, true, m_fakeExePath));

    // (3) 模拟验证失败 → 恢复
    QSettings reg2 = createSettings();  // 新实例模拟验证时的重新读取
    AutoStartManager::restorePrevious(reg2, m_key, saved);
    QVERIFY(AutoStartManager::verifyRestore(reg2, m_key, saved));

    // (4) 根据真实注册表状态设置菜单
    const bool actualEnabled = AutoStartManager::verifyAutoStart(reg2, m_key, true, m_fakeExePath);
    // 恢复后不应为启用状态（除非恢复后的值恰好等于当前 EXE 路径）
    if (oldValue == QStringLiteral("\"%1\"").arg(m_fakeExePath)) {
        QVERIFY(actualEnabled);
    } else {
        QVERIFY(!actualEnabled);
    }

    // === 模拟禁用自启 (enabled=false) ===

    // (1) savePrevious
    auto saved2 = AutoStartManager::savePrevious(reg2, m_key);
    QVERIFY(saved2.existed);

    // (2) 禁用（删除）
    QVERIFY(AutoStartManager::applyAutoStart(reg2, m_key, false, m_fakeExePath));
    QVERIFY(!reg2.contains(m_key));

    // (3) 模拟验证失败 → 恢复
    AutoStartManager::restorePrevious(reg2, m_key, saved2);
    QVERIFY(AutoStartManager::verifyRestore(reg2, m_key, saved2));

    // (4) 真实状态：验证恢复后的值就是旧值
    QCOMPARE(reg2.value(m_key).toString(), oldValue);

    // 清理
    reg2.remove(m_key);
    reg2.sync();
}

// ═══════════════════════════════════════════════════════════════════════════
// 主函数
// ═══════════════════════════════════════════════════════════════════════════

QTEST_GUILESS_MAIN(AutoStartTest)
#include "AutoStartTest.moc"
