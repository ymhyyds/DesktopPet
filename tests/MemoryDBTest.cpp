// MemoryDB 长期记忆层测试：迁移、CRUD、FTS 同步、去重候选、角色隔离。
// 构建：cmake -DBUILD_TTS_TESTS=ON -B build && cmake --build build --target test_memory_db --config Release

#include <QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <memory>

#include "memory/MemoryDB.h"

class MemoryDBTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void migrationCreatesSchema();
    void addAndListMemory();
    void searchMatchesThreeCharAndTwoCharQueries();
    void updateAndDeleteKeepFtsInSync();
    void fixedMemoriesIncludeSharedProfile();
    void characterScopeIsolation();
    void clearMemoriesKeepsSharedProfile();
    void recentAndBeforeMessages();
    void loadMessagesRangeHonorsBounds();
    void memoriesByKindFiltersKindAndCharacter();
    void memoriesByKindOrdersPinnedThenImportance();
    void memoriesByKindSkipsArchivedAndHonorsLimit();

private:
    MemoryRecord make(const QString &character, const QString &content,
                      const QString &kind = QStringLiteral("event"),
                      const QString &subject = QStringLiteral("主人"),
                      int importance = 3) const;

    QTemporaryDir m_dir;
    QString m_path;
    std::unique_ptr<MemoryDB> m_db;
};

void MemoryDBTest::init()
{
    QVERIFY(m_dir.isValid());
    m_path = m_dir.filePath(QStringLiteral("pet_memory_test.db"));
    QFile::remove(m_path);
    m_db = std::make_unique<MemoryDB>();
    QVERIFY(m_db->open(m_path));
}

void MemoryDBTest::cleanup()
{
    m_db.reset();
    QFile::remove(m_path);
}

MemoryRecord MemoryDBTest::make(const QString &character, const QString &content,
                                const QString &kind, const QString &subject,
                                int importance) const
{
    MemoryRecord m;
    m.characterName = character;
    m.kind = kind;
    m.subject = subject;
    m.content = content;
    m.importance = importance;
    return m;
}

void MemoryDBTest::migrationCreatesSchema()
{
    {
        QSqlDatabase check = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                       QStringLiteral("mem_check"));
        check.setDatabaseName(m_path);
        QVERIFY(check.open());
        QSqlQuery q(check);
        QVERIFY(q.exec(QStringLiteral("PRAGMA user_version")));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 1);

        QVERIFY(q.exec(QStringLiteral(
            "SELECT name FROM sqlite_master WHERE name IN "
            "('memories','memories_fts','memories_ai','memories_ad','memories_au')")));
        int found = 0;
        while (q.next()) ++found;
        QCOMPARE(found, 5);
        check.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("mem_check"));
}

void MemoryDBTest::addAndListMemory()
{
    const int id = m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                        QStringLiteral("主人喜欢喝冰美式，不加糖"),
                                        QStringLiteral("preference")));
    QVERIFY(id > 0);
    QVERIFY(m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("九月约好一起去看海"),
                                 QStringLiteral("promise"))) > 0);

    const QList<MemoryRecord> all = m_db->listMemories(QStringLiteral("xiaoyue-"));
    QCOMPARE(all.size(), 2);
    QCOMPARE(m_db->memoryCount(QStringLiteral("xiaoyue-")), 2);

    MemoryRecord byId;
    QVERIFY(m_db->memoryById(id, &byId));
    QCOMPARE(byId.content, QStringLiteral("主人喜欢喝冰美式，不加糖"));
    QCOMPARE(byId.kind, QStringLiteral("preference"));
}

void MemoryDBTest::searchMatchesThreeCharAndTwoCharQueries()
{
    m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                         QStringLiteral("主人喜欢喝冰美式，不加糖"),
                         QStringLiteral("preference")));
    m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                         QStringLiteral("主人最近在追一部叫葬送的芙莉莲的动画"),
                         QStringLiteral("event")));

    // 三字词走 FTS5 trigram
    const auto coffee = m_db->searchMemories(QStringLiteral("xiaoyue-"),
                                             QStringLiteral("冰美式"));
    QCOMPARE(coffee.size(), 1);
    QVERIFY(coffee.first().content.contains(QStringLiteral("冰美式")));

    // 两字词 FTS trigram 覆盖不到，靠 LIKE 兜底
    const auto keyword = m_db->searchMemories(QStringLiteral("xiaoyue-"),
                                              QStringLiteral("美式"));
    QCOMPARE(keyword.size(), 1);
    QVERIFY(keyword.first().content.contains(QStringLiteral("冰美式")));

    // 无命中返回空
    QVERIFY(m_db->searchMemories(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("量子力学")).isEmpty());
}

void MemoryDBTest::updateAndDeleteKeepFtsInSync()
{
    const int id = m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                        QStringLiteral("主人喜欢喝冰美式，不加糖"),
                                        QStringLiteral("preference")));
    QVERIFY(id > 0);
    QCOMPARE(m_db->searchMemories(QStringLiteral("xiaoyue-"),
                                  QStringLiteral("冰美式")).size(), 1);

    MemoryRecord rec;
    QVERIFY(m_db->memoryById(id, &rec));
    rec.content = QStringLiteral("主人现在改喝热拿铁，少糖");
    QVERIFY(m_db->updateMemory(rec));

    QVERIFY(m_db->searchMemories(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("冰美式")).isEmpty());
    QCOMPARE(m_db->searchMemories(QStringLiteral("xiaoyue-"),
                                  QStringLiteral("热拿铁")).size(), 1);

    QVERIFY(m_db->deleteMemory(id));
    QVERIFY(m_db->searchMemories(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("热拿铁")).isEmpty());
}

void MemoryDBTest::fixedMemoriesIncludeSharedProfile()
{
    m_db->addMemory(make(QStringLiteral("*"),
                         QStringLiteral("主人的生日是 3 月 14 日"),
                         QStringLiteral("profile"), QStringLiteral("主人"), 5));
    m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                         QStringLiteral("九月约好一起去看海"),
                         QStringLiteral("promise"), QStringLiteral("主人"), 4));
    m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                         QStringLiteral("主人昨天随口提了一句天气不错"),
                         QStringLiteral("event"), QStringLiteral("主人"), 1));

    const QList<MemoryRecord> fixed = m_db->fixedMemories(QStringLiteral("xiaoyue-"));
    QCOMPARE(fixed.size(), 2);
    bool hasShared = false;
    bool hasPromise = false;
    for (const MemoryRecord &m : fixed) {
        if (m.characterName == QStringLiteral("*")) hasShared = true;
        if (m.kind == QStringLiteral("promise")) hasPromise = true;
    }
    QVERIFY(hasShared);
    QVERIFY(hasPromise);
}

void MemoryDBTest::characterScopeIsolation()
{
    m_db->addMemory(make(QStringLiteral("*"),
                         QStringLiteral("主人的生日是 3 月 14 日"),
                         QStringLiteral("profile"), QStringLiteral("主人"), 5));
    m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                         QStringLiteral("和 xiaoyue- 约好周末看电影"),
                         QStringLiteral("promise")));

    // 搜其他角色时看不到 xiaoyue- 的专属回忆
    const auto other = m_db->searchMemories(QStringLiteral("Hiyori"),
                                            QStringLiteral("周末看电影"));
    QVERIFY(other.isEmpty());

    // 但共享画像对所有角色可见
    const auto shared = m_db->listMemories(QStringLiteral("Hiyori"));
    QCOMPARE(shared.size(), 1);
    QCOMPARE(shared.first().characterName, QStringLiteral("*"));
}

void MemoryDBTest::clearMemoriesKeepsSharedProfile()
{
    m_db->addMemory(make(QStringLiteral("*"),
                         QStringLiteral("主人的生日是 3 月 14 日"),
                         QStringLiteral("profile"), QStringLiteral("主人"), 5));
    m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                         QStringLiteral("九月约好一起去看海"),
                         QStringLiteral("promise")));
    m_db->addMemory(make(QStringLiteral("Hiyori"),
                         QStringLiteral("和 Hiyori 约好一起打游戏"),
                         QStringLiteral("promise")));

    QCOMPARE(m_db->clearMemories(QStringLiteral("xiaoyue-")), 1);
    QCOMPARE(m_db->memoryCount(QStringLiteral("xiaoyue-")), 1); // 只剩共享
    QCOMPARE(m_db->memoryCount(QStringLiteral("Hiyori")), 2);   // 共享 + 自己的

    QCOMPARE(m_db->clearMemories(QString()), 2);
    QCOMPARE(m_db->memoryCount(QString()), 0);
}

void MemoryDBTest::recentAndBeforeMessages()
{
    QVERIFY(m_db->saveMessage(QStringLiteral("xiaoyue-"), QStringLiteral("user"),
                              QStringLiteral("第一条")));
    QVERIFY(m_db->saveMessage(QStringLiteral("xiaoyue-"), QStringLiteral("assistant"),
                              QStringLiteral("第二条")));
    QVERIFY(m_db->saveMessage(QStringLiteral("xiaoyue-"), QStringLiteral("user"),
                              QStringLiteral("第三条")));
    QVERIFY(m_db->saveMessage(QStringLiteral("Hiyori"), QStringLiteral("user"),
                              QStringLiteral("别的角色")));

    const QList<StoredMessage> recent =
        m_db->loadRecentMessages(QStringLiteral("xiaoyue-"), 2);
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent.first().content, QStringLiteral("第二条"));
    QCOMPARE(recent.last().content, QStringLiteral("第三条"));

    const QList<StoredMessage> before =
        m_db->loadMessagesBefore(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("2999-01-01T00:00:00"));
    QCOMPARE(before.size(), 3);
    QCOMPARE(before.first().content, QStringLiteral("第一条"));
}

void MemoryDBTest::loadMessagesRangeHonorsBounds()
{
    // 直接用第二条连接写入指定时间，验证 [after, before) 边界
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                    QStringLiteral("mem_range"));
        db.setDatabaseName(m_path);
        QVERIFY(db.open());
        QSqlQuery q(db);
        const QList<QPair<QString, QString>> rows = {
            {QStringLiteral("2026-08-01T10:00:00"), QStringLiteral("八月初")},
            {QStringLiteral("2026-08-20T10:00:00"), QStringLiteral("八月中")},
            {QStringLiteral("2026-09-01T00:00:00"), QStringLiteral("九月零点")},
            {QStringLiteral("2026-09-15T10:00:00"), QStringLiteral("九月中")},
        };
        for (const auto &row : rows) {
            q.prepare(QStringLiteral(
                "INSERT INTO messages (character_name, role, content, created_at) "
                "VALUES ('xiaoyue-', 'user', :c, :t)"));
            q.bindValue(QStringLiteral(":c"), row.second);
            q.bindValue(QStringLiteral(":t"), row.first);
            QVERIFY(q.exec());
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("mem_range"));

    const auto middle = m_db->loadMessagesRange(
        QStringLiteral("xiaoyue-"), QStringLiteral("2026-08-15T00:00:00"),
        QStringLiteral("2026-09-01T00:00:00"));
    QCOMPARE(middle.size(), 1);
    QCOMPARE(middle.first().content, QStringLiteral("八月中"));

    const auto earliest = m_db->loadMessagesRange(
        QStringLiteral("xiaoyue-"), QString(),
        QStringLiteral("2026-09-01T00:00:00"));
    QCOMPARE(earliest.size(), 2);
    QCOMPARE(earliest.first().content, QStringLiteral("八月初"));

    const auto september = m_db->loadMessagesRange(
        QStringLiteral("xiaoyue-"), QStringLiteral("2026-09-01T00:00:00"),
        QStringLiteral("2026-10-01T00:00:00"));
    QCOMPARE(september.size(), 2);
    QCOMPARE(september.first().content, QStringLiteral("九月零点"));
    QCOMPARE(september.last().content, QStringLiteral("九月中"));
}

void MemoryDBTest::memoriesByKindFiltersKindAndCharacter()
{
    QVERIFY(m_db->addMemory(make(QStringLiteral("*"),
                                 QStringLiteral("主人答应每天和宠物视频"),
                                 QStringLiteral("promise"), QStringLiteral("主人"), 2)) > 0);
    QVERIFY(m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("主人答应永远只爱 neko"),
                                 QStringLiteral("promise"), QStringLiteral("主人"), 3)) > 0);
    QVERIFY(m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("主人昨天加班到很晚"),
                                 QStringLiteral("event"), QStringLiteral("主人"), 5)) > 0);
    QVERIFY(m_db->addMemory(make(QStringLiteral("Hiyori"),
                                 QStringLiteral("和 Hiyori 约好周末一起打游戏"),
                                 QStringLiteral("promise"), QStringLiteral("主人"), 5)) > 0);

    const QList<MemoryRecord> promises =
        m_db->memoriesByKind(QStringLiteral("xiaoyue-"), QStringLiteral("promise"));
    QCOMPARE(promises.size(), 2); // 共享('*') + 本角色，不含 Hiyori，也不含 event
    for (const MemoryRecord &m : promises) {
        QCOMPARE(m.kind, QStringLiteral("promise"));
        QVERIFY(m.characterName != QStringLiteral("Hiyori"));
    }

    // kind 为空 → 空结果，不误伤成全量
    QVERIFY(m_db->memoriesByKind(QStringLiteral("xiaoyue-"), QString()).isEmpty());
    QVERIFY(m_db->memoriesByKind(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("  ")).isEmpty());

    // 没有对应 kind 时返回空
    QVERIFY(m_db->memoriesByKind(QStringLiteral("xiaoyue-"),
                                 QStringLiteral("relationship")).isEmpty());
}

void MemoryDBTest::memoriesByKindOrdersPinnedThenImportance()
{
    const int low = m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                         QStringLiteral("主人答应少喝奶茶"),
                                         QStringLiteral("promise"), QStringLiteral("主人"), 1));
    const int high = m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                          QStringLiteral("主人答应永远只爱 neko"),
                                          QStringLiteral("promise"), QStringLiteral("主人"), 4));
    const int pinned = m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                            QStringLiteral("主人答应一起睡"),
                                            QStringLiteral("promise"), QStringLiteral("主人"), 1));
    QVERIFY(low > 0 && high > 0 && pinned > 0);
    QVERIFY(m_db->setMemoryStatus(pinned, QStringLiteral("pinned")));

    const QList<MemoryRecord> got =
        m_db->memoriesByKind(QStringLiteral("xiaoyue-"), QStringLiteral("promise"));
    QCOMPARE(got.size(), 3);
    QCOMPARE(got.at(0).id, pinned); // pinned 优先
    QCOMPARE(got.at(1).id, high);   // 其次 importance 高
    QCOMPARE(got.at(2).id, low);
}

void MemoryDBTest::memoriesByKindSkipsArchivedAndHonorsLimit()
{
    QList<int> ids;
    for (int i = 0; i < 4; ++i) {
        ids.append(m_db->addMemory(make(QStringLiteral("xiaoyue-"),
                                        QStringLiteral("主人答应第 %1 件事").arg(i),
                                        QStringLiteral("promise"), QStringLiteral("主人"),
                                        3 - i % 2)));
        QVERIFY(ids.last() > 0);
    }
    QVERIFY(m_db->setMemoryStatus(ids.at(0), QStringLiteral("archived")));

    const QList<MemoryRecord> all =
        m_db->memoriesByKind(QStringLiteral("xiaoyue-"), QStringLiteral("promise"));
    QCOMPARE(all.size(), 3); // archived 不出现
    for (const MemoryRecord &m : all) QVERIFY(m.id != ids.at(0));

    const QList<MemoryRecord> limited =
        m_db->memoriesByKind(QStringLiteral("xiaoyue-"), QStringLiteral("promise"), 2);
    QCOMPARE(limited.size(), 2);
}

QTEST_GUILESS_MAIN(MemoryDBTest)
#include "MemoryDBTest.moc"
