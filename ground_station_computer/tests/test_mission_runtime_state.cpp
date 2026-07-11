#include <QtTest/QtTest>

#include "framework/runtime/mission_runtime_state.h"

class MissionRuntimeStateTests : public QObject {
    Q_OBJECT

private slots:
    void disablesBothControlsWhenCommandSyncDisabled();
    void enablesExecuteOnlyWhenReadyAndNotRunning();
    void enablesStopOnlyWhenRunning();
    void rejectsAckStateForDifferentTask();
    void disablesExecuteWhenAckSaysMissionNotLoaded();
    void formatsAckStateForStatusDisplay();
    void formatsVisionArmedStateForStatusDisplay();
    void enablesVisionControlsOnlyForOnlineLoadedTask();
};

void MissionRuntimeStateTests::disablesBothControlsWhenCommandSyncDisabled() {
    const MissionRuntimeControls controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        false,
        true,
        true,
        false,
    });
    QVERIFY(!controls.can_execute);
    QVERIFY(!controls.can_stop);
}

void MissionRuntimeStateTests::enablesExecuteOnlyWhenReadyAndNotRunning() {
    const MissionRuntimeControls controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        true,
        true,
        true,
        false,
    });
    QVERIFY(controls.can_execute);
    QVERIFY(!controls.can_stop);
}

void MissionRuntimeStateTests::enablesStopOnlyWhenRunning() {
    const MissionRuntimeControls controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        true,
        true,
        true,
        true,
    });
    QVERIFY(!controls.can_execute);
    QVERIFY(controls.can_stop);
}

void MissionRuntimeStateTests::rejectsAckStateForDifferentTask() {
    const MissionRuntimeControls controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        true,
        true,
        true,
        false,
        "local-task",
        "airborne-task",
        true,
        7,
    });
    QVERIFY(!controls.can_execute);
    QVERIFY(!controls.can_stop);
}

void MissionRuntimeStateTests::disablesExecuteWhenAckSaysMissionNotLoaded() {
    const MissionRuntimeControls controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        true,
        true,
        true,
        false,
        "case-001",
        "case-001",
        false,
        8,
    });
    QVERIFY(!controls.can_execute);
    QVERIFY(!controls.can_stop);
}

void MissionRuntimeStateTests::formatsAckStateForStatusDisplay() {
    const QString text = MissionRuntimeState::airborneStatusText(MissionRuntimeInputs{
        true,
        true,
        true,
        false,
        "case-001",
        "case-001",
        true,
        42,
    });

    QVERIFY(text.contains("在线"));
    QVERIFY(text.contains("case-001"));
    QVERIFY(text.contains("已加载"));
    QVERIFY(text.contains("待执行"));
    QVERIFY(text.contains("42"));
}

void MissionRuntimeStateTests::formatsVisionArmedStateForStatusDisplay() {
    const QString text = MissionRuntimeState::airborneStatusText(MissionRuntimeInputs{
        true,
        true,
        true,
        false,
        "case-001",
        "case-001",
        true,
        42,
        true,
    });

    QVERIFY(text.contains("视觉瞄准: 已武装"));
}

void MissionRuntimeStateTests::enablesVisionControlsOnlyForOnlineLoadedTask() {
    const MissionRuntimeControls ready_controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        true,
        true,
        true,
        false,
        "case-001",
        "case-001",
        true,
        42,
    });
    QVERIFY(ready_controls.can_arm_vision);
    QVERIFY(ready_controls.can_reset_vision);

    const MissionRuntimeControls offline_controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        true,
        false,
        true,
        false,
        "case-001",
        "case-001",
        true,
        42,
    });
    QVERIFY(!offline_controls.can_arm_vision);
    QVERIFY(!offline_controls.can_reset_vision);

    const MissionRuntimeControls unloaded_controls = MissionRuntimeState::controlsFor(MissionRuntimeInputs{
        true,
        true,
        true,
        false,
        "case-001",
        "case-001",
        false,
        42,
    });
    QVERIFY(!unloaded_controls.can_arm_vision);
    QVERIFY(!unloaded_controls.can_reset_vision);
}

QTEST_MAIN(MissionRuntimeStateTests)
#include "test_mission_runtime_state.moc"
