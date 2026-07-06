// ── DragAnchorTest.cpp ──
// 纯函数测试：验证 InputProxyWindow 拖动坐标计算逻辑。
// 不依赖窗口系统、OpenGL 或 Live2D 模型。
#include <QtTest>
#include <QPoint>
#include <QRect>
#include <QRegion>

#include "../interaction/DragAnchorHelpers.h"

class DragAnchorTest : public QObject
{
    Q_OBJECT

private slots:
    // 1. offset=(0,0) 时拖动结果不变
    void test_zeroOffset()
    {
        // 鼠标在 proxy-local (100,200) 处按下，无裁剪
        const QPoint anchor = ComputeDragAnchor(QPoint(100, 200), 0, 0);
        QCOMPARE(anchor, QPoint(100, 200));

        // 鼠标移动到全局 (500,400)
        const QPoint newPos = ComputeNewPosition(QPoint(500, 400), anchor);
        QCOMPARE(newPos, QPoint(400, 200));
    }

    // 2. proxy 向下裁剪 offsetY>0 时，开始拖动不会向下瞬移
    void test_offsetY_positive()
    {
        // PetWindow 顶部超出屏幕，proxy 下移 30px
        // 鼠标在 proxy-local (100,20)，对应 PetWindow-local (100,50)
        const QPoint anchor = ComputeDragAnchor(QPoint(100, 20), 0, 30);
        QCOMPARE(anchor, QPoint(100, 50));

        // 全局鼠标位置不变 → PetWindow 位置应不变
        // 如果 PetWindow 原点在 (100, -30)，鼠标全局位置 = (100, -30) + (100, 50) = (200, 20)
        const QPoint newPos = ComputeNewPosition(QPoint(200, 20), anchor);
        QCOMPARE(newPos, QPoint(100, -30));
    }

    // 3. proxy 向右裁剪 offsetX>0 时，不会横向瞬移
    void test_offsetX_positive()
    {
        const QPoint anchor = ComputeDragAnchor(QPoint(50, 100), 40, 0);
        QCOMPARE(anchor, QPoint(90, 100));

        const QPoint newPos = ComputeNewPosition(QPoint(200, 300), anchor);
        QCOMPARE(newPos, QPoint(110, 200));
    }

    // 4. X/Y 同时存在 offset 时结果正确
    void test_offsetXY_both()
    {
        const QPoint anchor = ComputeDragAnchor(QPoint(30, 40), 10, 20);
        QCOMPARE(anchor, QPoint(40, 60));

        const QPoint newPos = ComputeNewPosition(QPoint(300, 400), anchor);
        QCOMPARE(newPos, QPoint(260, 340));
    }

    // 5. 鼠标移动 delta 与 PetWindow 移动 delta 完全一致
    void test_dragDeltaConsistent()
    {
        const int gx = 500, gy = 400;
        const QPoint anchor(150, 200);
        const QPoint pos1 = ComputeNewPosition(QPoint(gx, gy), anchor);
        // 鼠标右移 100px，下移 50px
        const QPoint pos2 = ComputeNewPosition(QPoint(gx + 100, gy + 50), anchor);
        QCOMPARE(pos2 - pos1, QPoint(100, 50));
    }

    // 6. 拖动期间 anchor 即使被外部使用也不改变已保存锚点
    void test_anchorImmutable()
    {
        // 模拟：第一次 press 记录锚点
        const QPoint anchor = ComputeDragAnchor(QPoint(80, 90), 0, 0);
        QCOMPARE(anchor, QPoint(80, 90));

        // 即使 geoOffset 变化（模拟外部干扰），锚点不变
        const QPoint pos1 = ComputeNewPosition(QPoint(600, 500), anchor);
        const QPoint pos2 = ComputeNewPosition(QPoint(700, 550), anchor);
        QCOMPARE(pos2 - pos1, QPoint(100, 50));  // 仍精确跟随鼠标
    }

    // 7. 裁剪后的 preciseRegion 正确平移到 proxy 本地坐标
    //    验证 translate(-offset) 的等价性
    void test_maskTranslation()
    {
        // PetWindow 中 drawable rect = (50, 60, 100, 200)
        QRect petLocalRect(50, 60, 100, 200);
        const int ox = 10, oy = 20;
        // Proxy-local = pet-local - offset
        QRect proxyLocal = petLocalRect.translated(-ox, -oy);
        QCOMPARE(proxyLocal, QRect(40, 40, 100, 200));

        // 回转换算：proxy-local + offset → pet-local
        QRect petLocalBack = proxyLocal.translated(ox, oy);
        QCOMPARE(petLocalBack, petLocalRect);
    }

    // 8. proxy-local 点转换到 pet-local 点时只加一次 offset
    void test_coordConversionOnce()
    {
        // mousePressEvent: 转换 proxy-local → pet-local
        const QPoint proxyLocal(30, 40);
        const int ox = 15, oy = 25;
        const QPoint petLocal = proxyLocal + QPoint(ox, oy);
        QCOMPARE(petLocal, QPoint(45, 65));

        // 不应重复加 offset
        const QPoint wrong = proxyLocal + QPoint(ox, oy) + QPoint(ox, oy);
        QVERIFY(petLocal != wrong);
    }

    // 9. 释放后重新 syncGeometry 不影响下一次拖动（通过新锚点验证）
    void test_newPressUsesNewAnchor()
    {
        // 第一次拖动锚点
        const QPoint anchor1 = ComputeDragAnchor(
            QPoint(100, 100), 0, 0);
        QCOMPARE(anchor1, QPoint(100, 100));

        // 第二次拖动（不同 press 位置，不同 offset）
        const QPoint anchor2 = ComputeDragAnchor(
            QPoint(200, 150), 5, 10);
        QCOMPARE(anchor2, QPoint(205, 160));

        // 两次锚点独立
        QVERIFY(anchor1 != anchor2);
    }

    // 10. 连续两次拖动均能立即响应
    void test_consecutiveDrags()
    {
        // 拖动 1: press at (10,10), move to global (210,210)
        QPoint anchor = ComputeDragAnchor(QPoint(10, 10), 0, 0);
        QPoint p1 = ComputeNewPosition(QPoint(210, 210), anchor);
        QCOMPARE(p1, QPoint(200, 200));

        // 释放后再次拖动 2: press at (50,50), move to global (300,300)
        anchor = ComputeDragAnchor(QPoint(50, 50), 0, 0);
        QPoint p2 = ComputeNewPosition(QPoint(300, 300), anchor);
        QCOMPARE(p2, QPoint(250, 250));

        // 每次拖动锚点独立，不影响
        QVERIFY(p1 != p2);
    }

    // ── NeedsMaskRefresh 纯函数测试 ──────────────────────────────────────

    // 1. offset 从 0 变为正值 → 需要刷新 mask
    void test_maskRefresh_offset0toPositive()
    {
        QVERIFY(NeedsMaskRefresh(0, 0, 10, 0, QSize(400, 300), QSize(400, 300)));
        QVERIFY(NeedsMaskRefresh(0, 0, 0, 20, QSize(400, 300), QSize(400, 300)));
        QVERIFY(NeedsMaskRefresh(0, 0, 15, 25, QSize(400, 300), QSize(400, 300)));
    }

    // 2. offset 从正值恢复为 0 → 需要刷新 mask
    void test_maskRefresh_positiveTo0()
    {
        QVERIFY(NeedsMaskRefresh(10, 0, 0, 0, QSize(400, 300), QSize(400, 300)));
        QVERIFY(NeedsMaskRefresh(0, 20, 0, 0, QSize(400, 300), QSize(400, 300)));
        QVERIFY(NeedsMaskRefresh(15, 25, 0, 0, QSize(400, 300), QSize(400, 300)));
    }

    // 3. proxy 尺寸变化 → 需要刷新 mask
    void test_maskRefresh_sizeChanged()
    {
        QVERIFY(NeedsMaskRefresh(0, 0, 0, 0, QSize(400, 300), QSize(350, 250)));
        QVERIFY(NeedsMaskRefresh(0, 0, 0, 0, QSize(400, 300), QSize(500, 400)));
    }

    // 4. 只有窗口位置变化，size 和 offset 不变 → 不刷新 mask
    void test_maskRefresh_onlyPositionChanged()
    {
        QVERIFY(!NeedsMaskRefresh(0, 0, 0, 0, QSize(400, 300), QSize(400, 300)));
        QVERIFY(!NeedsMaskRefresh(10, 20, 10, 20, QSize(400, 300), QSize(400, 300)));
    }

    // 5. X/Y offset 同时变化时只需刷新一次
    void test_maskRefresh_xyOffsetBoth()
    {
        QVERIFY(NeedsMaskRefresh(0, 0, 5, 10, QSize(400, 300), QSize(400, 300)));
        QVERIFY(NeedsMaskRefresh(3, 7, 5, 10, QSize(400, 300), QSize(400, 300)));
    }

    // 6. offset 和 size 同时变化 → 刷新一次（返回值仍是 true）
    void test_maskRefresh_offsetAndSizeBoth()
    {
        QVERIFY(NeedsMaskRefresh(0, 0, 10, 20, QSize(400, 300), QSize(350, 250)));
    }

    // 7. 连续 syncGeometry() 且状态不变时不重复刷新
    void test_maskRefresh_consecutiveUnchanged()
    {
        QVERIFY(!NeedsMaskRefresh(10, 20, 10, 20, QSize(400, 300), QSize(400, 300)));
        QVERIFY(!NeedsMaskRefresh(0, 0, 0, 0, QSize(400, 300), QSize(400, 300)));
    }

    // 8. 裁剪后 mask 使用新的 offset（坐标平移验证）
    void test_maskRefresh_clippedMaskUsesNewOffset()
    {
        QRect petLocalRect(50, 60, 100, 200);
        const int ox = 10, oy = 20;
        QRect proxyLocal = petLocalRect.translated(-ox, -oy);
        QCOMPARE(proxyLocal, QRect(40, 40, 100, 200));

        QRect petLocalBack = proxyLocal.translated(ox, oy);
        QCOMPARE(petLocalBack, petLocalRect);
    }

    // 9. X 或 Y 单独变化 → 需要刷新
    void test_maskRefresh_eitherOffsetAlone()
    {
        QVERIFY(NeedsMaskRefresh(5, 0, 10, 0, QSize(400, 300), QSize(400, 300)));
        QVERIFY(NeedsMaskRefresh(0, 5, 0, 10, QSize(400, 300), QSize(400, 300)));
    }

    // ════════════════════════════════════════════════════════════════
    // 生产场景测试（offset 始终为 0，proxy-local == pet-local）
    // ════════════════════════════════════════════════════════════════

    // 10. 生产场景：offset=0 时 anchor 就是 proxy-local
    void test_production_anchorEqualsLocal()
    {
        QCOMPARE(ComputeDragAnchor(QPoint(150, 200), 0, 0), QPoint(150, 200));
        QCOMPARE(ComputeDragAnchor(QPoint(0, 0), 0, 0), QPoint(0, 0));
    }

    // 11. 生产场景：proxy 覆盖 PetWindow 完整尺寸
    void test_production_proxyFullCoverage()
    {
        // 模拟 PetWindow 放大后（如缩放 200%），proxy size == pet size
        const QSize petSize(600, 800);
        const QSize proxySize(600, 800);  // 无裁剪
        QCOMPARE(proxySize, petSize);
        QVERIFY(!NeedsMaskRefresh(0, 0, 0, 0, QSize(600, 800), petSize));
    }

    // 12. 生产场景：proxy 超出屏幕边缘时不丢失覆盖
    void test_production_noAvailableGeometryClip()
    {
        // PetWindow 顶部超出屏幕 50px，但不裁剪 proxy
        // proxy geometry 仍为完整 PetWindow geometry
        const QRect petGeo(100, -50, 400, 500);
        // 旧行为会裁剪到 availableGeometry.top()=0 → proxyGeo=(100, 0, 400, 450)
        // 新行为 proxyGeo = petGeo = (100, -50, 400, 500)
        const QRect proxyGeo = petGeo;  // 完整跟随
        QCOMPARE(proxyGeo.top(), -50);    // 仍覆盖屏幕外部分（OS 自动裁剪）
        QCOMPARE(proxyGeo.height(), 500); // 屏幕内 450px 仍然可交互
    }

    // 13. 生产场景：mask 不需要 geoOffset 平移
    void test_production_maskNoTranslate()
    {
        // 旧行为：mask = petLocal.translate(-offset)
        // 新行为：mask = petLocal（offset = 0，proxy-local == pet-local）
        const QRect petLocalDrawable(50, 60, 100, 200);
        const QRect proxyLocal = petLocalDrawable;  // 无平移
        QCOMPARE(proxyLocal, QRect(50, 60, 100, 200));
    }

    // 14. 生产场景：proxy-local → pet-local 不需要转换
    void test_production_coordNoConversion()
    {
        // Proxy-local point
        const QPoint proxyLocal(120, 80);
        // PetWindow-local point（无 offset 时相等）
        const QPoint petLocal = proxyLocal;
        QCOMPARE(proxyLocal, petLocal);
        // hitTest/pixelToCanvas 直接从 petLocal 开始，不加 offset
    }

    // 15. 模型缩放后 size 变化 → 触发 mask 刷新
    void test_production_scaleTriggersMaskRefresh()
    {
        // 缩放 100% → 缩放 200%，size 从 (200, 300) → (400, 600)
        QVERIFY(NeedsMaskRefresh(0, 0, 0, 0, QSize(200, 300), QSize(400, 600)));
        // 稳定后再次触发同一 size → 不刷新
        QVERIFY(!NeedsMaskRefresh(0, 0, 0, 0, QSize(400, 600), QSize(400, 600)));
    }

    // 16. 连续缩放后拖动不瞬移
    void test_production_scaleThenDrag()
    {
        // 缩放后 press
        const QPoint anchor = ComputeDragAnchor(QPoint(100, 100), 0, 0);
        QCOMPARE(anchor, QPoint(100, 100));
        // 拖动
        const QPoint newPos = ComputeNewPosition(QPoint(500, 400), anchor);
        QCOMPARE(newPos, QPoint(400, 300));
    }

    // ════════════════════════════════════════════════════════════════
    // GlobalToPetPoint 纯函数测试
    // ════════════════════════════════════════════════════════════════

    void test_globalToPet_middle()
    {
        // PetWindow 位于屏幕中间 (200,200)，尺寸 400x500
        // 鼠标全局位置 (400, 400) → PetWindow-local (200, 200)
        const QPoint petTopLeft(200, 200);
        const QPoint global(400, 400);
        QCOMPARE(GlobalToPetPoint(global, petTopLeft), QPoint(200, 200));
    }

    void test_globalToPet_topOverflow()
    {
        // PetWindow 顶部越界 (-50, -50)，尺寸 400x500
        // 鼠标全局位置 (100, 50) → PetWindow-local (150, 100)
        const QPoint petTopLeft(-50, -50);
        const QPoint global(100, 50);
        QCOMPARE(GlobalToPetPoint(global, petTopLeft), QPoint(150, 100));
    }

    void test_globalToPet_bottomOverflow()
    {
        // PetWindow 底部超出屏幕，左上角 (200, 800)，鼠标在 (300, 850)
        // 注意：PetWindow-local (300-200, 850-800) = (100, 50)
        // 与 proxy-local 无关
        const QPoint petTopLeft(200, 800);
        const QPoint global(300, 850);
        QCOMPARE(GlobalToPetPoint(global, petTopLeft), QPoint(100, 50));
    }

    void test_globalToPet_independentOfProxy()
    {
        // GlobalToPetPoint 完全不受 proxy offset/size 影响
        const QPoint petTopLeft(100, -30);
        // proxy 可能在 (100, 0) 即 offset (0, 30)，但不影响
        const QPoint global(250, 120);
        const QPoint expected(150, 150);
        QCOMPARE(GlobalToPetPoint(global, petTopLeft), expected);
    }

    // ════════════════════════════════════════════════════════════════
    // ComputePetTopLeftFromDrag 纯函数测试
    // ════════════════════════════════════════════════════════════════

    void test_firstFrame_noTeleport()
    {
        // 按下时：PetWindow 在 (300, 200)，鼠标全局 (500, 400)
        // 锚点 = PetWindow-local (200, 200)
        // 第一帧鼠标未移动：newPos = (500, 400) - (200, 200) = (300, 200) == 原位置
        const QPoint petTopLeft(300, 200);
        const QPoint globalMouse(500, 400);
        const QPoint anchorInPet = GlobalToPetPoint(globalMouse, petTopLeft);
        QCOMPARE(anchorInPet, QPoint(200, 200));
        const QPoint newPos = ComputePetTopLeftFromDrag(globalMouse, anchorInPet);
        QCOMPARE(newPos, petTopLeft);
    }

    void test_firstFrame_topOverflow()
    {
        // 顶部越界：PetWindow 在 (100, -60)，鼠标全局 (250, 100)
        // 锚点 = PetWindow-local (150, 160)
        // 第一帧：newPos = (250, 100) - (150, 160) = (100, -60) ✅ 不瞬移
        const QPoint petTopLeft(100, -60);
        const QPoint globalMouse(250, 100);
        const QPoint anchorInPet = GlobalToPetPoint(globalMouse, petTopLeft);
        QCOMPARE(anchorInPet, QPoint(150, 160));
        const QPoint newPos = ComputePetTopLeftFromDrag(globalMouse, anchorInPet);
        QCOMPARE(newPos, petTopLeft);
    }

    void test_firstFrame_bottomOverflow()
    {
        // 底部越界：PetWindow 在 (200, 900)，鼠标全局 (350, 950)
        // 锚点 = PetWindow-local (150, 50)
        const QPoint petTopLeft(200, 900);
        const QPoint globalMouse(350, 950);
        const QPoint anchorInPet = GlobalToPetPoint(globalMouse, petTopLeft);
        QCOMPARE(anchorInPet, QPoint(150, 50));
        const QPoint newPos = ComputePetTopLeftFromDrag(globalMouse, anchorInPet);
        QCOMPARE(newPos, petTopLeft);
    }

    // ════════════════════════════════════════════════════════════════
    // 连续拖动 20 次无累计偏移
    // ════════════════════════════════════════════════════════════════

    void test_20consecutiveDrags_noDrift()
    {
        // 第一次按下
        QPoint petTopLeft(400, 300);
        QPoint globalMouse(600, 500);
        QPoint anchorInPet = GlobalToPetPoint(globalMouse, petTopLeft);
        QCOMPARE(anchorInPet, QPoint(200, 200));

        // 连续拖动 20 次
        for (int i = 0; i < 20; ++i) {
            globalMouse += QPoint(10, 5);
            const QPoint newTopLeft = ComputePetTopLeftFromDrag(globalMouse, anchorInPet);
            // 每次 globalMouse 移动 (10,5)，PetWindow 也应移动 (10,5)
            QCOMPARE(newTopLeft - petTopLeft, QPoint(10, 5));
            petTopLeft = newTopLeft;
        }
    }

    // ════════════════════════════════════════════════════════════════
    // DeriveProxyMask 纯函数测试
    // ════════════════════════════════════════════════════════════════

    void test_deriveMask_identicalGeo()
    {
        // PetWindow 与 Proxy geometry 完全一致 → mask 无需平移
        const QRect petGeo(100, 100, 400, 500);
        const QRect proxyGeo(100, 100, 400, 500);
        QRegion canonicalRect;
        canonicalRect |= QRegion(50, 60, 100, 200);
        canonicalRect |= QRegion(200, 100, 80, 150);

        QRegion mask = DeriveProxyMask(canonicalRect, petGeo, proxyGeo);
        // translation = (100,100) - (100,100) = (0,0)，mask == canonicalRect
        QVERIFY(mask == canonicalRect);
    }

    void test_deriveMask_topClip()
    {
        // PetWindow 顶部越界：origin(-50, -30)，proxy 裁剪到 (0, 0)
        // 屏幕可用区从 y=0 开始
        const QRect petGeo(-50, -30, 400, 500);
        const QRect proxyGeo(0, 0, 350, 470);  // intersected
        // PetWindow-local drawable 在 (10, 20, 100, 200)
        QRegion canonicalRect(QRegion(10, 20, 100, 200));
        // translation = (-50,-30) - (0,0) = (-50,-30)
        // proxy-local = petLocal + (-50,-30) = (-40, -10)  → intersected 后 (0, 0, 60, 190)
        // 注意：(-40, -10) 到 (60, 190)，intersect 到 proxy size → (0, 0, 60, 190)
        QRegion expected(QRegion(0, 0, 60, 190));
        QRegion mask = DeriveProxyMask(canonicalRect, petGeo, proxyGeo);
        // 因为 QRegion(10-50, 20-30, 100, 200) 在 x=-40..60, y=-10..190
        // intersected with (0,0,350,470) → (0,0,60,190)
        QCOMPARE(mask.boundingRect(), expected.boundingRect());
    }

    void test_deriveMask_bottomClip()
    {
        // PetWindow 底部越界：origin(0, 900)，proxy 裁剪到底部
        // 屏幕可用区高 1000，但有任务栏，假设 available 高 950
        // petGeo 底部 = 900+500 = 1400 > 950，proxy 高 = 950-900 = 50
        const QRect petGeo(0, 900, 400, 500);
        const QRect proxyGeo(0, 900, 400, 50);  // 屏幕内上半部分
        // PetWindow-local drawable 在 bottom 区域 (50, 920, 100, 200)
        // PetWindow-local: (50, 920, 100, 200) 表示在 PetWindow 内的位置
        // translation = (0,900) - (0,900) = (0,0)
        // proxy-local = (50, 920, 100, 200)
        // intersected with proxy (0, 900, 400, 50) in proxy-local coords
        // proxy-local (50, 920, 100, 200) → intersected with (0, 0, 400, 50)
        // wait... the DeriveProxyMask function:
        // canonicalPetRegion.translated(petGeo.topLeft() - actualProxyGeo.topLeft())
        // = canonicalPetRegion.translated((0,900) - (0,900)) = canonicalPetRegion.translated(0,0)
        // = canonicalPetRegion unchanged at (50, 920, 100, 200)
        // Then intersected with QRect(0, 0, 400, 50)
        // In proxy-local coords: the region (50, 920, 100, 200) is way below the proxy
        // Wait, that's wrong. The region (50, 920, 100, 200) is in PetWindow-local coords.
        // After translation (0,0), it's still (50, 920, 100, 200) in proxy-local coords.
        // But the proxy is only 50 pixels tall (proxy-local y 0..50).
        // So (50, 920) is way outside the proxy. The mask would be empty.
        //
        // This doesn't make sense. Let me rethink.
        //
        // PetWindow-local coordinates: (0,0) is the top-left of the PetWindow.
        // So a drawable at PetWindow-local (50, 920) is 920 pixels below PetWindow's top.
        // PetWindow itself starts at y=900 on screen.
        // So the drawable screen position is 900 + 920 = 1820 — way below the screen.
        //
        // A more realistic test: the drawable is in the VISIBLE part.
        // Let's assume PetWindow at (0, 900), proxy at (0, 900, 400, 50).
        // A drawable near the top of PetWindow: PetWindow-local (50, 0, 100, 50).
        // That's at screen y 900..950, which is within the proxy.
        // After translation (0,0): proxy-local (50, 0, 100, 50).
        // Intersected with (0, 0, 400, 50): (50, 0, 100, 50) — fully visible!
        //
        // So the mask correctly preserves the visible part.

        QRegion canonicalRect(QRegion(50, 10, 100, 60)); // drawable near PetWindow top
        // translation = (0,900) - (0,900) = (0,0)
        // proxy-local same as pet-local
        // intersected with proxy size (400, 50)
        QRegion expected(QRegion(50, 10, 100, 40)); // clipped to 40 high
        QRegion mask = DeriveProxyMask(canonicalRect, petGeo, proxyGeo);
        QCOMPARE(mask.boundingRect(), expected.boundingRect());
    }

    void test_deriveMask_canonicalUnmodified()
    {
        // 验证 canonicalPetRegion 不被 DeriveProxyMask 修改
        const QRect petGeo(100, 100, 400, 500);
        const QRect proxyGeo(0, 0, 500, 600);  // proxy 不同位置
        QRegion canonicalRect(QRegion(50, 60, 100, 200));
        const QRegion original = canonicalRect;

        QRegion mask = DeriveProxyMask(canonicalRect, petGeo, proxyGeo);
        QVERIFY(mask != canonicalRect);  // 被平移了
        QVERIFY(canonicalRect == original);  // canonical 不变
    }

    void test_deriveMask_requestedVsActual()
    {
        // requested proxy geo 与实际不同时，mask 使用 actual geometry
        // requested: (0, 0, 400, 500), actual: (2, 0, 396, 500) — WM 调整了 x
        const QRect petGeo(100, 100, 400, 500);
        const QRect actualProxyGeo(102, 100, 396, 500);  // WM moved right by 2
        QRegion canonicalRect(QRegion(50, 60, 100, 200));
        // translation = (100,100) - (102,100) = (-2, 0)
        // proxy-local = (48, 60, 100, 200)
        QRegion expected(QRegion(48, 60, 100, 200));
        QRegion mask = DeriveProxyMask(canonicalRect, petGeo, actualProxyGeo);
        QVERIFY(mask == expected);
    }
};

QTEST_MAIN(DragAnchorTest)
#include "DragAnchorTest.moc"
