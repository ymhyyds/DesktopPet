#include <QtTest>
#include <QStringList>
#include <QRandomGenerator>

// ============================================================================
// ExpressionManagerTest — Live2D 预制表情选择逻辑纯逻辑测试
//
// 测试 ModelController 中的表情选择逻辑（startRandomExpression 的核心算法），
// 不依赖 Cubism SDK、OpenGL、窗口或真实模型。
//
// 测试策略：
//   使用一个纯逻辑辅助类模拟 ModelController 中表情选择相关的逻辑：
//   - 表情名称列表管理（成功加载 vs 加载失败）
//   - 随机选择带连续去重
//   - 索引/名称越界安全
// ============================================================================

// ── 纯逻辑辅助类：模拟 ModelController 的表情选择逻辑 ─────────────────
class ExpressionSelector
{
public:
    QStringList names;         // 成功加载的表情名称
    int lastIndex = -1;        // 上次选中的索引

    bool hasExpressions() const { return !names.isEmpty(); }
    int count() const { return names.size(); }

    /// 模拟 startRandomExpression 的选号逻辑（纯选择，不依赖 _expressions 映射）
    int pickRandomIndex()
    {
        const int n = names.size();
        if (n == 0) return -1;

        int pick;
        if (n == 1) {
            pick = 0;
        } else {
            do {
                pick = QRandomGenerator::global()->bounded(n);
            } while (pick == lastIndex);
        }

        lastIndex = pick;
        return pick;
    }

    /// 模拟 startExpressionByIndex（仅边界检查，不涉及 _expressions 映射）
    bool isValidIndex(int index) const
    {
        return index >= 0 && index < names.size();
    }

    /// 模拟 startExpressionByName
    int indexOfName(const QString &name) const
    {
        return names.indexOf(name);
    }
};

// ============================================================================
// 测试用例
// ============================================================================

class ExpressionManagerTest : public QObject
{
    Q_OBJECT

private slots:
    // ── 0 个可用表情 ──
    void test_noExpressions_returnsFalse();
    void test_noExpressions_noAccessViolation();

    // ── 1 个可用表情 ──
    void test_singleExpression_alwaysValid();
    void test_singleExpression_repeatAllowed();

    // ── 多个可用表情 ──
    void test_multiExpression_inRange();
    void test_multiExpression_noConsecutiveRepeat();
    void test_multiExpression_convergesToUniform();

    // ── 上一次索引非法时 ──
    void test_lastIndexInvalid_stillSelects();

    // ── 索引/名称边界 ──
    void test_indexOutOfBounds_returnsFalse();
    void test_nameNotFound_returnsFalse();

    // ── 加载失败过滤 ──
    void test_loadFailures_skippedInList();

    // ── 空模型兼容 ──
    void test_emptyModel_noCrash();
};

// ============================================================================
// 实现
// ============================================================================

void ExpressionManagerTest::test_noExpressions_returnsFalse()
{
    ExpressionSelector sel;
    QCOMPARE(sel.hasExpressions(), false);
    QCOMPARE(sel.count(), 0);

    const int pick = sel.pickRandomIndex();
    QCOMPARE(pick, -1);
}

void ExpressionManagerTest::test_noExpressions_noAccessViolation()
{
    ExpressionSelector sel;
    // 多次调用空列表，不应崩溃
    for (int i = 0; i < 100; ++i) {
        const int pick = sel.pickRandomIndex();
        QCOMPARE(pick, -1);
    }
}

void ExpressionManagerTest::test_singleExpression_alwaysValid()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("爱心眼");

    // 无论选多少次，始终返回索引 0
    for (int i = 0; i < 50; ++i) {
        const int pick = sel.pickRandomIndex();
        QCOMPARE(pick, 0);
        QCOMPARE(sel.isValidIndex(pick), true);
    }
}

void ExpressionManagerTest::test_singleExpression_repeatAllowed()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("脸红");

    // 只有一个表情时，允许连续选中（不陷入死循环）
    int prev = -2;
    for (int i = 0; i < 20; ++i) {
        const int pick = sel.pickRandomIndex();
        QCOMPARE(pick, 0);
        // 因为只有一个，前后相同是允许的
        prev = pick;
    }
}

void ExpressionManagerTest::test_multiExpression_inRange()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("爱心眼") << QStringLiteral("黑脸")
              << QStringLiteral("脸红") << QStringLiteral("星星眼")
              << QStringLiteral("晕晕眼");

    QCOMPARE(sel.count(), 5);

    for (int i = 0; i < 200; ++i) {
        const int pick = sel.pickRandomIndex();
        QVERIFY2(pick >= 0 && pick < 5,
                 qPrintable(QStringLiteral("索引越界: %1").arg(pick)));
    }
}

void ExpressionManagerTest::test_multiExpression_noConsecutiveRepeat()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("爱心眼") << QStringLiteral("黑脸")
              << QStringLiteral("脸红") << QStringLiteral("星星眼")
              << QStringLiteral("晕晕眼");

    // 连续 100 次选择，检查不出现连续相同
    int prev = -1;
    int consecutiveSame = 0;
    for (int i = 0; i < 100; ++i) {
        const int pick = sel.pickRandomIndex();
        if (pick == prev) {
            ++consecutiveSame;
        }
        prev = pick;
    }

    // 在 5 个表情中随机选 100 次，不应出现连续相同（去重逻辑保证）
    // 但由于 QRandomGenerator 是伪随机，理论上可能极小概率出现极端情况，
    // 所以这里使用宽松的统计断言
    QVERIFY2(consecutiveSame == 0,
             qPrintable(QStringLiteral("出现 %1 次连续相同表情（去重失败）").arg(consecutiveSame)));
}

void ExpressionManagerTest::test_multiExpression_convergesToUniform()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("A") << QStringLiteral("B")
              << QStringLiteral("C") << QStringLiteral("D");

    // 遍历所有可能的 lastIndex 起始值
    for (int start = -1; start < 4; ++start) {
        sel.lastIndex = start;
        QSet<int> seen;
        // 最多尝试 count*2 次应遍历所有合法索引
        for (int attempt = 0; attempt < 20; ++attempt) {
            const int pick = sel.pickRandomIndex();
            QVERIFY(pick >= 0 && pick < 4);
            seen.insert(pick);
        }
        // 即使从某个特定 lastIndex 开始，经过足够次数也应覆盖全部
        // 注意：由于去重，第一次不会选到 lastIndex，但后面可能选到
        QCOMPARE(seen.size(), 4);
    }
}

void ExpressionManagerTest::test_lastIndexInvalid_stillSelects()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("A") << QStringLiteral("B") << QStringLiteral("C");
    sel.lastIndex = -1; // 从未播放

    // 从非法 lastIndex 开始，应能正常选择
    const int pick = sel.pickRandomIndex();
    QVERIFY(pick >= 0 && pick < 3);

    sel.lastIndex = 999; // 非法索引
    const int pick2 = sel.pickRandomIndex();
    QVERIFY(pick2 >= 0 && pick2 < 3);
}

void ExpressionManagerTest::test_indexOutOfBounds_returnsFalse()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("A") << QStringLiteral("B");

    QCOMPARE(sel.isValidIndex(-1), false);
    QCOMPARE(sel.isValidIndex(2), false);
    QCOMPARE(sel.isValidIndex(100), false);
    QCOMPARE(sel.isValidIndex(0), true);
    QCOMPARE(sel.isValidIndex(1), true);
}

void ExpressionManagerTest::test_nameNotFound_returnsFalse()
{
    ExpressionSelector sel;
    sel.names << QStringLiteral("爱心眼") << QStringLiteral("黑脸");

    QCOMPARE(sel.indexOfName(QStringLiteral("爱心眼")), 0);
    QCOMPARE(sel.indexOfName(QStringLiteral("黑脸")), 1);
    QCOMPARE(sel.indexOfName(QStringLiteral("不存在的表情")), -1);
    QCOMPARE(sel.indexOfName(QString()), -1);
}

void ExpressionManagerTest::test_loadFailures_skippedInList()
{
    // 模拟：有 5 个 exp3.json 文件，但 2 个加载失败
    // 只有成功加载的进入 names 列表
    ExpressionSelector sel;
    sel.names << QStringLiteral("exp_01") << QStringLiteral("exp_03")
              << QStringLiteral("exp_05"); // exp_02, exp_04 加载失败被跳过

    QCOMPARE(sel.count(), 3);
    QCOMPARE(sel.hasExpressions(), true);

    // 选择应始终在合法范围内
    for (int i = 0; i < 50; ++i) {
        const int pick = sel.pickRandomIndex();
        QVERIFY(pick >= 0 && pick < 3);
    }

    // 按名称寻址，失败的名称不应存在
    QCOMPARE(sel.indexOfName(QStringLiteral("exp_02")), -1);
    QCOMPARE(sel.indexOfName(QStringLiteral("exp_04")), -1);
    QCOMPARE(sel.indexOfName(QStringLiteral("exp_01")), 0);
    QCOMPARE(sel.indexOfName(QStringLiteral("exp_03")), 1);
    QCOMPARE(sel.indexOfName(QStringLiteral("exp_05")), 2);
}

void ExpressionManagerTest::test_emptyModel_noCrash()
{
    // 模拟没有 Expressions 的角色（如 Hiyori、Wanko）
    ExpressionSelector sel;
    QCOMPARE(sel.hasExpressions(), false);
    QCOMPARE(sel.count(), 0);
    QCOMPARE(sel.pickRandomIndex(), -1);
    QCOMPARE(sel.isValidIndex(0), false);
    QCOMPARE(sel.indexOfName(QStringLiteral("anything")), -1);
}

QTEST_MAIN(ExpressionManagerTest)
#include "ExpressionManagerTest.moc"
