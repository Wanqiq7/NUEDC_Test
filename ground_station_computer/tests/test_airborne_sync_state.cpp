#include <QtTest/QtTest>

#include "framework/runtime/airborne_sync_state.h"
#include "framework/runtime/mission_runtime_state.h"

namespace {

CommandSendResult makeAck(bool ok, const QString &task_id, bool loaded, bool running, quint64 seq) {
    CommandSendResult result;
    result.ok = ok;
    result.task_id = task_id;
    result.mission_loaded = loaded;
    result.mission_running = running;
    result.last_accepted_sequence = seq;
    return result;
}

} // namespace

class AirborneSyncStateTests : public QObject {
    Q_OBJECT

private slots:
    void defaultsAreCleared();
    void resetClearsEverything();
    void clearAckKeepsSyncedAndRunning();
    void applyValidAckPopulatesState();
    void applyInvalidAckIsIgnored();
    void applyAirborneSyncOfflineClearsAck();
    void applyAirborneSyncUnsyncedClearsRunningAndLoaded();
    void markControlStartedAndStopped();
    void fillRuntimeInputsMirrorsState();
};

void AirborneSyncStateTests::defaultsAreCleared() {
    AirborneSyncState state;
    QVERIFY(!state.syncedToAirborne());
    QVERIFY(!state.running());
    QVERIFY(state.acknowledgedTaskId().isEmpty());
    QVERIFY(!state.acknowledgedMissionLoaded());
    QCOMPARE(state.lastAcceptedSequence(), quint64(0));
    QVERIFY(!state.hasAck());
}

void AirborneSyncStateTests::resetClearsEverything() {
    AirborneSyncState state;
    QVERIFY(state.applyCommandAck(makeAck(true, "case-1", true, true, 7)));
    state.setSyncedToAirborne(true);
    state.setRunning(true);

    state.reset();

    QVERIFY(!state.syncedToAirborne());
    QVERIFY(!state.running());
    QVERIFY(!state.hasAck());
    QVERIFY(state.acknowledgedTaskId().isEmpty());
    QCOMPARE(state.lastAcceptedSequence(), quint64(0));
}

void AirborneSyncStateTests::clearAckKeepsSyncedAndRunning() {
    // clearAck 仅清 ack 字段，保留 synced/running（对应 applyMissionPlanResult 中途的
    // sync_state_.clearAck()，此处不应连带清掉刚设置的同步/运行态）。
    AirborneSyncState state;
    state.setSyncedToAirborne(true);
    state.setRunning(true);
    QVERIFY(state.applyCommandAck(makeAck(true, "case-1", true, true, 3)));

    state.clearAck();

    QVERIFY(state.syncedToAirborne());
    QVERIFY(state.running());
    QVERIFY(!state.hasAck());
    QVERIFY(state.acknowledgedTaskId().isEmpty());
    QCOMPARE(state.lastAcceptedSequence(), quint64(0));
}

void AirborneSyncStateTests::applyValidAckPopulatesState() {
    AirborneSyncState state;
    const bool applied = state.applyCommandAck(makeAck(true, "case-42", true, true, 9));
    QVERIFY(applied);
    QCOMPARE(state.acknowledgedTaskId(), QStringLiteral("case-42"));
    QVERIFY(state.acknowledgedMissionLoaded());
    QVERIFY(state.syncedToAirborne());
    QVERIFY(state.running());
    QCOMPARE(state.lastAcceptedSequence(), quint64(9));
    QVERIFY(state.hasAck());
}

void AirborneSyncStateTests::applyInvalidAckIsIgnored() {
    AirborneSyncState state;
    QVERIFY(state.applyCommandAck(makeAck(true, "case-7", true, true, 5)));

    // ok=false 不应覆盖既有状态。
    QVERIFY(!state.applyCommandAck(makeAck(false, "case-other", false, false, 99)));
    QCOMPARE(state.acknowledgedTaskId(), QStringLiteral("case-7"));
    QVERIFY(state.syncedToAirborne());
    QCOMPARE(state.lastAcceptedSequence(), quint64(5));

    // ok=true 但 task_id 空且 seq==0 亦视为无效，不覆盖。
    QVERIFY(!state.applyCommandAck(makeAck(true, QString(), false, false, 0)));
    QCOMPARE(state.acknowledgedTaskId(), QStringLiteral("case-7"));
    QCOMPARE(state.lastAcceptedSequence(), quint64(5));
}

void AirborneSyncStateTests::applyAirborneSyncOfflineClearsAck() {
    AirborneSyncState state;
    QVERIFY(state.applyCommandAck(makeAck(true, "case-1", true, true, 4)));

    state.applyAirborneSync(/*online=*/false, /*synced=*/true);

    // 离线：清 ack；synced 仍按传入值设为 true，running 保持（synced=true 不清）。
    QVERIFY(!state.hasAck());
    QVERIFY(state.syncedToAirborne());
    QVERIFY(state.running());
}

void AirborneSyncStateTests::applyAirborneSyncUnsyncedClearsRunningAndLoaded() {
    AirborneSyncState state;
    QVERIFY(state.applyCommandAck(makeAck(true, "case-1", true, true, 4)));

    state.applyAirborneSync(/*online=*/true, /*synced=*/false);

    QVERIFY(!state.syncedToAirborne());
    QVERIFY(!state.running());
    QVERIFY(!state.acknowledgedMissionLoaded());
    // online=true 时不清 ack 序列 / task_id。
    QCOMPARE(state.acknowledgedTaskId(), QStringLiteral("case-1"));
    QCOMPARE(state.lastAcceptedSequence(), quint64(4));
}

void AirborneSyncStateTests::markControlStartedAndStopped() {
    AirborneSyncState state;
    state.markControlStarted();
    QVERIFY(state.running());
    QVERIFY(state.acknowledgedMissionLoaded());

    state.markControlStopped();
    QVERIFY(!state.running());
    // stop 不改 loaded。
    QVERIFY(state.acknowledgedMissionLoaded());
}

void AirborneSyncStateTests::fillRuntimeInputsMirrorsState() {
    AirborneSyncState state;
    QVERIFY(state.applyCommandAck(makeAck(true, "case-9", true, true, 12)));

    MissionRuntimeInputs inputs;
    state.fillRuntimeInputs(inputs);

    QCOMPARE(inputs.mission_synced_to_airborne, state.syncedToAirborne());
    QCOMPARE(inputs.mission_running, state.running());
    QCOMPARE(inputs.acknowledged_task_id, state.acknowledgedTaskId());
    QCOMPARE(inputs.acknowledged_mission_loaded, state.acknowledgedMissionLoaded());
    QCOMPARE(inputs.last_accepted_sequence, state.lastAcceptedSequence());
    // fillRuntimeInputs 不触碰非本类字段。
    QVERIFY(!inputs.command_sync_enabled);
    QVERIFY(!inputs.airborne_online);
    QVERIFY(inputs.active_task_id.isEmpty());
}

QTEST_MAIN(AirborneSyncStateTests)
#include "test_airborne_sync_state.moc"
