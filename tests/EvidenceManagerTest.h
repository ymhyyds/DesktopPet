#pragma once

#include <QObject>

class EvidenceManagerTest : public QObject {
    Q_OBJECT

public:
    explicit EvidenceManagerTest() = default;

private slots:
    void init();
    void cleanup();

    // 基本功能
    void testAddEvidenceOriginalTextExact();
    void testRoleAndOrderCorrect();
    void testDedupSameRoleAndText();
    void testDifferentRoleSameTextNoDedup();

    // 容量裁剪
    void testMaxCountTrimOldest();
    void testMaxLengthTrimOldest();
    void testSingleExceedsLimitNotAdded();

    // 生命周期
    void testTwoStyleSwitchesPreserveAndAccumulate();
    void testRoleSwitchClearsEvidence();
    void testUserClearClearsEvidence();
    void testFallbackDoesNotAffectEvidence();

    // 注入格式与保真
    void testEvidenceBlockContainsYenAndAlternatives();
    void testEvidenceBlockNoFabricatedRoomType();
    void testSpecialCharactersEscapedSafely();
    void testOpeningTagInjectedSafely();
    void testEvidenceBlockConflictInstructionPresent();
    void testOldAssistantStyleNotInstruction();
};
