#include <QtTest/QtTest>

#include "framework/runtime/mission_runtime_state.h"

class MissionRuntimeStateTests : public QObject {
    Q_OBJECT

private slots:
    void disablesBothControlsWhenCommandSyncDisabled();
    void enablesExecuteOnlyWhenReadyAndNotRunning();
    void enablesStopOnlyWhenRunning();
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

QTEST_MAIN(MissionRuntimeStateTests)
#include "test_mission_runtime_state.moc"
