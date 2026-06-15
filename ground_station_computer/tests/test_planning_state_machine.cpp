#include <QtTest/QtTest>

#include "h_problem/mission/h_no_fly_planning_state.h"

class PlanningStateMachineTests : public QObject {
    Q_OBJECT

private slots:
    void startsInIdlePreview();
    void entersSelectionAfterClick();
    void selectionValidSwitchesButtonText();
    void readyStateAfterValidSelection();
    void readyStateLosesValidity();
    void resetsAfterGenerationSuccess();
};

void PlanningStateMachineTests::startsInIdlePreview() {
    PlanningStateMachine machine;
    QCOMPARE(machine.state(), PlanningUiState::IdlePreview);
    QCOMPARE(machine.primaryButtonText(), QStringLiteral("设置禁飞区"));
}

void PlanningStateMachineTests::entersSelectionAfterClick() {
    PlanningStateMachine machine;
    machine.handlePrimaryButtonClicked();
    QCOMPARE(machine.state(), PlanningUiState::SelectingNoFlyZone);
    QCOMPARE(machine.primaryButtonText(), QStringLiteral("设置禁飞区"));
}

void PlanningStateMachineTests::selectionValidSwitchesButtonText() {
    PlanningStateMachine machine;
    machine.handlePrimaryButtonClicked();
    QCOMPARE(machine.primaryButtonText(), QStringLiteral("设置禁飞区"));
    machine.updateSelectionValidity(true);
    QCOMPARE(machine.primaryButtonText(), QStringLiteral("航线生成"));
}

void PlanningStateMachineTests::readyStateAfterValidSelection() {
    PlanningStateMachine machine;
    machine.handlePrimaryButtonClicked();
    machine.updateSelectionValidity(true);
    QCOMPARE(machine.state(), PlanningUiState::ReadyToGenerate);
    QCOMPARE(machine.primaryButtonText(), QStringLiteral("航线生成"));
}

void PlanningStateMachineTests::readyStateLosesValidity() {
    PlanningStateMachine machine;
    machine.handlePrimaryButtonClicked();
    machine.updateSelectionValidity(true);
    QCOMPARE(machine.state(), PlanningUiState::ReadyToGenerate);
    machine.updateSelectionValidity(false);
    QCOMPARE(machine.state(), PlanningUiState::SelectingNoFlyZone);
    QCOMPARE(machine.primaryButtonText(), QStringLiteral("设置禁飞区"));
}

void PlanningStateMachineTests::resetsAfterGenerationSuccess() {
    PlanningStateMachine machine;
    machine.handlePrimaryButtonClicked();
    machine.updateSelectionValidity(true);
    machine.handleGenerationSucceeded();
    QCOMPARE(machine.state(), PlanningUiState::IdlePreview);
    QCOMPARE(machine.primaryButtonText(), QStringLiteral("设置禁飞区"));
}

QTEST_MAIN(PlanningStateMachineTests)
#include "test_planning_state_machine.moc"
