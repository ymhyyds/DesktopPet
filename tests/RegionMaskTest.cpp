// ── RegionMaskTest.cpp ──
// 纯函数测试：验证 PetWindow-local ↔ proxy-local 区域转换逻辑。
// 不依赖窗口系统、OpenGL 或 Live2D 模型。
#include <QtTest>
#include <QPoint>
#include <QRegion>

#include "../interaction/DragAnchorHelpers.h"

class RegionMaskTest : public QObject
{
    Q_OBJECT

private slots:

    // ═══════════════════════════════════════════════════════════
    // 1. 无裁剪：完整区域保持一致
    // ═══════════════════════════════════════════════════════════
    void test_noClip_identity()
    {
        // PetWindow = proxy，无偏移，无裁剪
        QRegion petRegion(QRect(50, 50, 100, 200));
        QRegion proxyMask = DeriveProxyMask(petRegion, 0, 0, QSize(400, 600));

        QCOMPARE(proxyMask, petRegion);

        // 也满足 fits-check
        QVERIFY(PetRegionFitsProxy(petRegion, 0, 0, QSize(400, 600)));
    }

    // ═══════════════════════════════════════════════════════════
    // 2. 底部裁剪一半（模拟模型下移超出屏幕）
    //    场景：PetWindow 下半身出屏幕，proxy 从底部变短
    //    offsetY=0，proxy 高度从 600 → 300
    //    canonical region 上半部分仍正确保留
    // ═══════════════════════════════════════════════════════════
    void test_bottomClip_half()
    {
        // PetWindow 高度 600，模型占据上半部分 y=50~250
        QRegion petRegion(QRect(50, 50, 100, 200));

        // proxy 被从底部截短到高度 300（模拟屏幕裁剪底部）
        // offsetY=0（顶部对齐）
        QRegion proxyMask = DeriveProxyMask(petRegion, 0, 0, QSize(400, 300));

        // 模型 y=50~250 在 proxy 高度 300 内 → 保留
        QVERIFY(proxyMask.contains(QPoint(100, 100)));
        QVERIFY(proxyMask.contains(QPoint(100, 200)));

        // 模型区域包含在 proxy 内
        QVERIFY(proxyMask == petRegion);

        // 全部 fits
        QVERIFY(PetRegionFitsProxy(petRegion, 0, 0, QSize(400, 300)));
    }

    // ═══════════════════════════════════════════════════════════
    // 3. 顶部裁剪一半（模拟模型上移超出屏幕）
    //    场景：PetWindow 上半身出屏幕，proxy 被从顶部截短
    //    offsetY>0，proxy 高度从 600 → 300
    //    canonical region 下半部分正确平移到 proxy-local
    // ═══════════════════════════════════════════════════════════
    void test_topClip_half()
    {
        // PetWindow 高度 600，模型占据下半部分 y=300~550
        QRegion petRegion(QRect(50, 300, 100, 250));

        // PetWindow 顶部超出屏幕 200px，proxy 下移到 y=0（屏幕顶部）
        // geoOffsetY = 0 - (-200) = 200
        // proxy 高度 = 400 (600 - 200)
        const int geoOffsetY = 200;
        QRegion proxyMask = DeriveProxyMask(petRegion, 0, geoOffsetY, QSize(400, 400));

        // PetWindow-local y=300 → proxy-local y = 300 - 200 = 100
        // petRegion y-range [300, 550) → proxy y-range [100, 350)
        // Last pixel in region: y=349
        QVERIFY(proxyMask.contains(QPoint(100, 100)));  // 模型顶部
        QVERIFY(proxyMask.contains(QPoint(100, 300)));  // 模型中部
        QVERIFY(proxyMask.contains(QPoint(100, 349)));  // 模型底部末像素

        // canonical petRegion 未被修改
        QVERIFY(petRegion.contains(QPoint(100, 300)));
        QVERIFY(petRegion.contains(QPoint(100, 549)));  // 区域末像素
    }

    // ═══════════════════════════════════════════════════════════
    // 4. 上下两个方向的屏幕内可见点都落在最终 mask
    // ═══════════════════════════════════════════════════════════
    void test_visiblePointsInMask()
    {
        // ── 向下越界：模型上半身在屏幕内 ──
        {
            // PetWindow 高度 600，模型 y=50~250（上半身可见）
            QRegion petRegion(QRect(0, 50, 200, 200));
            // proxy 底部被截掉 100px（屏幕裁剪），高度 500
            QRegion mask = DeriveProxyMask(petRegion, 0, 0, QSize(200, 500));
            // 模型 y=50~250 完全在 proxy y=0~500 内
            QVERIFY(mask.contains(QPoint(100, 100)));
            QVERIFY(mask.contains(QPoint(50, 200)));
            QVERIFY(!mask.contains(QPoint(100, 600))); // 屏幕外
        }

        // ── 向上越界：模型下半身在屏幕内 ──
        {
            // PetWindow 顶部 200px 超出屏幕，模型 y=300~550（下半身可见）
            QRegion petRegion(QRect(0, 300, 200, 250));
            int geoOffsetY = 200;
            QSize proxySize(200, 400);
            QRegion mask = DeriveProxyMask(petRegion, 0, geoOffsetY, proxySize);

            // petRegion y-range [300, 550) → proxy y-range [100, 350)
            // Last pixel in region: y=349
            QVERIFY(mask.contains(QPoint(100, 100)));  // 可见
            QVERIFY(mask.contains(QPoint(100, 349)));  // 可见（区域末像素）
            // proxy y=0 → PetWindow y=200（屏幕顶部），模型 y=300 > 200，所以在 mask 中
            // proxy y=50 → PetWindow y=250，不在模型中
            // 点在 PetRegion y=300-550 之间 → proxy y=100-350
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 5. 裁剪结果不得写回 canonical region
    // ═══════════════════════════════════════════════════════════
    void test_canonicalRegionNotOverwritten()
    {
        QRegion petRegion(QRect(50, 50, 200, 400));
        QRegion originalCopy = petRegion;

        // 模拟顶部裁剪
        QRegion proxyMask1 = DeriveProxyMask(petRegion, 0, 100, QSize(200, 300));
        Q_UNUSED(proxyMask1);
        // petRegion 不变
        QCOMPARE(petRegion, originalCopy);

        // 模拟底部裁剪
        QRegion proxyMask2 = DeriveProxyMask(petRegion, 0, 0, QSize(200, 200));
        Q_UNUSED(proxyMask2);
        QCOMPARE(petRegion, originalCopy);

        // 模拟两个方向连续裁剪
        for (int i = 0; i < 10; ++i) {
            QRegion m1 = DeriveProxyMask(petRegion, 0, 50, QSize(200, 300));
            QRegion m2 = DeriveProxyMask(petRegion, 0, 0, QSize(200, 500));
            Q_UNUSED(m1); Q_UNUSED(m2);
            // canonical region 每次调用后都不变
            QCOMPARE(petRegion, originalCopy);
        }
    }

    // ═══════════════════════════════════════════════════════════
    // 6. 连续顶部/底部裁剪切换 20 次无累计偏移
    // ═══════════════════════════════════════════════════════════
    void test_noAccumulatedOffset()
    {
        QRegion petRegion(QRect(100, 100, 200, 400));

        // 20 次 top/bottom 交替
        for (int i = 0; i < 20; ++i) {
            // 顶部裁剪
            QRegion topMask = DeriveProxyMask(petRegion, 0, 150, QSize(200, 300));
            Q_UNUSED(topMask);
            // 底部裁剪
            QRegion bottomMask = DeriveProxyMask(petRegion, 0, 0, QSize(200, 300));
            Q_UNUSED(bottomMask);
        }

        // 验证 canonical region 不变
        QCOMPARE(petRegion, QRegion(QRect(100, 100, 200, 400)));

        // 验证最终 mask 正确（proxy 宽度足够容纳 petRegion）
        QRegion finalMask = DeriveProxyMask(petRegion, 0, 0, QSize(300, 600));
        QCOMPARE(finalMask, petRegion);
    }

    // ═══════════════════════════════════════════════════════════
    // 7. 解除裁剪后完整 mask 恢复
    // ═══════════════════════════════════════════════════════════
    void test_unclip_restoresFullMask()
    {
        QRegion petRegion(QRect(0, 0, 400, 600));

        // 先裁剪顶部
        QRegion clippedUp = DeriveProxyMask(petRegion, 0, 200, QSize(400, 400));
        QVERIFY(clippedUp != petRegion);  // 被裁剪了

        // 解除裁剪
        QRegion fullMask = DeriveProxyMask(petRegion, 0, 0, QSize(400, 600));
        QCOMPARE(fullMask, petRegion);    // 完整恢复

        // 先裁剪底部
        QRegion clippedDown = DeriveProxyMask(petRegion, 0, 0, QSize(400, 300));
        QVERIFY(clippedDown != petRegion);

        // 解除
        QRegion fullMask2 = DeriveProxyMask(petRegion, 0, 0, QSize(400, 600));
        QCOMPARE(fullMask2, petRegion);
    }

    // ═══════════════════════════════════════════════════════════
    // 8. (Runtime test — needs Qt widget runtime)
    //    updateInputMask 使用完整 PetWindow/Live2DView 尺寸
    //    → 无法用纯函数测试
    // ═══════════════════════════════════════════════════════════

    // ═══════════════════════════════════════════════════════════
    // 9. proxy-local → pet-local 只加一次 offset（不累计）
    // ═══════════════════════════════════════════════════════════
    void test_offsetAppliedOnce()
    {
        const int geoOffsetX = 30;
        const int geoOffsetY = 200;
        QSize proxySize(400, 400);

        QRegion petRegion(QRect(50, 300, 100, 150));

        // 1. proxy-local → pet-local
        QPoint proxyPt(100, 100);
        QPoint petPt = ProxyToPetPoint(proxyPt, geoOffsetX, geoOffsetY);
        QCOMPARE(petPt, QPoint(130, 300));  // 加一次 offset

        // 2. pet-local 点在 petRegion 内
        QVERIFY(petRegion.contains(petPt));

        // 3. 再次调用 ProxyToPetPoint 不会二次累加
        QPoint petPt2 = ProxyToPetPoint(proxyPt, geoOffsetX, geoOffsetY);
        QCOMPARE(petPt2, petPt);
    }

    // ── 组合测试：多区域 + 偏移裁剪 ──
    void test_multiRectWithOffset()
    {
        // 模拟多个 drawable rect 组成的复杂区域
        QRegion petRegion;
        petRegion |= QRegion(QRect(50, 300, 100, 80));    // 身体
        petRegion |= QRegion(QRect(80, 200, 40, 100));    // 头部
        petRegion |= QRegion(QRect(30, 400, 140, 50));    // 裙摆

        QRegion originalCopy = petRegion;

        // 顶部裁剪（offsetY=150）
        QRegion mask = DeriveProxyMask(petRegion, 0, 150, QSize(200, 350));

        // 头部 y=200 → proxy y=50（在 mask 内）
        QVERIFY(mask.contains(QPoint(100, 50)));   // 头部在可见区域
        QVERIFY(mask.contains(QPoint(100, 229)));  // 身体在可见区域（末像素）
        // 裙摆 y=400 → proxy y=250（在 mask 内，高度 350）
        QVERIFY(mask.contains(QPoint(100, 250)));

        // canonical 不变
        QCOMPARE(petRegion, originalCopy);
    }

    // ── PetWindow-local 点通过 mask 检测 ──
    void test_petPointPassesMask()
    {
        // 模拟：模型在 PetWindow 下半部，上半部空白
        QRegion petRegion(QRect(50, 300, 200, 200));
        QRegion original = petRegion;

        // 向上越界：仅下半部可见
        // PetWindow-local y=300 → proxy-local y=200 (geoOffsetY=100)
        QRegion mask = DeriveProxyMask(petRegion, 0, 100, QSize(300, 400));

        // PetWindow-local 点 y=350 → proxy-local y=250 → 在 mask 内
        QPoint proxyPt(100, 250);                    // proxy-local
        QPoint petPt = ProxyToPetPoint(proxyPt, 0, 100);  // → PetWindow (100, 350)
        QVERIFY(petRegion.contains(petPt));          // 在 permanent region
        QVERIFY(mask.contains(proxyPt));              // 也在 proxy mask

        // PetWindow-local 点 y=150（空白上半部）→ proxy-local y=50
        // 不在 petRegion → 即便在 proxy mask 区域内也不交互
        QPoint proxyPt2(100, 50);
        QPoint petPt2 = ProxyToPetPoint(proxyPt2, 0, 100); // → PetWindow (100, 150)
        QVERIFY(!petRegion.contains(petPt2));        // 不在 permanent region

        // canonical 不变
        QCOMPARE(petRegion, original);
    }

    // ── X 方向偏移 ──
    void test_xOffset()
    {
        QRegion petRegion(QRect(100, 0, 200, 100));
        QRegion original = petRegion;

        // PetWindow 左边超出屏幕，proxy 右移
        QRegion mask = DeriveProxyMask(petRegion, 50, 0, QSize(250, 100));

        // PetWindow-local x=100 → proxy-local x=50
        QVERIFY(mask.contains(QPoint(50, 50)));

        // Canonical 不变
        QCOMPARE(petRegion, original);
    }

    // ════════════════════════════════════════════════════════════════
    // 基于实际 geometry 的 offset 测试
    // ════════════════════════════════════════════════════════════════

    // ── 顶部裁剪：使用实际 offset（actual proxy y=0, pet y=-300, offsetY=300）
    //    与使用错误 offset（如 280）得到的 mask 不同
    void test_topClip_actualVsRequestedOffset()
    {
        // 模型在 PetWindow 下半部分
        QRegion petRegion(QRect(50, 300, 100, 200));
        QRegion original = petRegion;

        // 实际 offset: proxy y=0, pet y=-300 → offsetY=300
        const int actualOffsetY = 300;
        const QSize proxySize(400, 300);  // actual proxy size

        // 使用实际 offset 派生 mask
        QRegion actualMask = DeriveProxyMask(petRegion, 0, actualOffsetY, proxySize);

        // 如果错误地使用请求的 offset（如 offsetY=280）
        const int requestedOffsetY = 280;
        QRegion wrongMask = DeriveProxyMask(petRegion, 0, requestedOffsetY, proxySize);

        // 两个 mask 不同（偏移量不同，裁剪位置不同）
        QVERIFY(actualMask != wrongMask);

        // 实际 offset: pet y=300 → proxy y=0 (刚好在 proxy 顶部可见)
        QVERIFY(actualMask.contains(QPoint(100, 0)));

        // 错误 offset: pet y=300 → proxy y=20 (偏移到了不同位置)
        // (可以包含但位置不同)

        // canonical 不变
        QCOMPARE(petRegion, original);
    }

    // ── 顶部裁剪：proxy 原点偏移后，pet-local 点正确落在对应的 proxy-local 位置
    void test_topClip_petPointCorrectlyProjected()
    {
        // PetWindow 尺寸 400x600，顶部 300px 超出屏幕
        // proxy 在 y=0, 高度 300
        const int geoOffsetY = 300;
        const QSize proxySize(400, 300);

        QRegion petRegion(QRect(50, 350, 100, 200));  // 模型 y=350~550
        QRegion original = petRegion;

        // 派生 mask
        QRegion mask = DeriveProxyMask(petRegion, 0, geoOffsetY, proxySize);

        // Pet y=350 → proxy y=50 (350-300)
        QVERIFY(mask.contains(QPoint(100, 50)));

        // Pet y=549 → proxy y=249 (549-300)
        QVERIFY(mask.contains(QPoint(100, 249)));

        // Pet y=300 → proxy y=0 (边缘)
        QVERIFY(!mask.contains(QPoint(100, -1)));  // 不在 proxy 内

        // canonical 不变
        QCOMPARE(petRegion, original);
    }

    // ── 底部裁剪：offsetY=0，proxy 顶部与 PetWindow 顶部对齐
    void test_bottomClip_actualOffsetZero()
    {
        // PetWindow y=500, proxy y=500, offsetY=0
        // 底部被裁剪，高度从 600 变为 300
        const int geoOffsetY = 0;
        const QSize proxySize(400, 300);  // 底部裁剪

        QRegion petRegion(QRect(50, 50, 100, 200));  // 模型在顶部
        QRegion original = petRegion;

        QRegion mask = DeriveProxyMask(petRegion, 0, geoOffsetY, proxySize);

        // 底部裁剪时，offsetY=0，模型区域完整保留
        QVERIFY(mask.contains(QPoint(100, 100)));
        QVERIFY(mask.contains(QPoint(100, 200)));

        // mask 等于 petRegion（区域完全在 proxy 内）
        QCOMPARE(mask, petRegion);

        QCOMPARE(petRegion, original);
    }

    // ── 顶部和底部裁剪反复切换，mask 使用各自的 offset，不互相干扰
    void test_alternatingTopBottomClips()
    {
        QRegion petRegion(QRect(50, 200, 100, 200));
        QRegion original = petRegion;

        // 顶部裁剪 3 次
        for (int i = 0; i < 3; ++i) {
            QRegion mask = DeriveProxyMask(petRegion, 0, 150, QSize(400, 450));
            // Pet y=200 → proxy y=50
            QVERIFY(mask.contains(QPoint(100, 50)));
            Q_UNUSED(mask);
        }

        // 底部裁剪 3 次
        for (int i = 0; i < 3; ++i) {
            QRegion mask = DeriveProxyMask(petRegion, 0, 0, QSize(400, 300));
            // offset=0, petRegion y=[200,400), proxy y=[0,300)
            // intersect: y=[200,300) → proxy y=[200,300)
            QVERIFY(mask.contains(QPoint(100, 250)));  // proxy y=250, pet y=250 → in region
            Q_UNUSED(mask);
        }

        // canonical 不变
        QCOMPARE(petRegion, original);
    }
};

QTEST_MAIN(RegionMaskTest)
#include "RegionMaskTest.moc"
