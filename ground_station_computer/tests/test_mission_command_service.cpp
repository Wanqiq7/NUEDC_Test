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
        ++attempts_;
        if (attempts_ <= replies_.size()) {
            return replies_.at(attempts_ - 1);
        }
        return CommandSendResult{false, "unexpected attempt"};
    }

    int attempts() const { return attempts_; }
    Envelope::PayloadCase lastPayloadCase() const { return last_payload_case_; }

private:
    QVector<CommandSendResult> replies_;
    mutable int attempts_ = 0;
    mutable Envelope::PayloadCase last_payload_case_ = Envelope::PAYLOAD_NOT_SET;
};

MissionPlanData makePlan() {
    MissionPlanData plan;
    plan.case_id = "case-123";
    plan.start_cell = "A1B1";
    plan.route = {"A1B1", "A1B2"};
    plan.terminal_cell = "A1B2";
    return plan;
}

}

class MissionCommandServiceTests : public QObject {
    Q_OBJECT

private slots:
    void missionPlanSyncUsesReliableTransport();
};

void MissionCommandServiceTests::missionPlanSyncUsesReliableTransport() {
    ScriptedTransport transport({
        CommandSendResult{false, "timeout"},
        CommandSendResult{true, "task plan stored"},
    });
    MissionCommandService service(&transport);

    const CommandSendResult result = service.sendMissionPlan(makePlan());

    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("task plan stored"));
    QCOMPARE(transport.attempts(), 2);
    QCOMPARE(static_cast<int>(transport.lastPayloadCase()), static_cast<int>(Envelope::kMissionLoad));
    QCOMPARE(static_cast<int>(service.linkStatus()), static_cast<int>(CommandLinkStatus::MissionSynced));
}

QTEST_MAIN(MissionCommandServiceTests)
#include "test_mission_command_service.moc"
