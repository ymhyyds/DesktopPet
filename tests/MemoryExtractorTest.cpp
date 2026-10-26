// MemoryExtractor 纯逻辑测试：JSON 容错解析、bigram 相似度、关键词推导。

#include <QtTest>

#include "memory/MemoryExtractor.h"

class MemoryExtractorTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesPlainArray();
    void parsesFencedJsonWithProse();
    void skipsInvalidItemsAndClampsValues();
    void rejectsGarbage();
    void similarityBasics();
    void keywordDerivation();
    void attributionAcceptsQuotedUserText();
    void attributionRejectsAssistantProposal();
    void attributionFallsBackToBigramOverlap();
    void attributionNeedsUserText();
};

void MemoryExtractorTest::parsesPlainArray()
{
    const QString raw = QStringLiteral(
        "[{\"kind\":\"preference\",\"subject\":\"主人\","
        "\"content\":\"主人喜欢喝冰美式，不加糖\",\"keywords\":\"咖啡 美式\","
        "\"importance\":4,\"action\":\"add\"}]");
    const QList<MemoryRecord> items = MemoryExtractor::parseExtractionJson(raw);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().kind, QStringLiteral("preference"));
    QCOMPARE(items.first().subject, QStringLiteral("主人"));
    QCOMPARE(items.first().importance, 4);
    QCOMPARE(items.first().keywords, QStringLiteral("咖啡 美式"));
    QCOMPARE(items.first().action, QStringLiteral("add"));
}

void MemoryExtractorTest::parsesFencedJsonWithProse()
{
    const QString raw = QStringLiteral(
        "好的，结果如下：\n```json\n"
        "[{\"kind\":\"event\",\"subject\":\"主人\",\"content\":\"九月约好一起去看海\","
        "\"importance\":3,\"action\":\"update\",\"id\":12}]\n"
        "```\n以上。");
    const QList<MemoryRecord> items = MemoryExtractor::parseExtractionJson(raw);
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().action, QStringLiteral("update"));
    QCOMPARE(items.first().id, 12);
    QCOMPARE(items.first().content, QStringLiteral("九月约好一起去看海"));
}

void MemoryExtractorTest::skipsInvalidItemsAndClampsValues()
{
    const QString raw = QStringLiteral(
        "[{\"content\":\"\"},"
        "{\"kind\":\"unknown\",\"content\":\"主人每周三晚上有课\",\"importance\":99},"
        "{\"kind\":\"preference\",\"content\":\"主人不吃香菜\",\"importance\":-3,"
        "\"keywords\":[\"香菜\",\"口味\"],\"action\":\"weird\"}]");
    const QList<MemoryRecord> items = MemoryExtractor::parseExtractionJson(raw);
    QCOMPARE(items.size(), 2);

    QCOMPARE(items.at(0).kind, QStringLiteral("event")); // 非法 kind 归一
    QCOMPARE(items.at(0).importance, 5);                 // clamp 1~5

    QCOMPARE(items.at(1).kind, QStringLiteral("preference"));
    QCOMPARE(items.at(1).importance, 1);
    QCOMPARE(items.at(1).keywords, QStringLiteral("香菜 口味"));
    QCOMPARE(items.at(1).action, QStringLiteral("add")); // 非法 action 归一
}

void MemoryExtractorTest::rejectsGarbage()
{
    QVERIFY(MemoryExtractor::parseExtractionJson(QStringLiteral("没有 JSON")).isEmpty());
    QVERIFY(MemoryExtractor::parseExtractionJson(QStringLiteral("[{坏掉的}]")).isEmpty());
    QVERIFY(MemoryExtractor::parseExtractionJson(QStringLiteral("[]")).isEmpty());
}

void MemoryExtractorTest::similarityBasics()
{
    QCOMPARE(MemoryExtractor::similarity(QStringLiteral("主人喜欢喝冰美式"),
                                         QStringLiteral("主人喜欢喝冰美式")),
             1.0);
    const double close = MemoryExtractor::similarity(
        QStringLiteral("主人喜欢喝冰美式，不加糖"),
        QStringLiteral("主人喜欢喝冰美式，不加糖。"));
    QVERIFY(close > 0.9);
    const double far = MemoryExtractor::similarity(
        QStringLiteral("主人喜欢喝冰美式"),
        QStringLiteral("九月约定去看海"));
    QVERIFY(far < 0.2);
}

void MemoryExtractorTest::keywordDerivation()
{
    const QString kw = MemoryExtractor::deriveKeywords(
        QStringLiteral("主人喜欢喝冰美式，不加糖"));
    QVERIFY(!kw.isEmpty());
    QVERIFY(kw.split(QLatin1Char(' ')).size() >= 2);
    QVERIFY(kw.contains(QStringLiteral("冰美")));
}

void MemoryExtractorTest::attributionAcceptsQuotedUserText()
{
    MemoryRecord m;
    m.kind = QStringLiteral("promise");
    m.content = QStringLiteral("主人答应周末陪 neko 看电影");
    m.evidence = QStringLiteral("周末陪你看电影");

    // 原话在主人发言里 → 通过；evidence 的空白/标点/大小写差异不算差异
    QVERIFY(MemoryExtractor::hasUserEvidence(
        m, QStringLiteral("主人：那好吧，周末陪你看电影，说定了")));
    m.evidence = QStringLiteral("周末 陪你看电影！");
    QVERIFY(MemoryExtractor::hasUserEvidence(
        m, QStringLiteral("主人：那好吧，周末陪你看电影，说定了")));
}

void MemoryExtractorTest::attributionRejectsAssistantProposal()
{
    // 实测出现过的那张假卡：助手提议“每天早安抱抱”，被抽成“主人答应”
    const QString userText = QStringLiteral(
        "主人：今天好累啊\n主人：你抱抱我就好\n主人：晚安");

    MemoryRecord m;
    m.kind = QStringLiteral("promise");
    m.content = QStringLiteral("主人答应每天摸头、给早安抱抱，并让对方在床边陪睡");
    m.evidence = QStringLiteral("每天都要早安抱抱");
    QVERIFY(!MemoryExtractor::hasUserEvidence(m, userText));

    // 模型没给 evidence 时，promise 一律不算有依据（不靠退化判定兜底，
    // 否则“主人”“抱抱”这类两字词就能凑够门槛）
    m.evidence.clear();
    QVERIFY(!MemoryExtractor::hasUserEvidence(m, userText));

    // 模型编造 evidence 也一样拦下
    m.evidence = QStringLiteral("我答应你每天早上都抱抱");
    QVERIFY(!MemoryExtractor::hasUserEvidence(m, userText));
}

void MemoryExtractorTest::attributionFallsBackToBigramOverlap()
{
    const QString userText = QStringLiteral("主人：什么时候一起去海边玩呀");

    MemoryRecord m;
    m.kind = QStringLiteral("event");
    m.content = QStringLiteral("主人说想一起去海边");
    QVERIFY(MemoryExtractor::hasUserEvidence(m, userText)); // 连续重合“一起去海边”

    m.content = QStringLiteral("主人答应每天写作业");
    QVERIFY(!MemoryExtractor::hasUserEvidence(m, userText)); // 只有零散两字词，不算

    // 单字 evidence 长度不足 2，同样走 event 的退化分支
    m.content = QStringLiteral("主人说想一起去海边");
    m.evidence = QStringLiteral("海");
    QVERIFY(MemoryExtractor::hasUserEvidence(m, userText));

    // 同样的内容换成 promise 且没有 evidence → 丢
    m.evidence.clear();
    m.kind = QStringLiteral("promise");
    QVERIFY(!MemoryExtractor::hasUserEvidence(m, userText));
}

void MemoryExtractorTest::attributionNeedsUserText()
{
    MemoryRecord m;
    m.kind = QStringLiteral("promise");
    m.content = QStringLiteral("主人答应每天摸头");
    m.evidence = QStringLiteral("每天摸头");

    // 本请求没有主人发言时，promise/event 一律不可信
    QVERIFY(!MemoryExtractor::hasUserEvidence(m, QString()));
    QVERIFY(!MemoryExtractor::hasUserEvidence(m, QStringLiteral("   \n ")));
}

QTEST_GUILESS_MAIN(MemoryExtractorTest)
#include "MemoryExtractorTest.moc"
