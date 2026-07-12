#include <QtTest/QtTest>

#include "h_problem/mission/h_mission_command_service.h"
#include "messages.pb.h"

namespace {

class ScriptedTransport final : public CommandTransport {
public:
    explicit ScriptedTransport(QVector<CommandSendResult> replies)
        : replies_(std::move(replies)) {}

    CommandSendResult sendEnvelope(const Envelope &envelope) const override {
        last_payload_case_ = envelope.payload_case();
        last_sequence_ = envelope.sequence();
        if (envelope.has_control_command()) {
            last_control_type_ = envelope.control_command().type();
            last_task_id_ = QString::fromStdString(envelope.control_command().task_id());
        }
        ++attempts_;
        if (attempts_ <= replies_.size()) {
            return replies_.at(attempts_ - 1);
        }
        return CommandSendResult{false, "unexpected attempt"};
    }

    int attempts() const { return attempts_; }
    Envelope::PayloadCase lastPayloadCase() const { return last_payload_case_; }
    quint64 lastSequence() const { return last_sequence_; }
    CommandType lastControlType() const { return last_control_type_; }
    QString lastTaskId() const { return last_task_id_; }

private:
    QVector<CommandSendResult> replies_;
    mutable int attempts_ = 0;
    mutable Envelope::PayloadCase last_payload_case_ = Envelope::PAYLOAD_NOT_SET;
    mutable quint64 last_sequence_ = 0;
    mutable CommandType last_control_type_ = COMMAND_TYPE_UNSPECIFIED;
    mutable QString last_task_id_;
};

competition::TaskPlan makePlan() {
    competition::TaskPlan plan;
    plan.task_id = "case-123";
    plan.task_type = "h_problem";
    plan.start_waypoint_id = "A1B1";
    plan.terminal_waypoint_id = "A1B2";
    plan.waypoints = {{"A1B1", 0}, {"A1B2", 1}};
    return plan;
}

}

class MissionCommandServiceTests : public QObject {
    Q_OBJECT

private slots:
    void taskPlanSyncUsesReliableTransport();
    void taskPlanSyncReturnsAckRuntimeState();
    void armTargetingSendsControlCommand();
    void disarmTargetingSendsControlCommand();
};

void MissionCommandServiceTests::taskPlanSyncUsesReliableTransport() {
    ScriptedTransport transport({
        CommandSendResult{false, "timeout"},
        CommandSendResult{true, "task plan stored"},
    });
    MissionCommandService service(&transport);

    const CommandSendResult result = service.sendTaskPlan(makePlan());

    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("task plan stored"));
    QCOMPARE(transport.attempts(), 2);
    QCOMPARE(static_cast<int>(transport.lastPayloadCase()), static_cast<int>(Envelope::kMissionLoad));
    QVERIFY(transport.lastSequence() > 0);
    QCOMPARE(static_cast<int>(service.linkStatus()), static_cast<int>(CommandLinkStatus::MissionSynced));
}


void MissionCommandServiceTests::taskPlanSyncReturnsAckRuntimeState() {
    ScriptedTransport transport({
        CommandSendResult{true, "task plan stored", "case-123", true, false, 77},
    });
    MissionCommandService service(&transport);

    const CommandSendResult result = service.sendTaskPlan(makePlan());

    QVERIFY(result.ok);
    QCOMPARE(result.task_id, QString("case-123"));
    QVERIFY(result.mission_loaded);
    QVERIFY(!result.mission_running);
    QCOMPARE(result.last_accepted_sequence, 77ULL);
}

void MissionCommandServiceTests::armTargetingSendsControlCommand() {
    ScriptedTransport transport({CommandSendResult{true, "vision armed"}});
    MissionCommandService service(&transport);

    const CommandSendResult result = service.sendControlCommand(
        GroundControlCommandType::ArmTargeting, "case-123");

    QVERIFY(result.ok);
    QCOMPARE(static_cast<int>(transport.lastPayloadCase()), static_cast<int>(Envelope::kControlCommand));
    QCOMPARE(static_cast<int>(transport.lastControlType()), static_cast<int>(COMMAND_TYPE_ARM_TARGETING));
    QCOMPARE(transport.lastTaskId(), QString("case-123"));
}

void MissionCommandServiceTests::disarmTargetingSendsControlCommand() {
    ScriptedTransport transport({CommandSendResult{true, "vision reset"}});
    MissionCommandService service(&transport);

    const CommandSendResult result = service.disarmVisionTargeting("case-123");

    QVERIFY(result.ok);
    QCOMPARE(static_cast<int>(transport.lastPayloadCase()), static_cast<int>(Envelope::kControlCommand));
    QCOMPARE(static_cast<int>(transport.lastControlType()), static_cast<int>(COMMAND_TYPE_RESET_TARGETING));
    QCOMPARE(transport.lastTaskId(), QString("case-123"));
}
QTEST_MAIN(MissionCommandServiceTests)
#include "test_mission_command_service.moc"
