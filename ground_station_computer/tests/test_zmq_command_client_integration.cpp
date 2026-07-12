#include <QtTest/QtTest>

#include "competition_core/protocol/command_handler.h"
#include "competition_core/protocol/envelope_codec.h"
#include "framework/communication/zmq_command_client.h"
#include "messages.pb.h"

#include <QElapsedTimer>

#include <thread>

#include <zmq.hpp>

namespace {

competition::TaskPlan makeTaskPlan() {
    competition::TaskPlan plan;
    plan.task_id = "local-rep-task";
    plan.task_type = "h_problem";
    return plan;
}

class LocalCommandServer {
public:
    LocalCommandServer()
        : socket_(context_, zmq::socket_type::rep) {
        state_.setActiveTaskPlan(makeTaskPlan());
        state_.setMissionLoaded(true);
        socket_.set(zmq::sockopt::linger, 0);
        socket_.set(zmq::sockopt::rcvtimeo, 500);
        socket_.set(zmq::sockopt::sndtimeo, 500);
        socket_.bind("tcp://127.0.0.1:*");
        endpoint_ = QString::fromStdString(socket_.get(zmq::sockopt::last_endpoint));
        worker_ = std::thread([this] { serveTwoRequests(); });
    }

    ~LocalCommandServer() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    QString endpoint() const { return endpoint_; }
    void wait() {
        if (worker_.joinable()) {
            worker_.join();
        }
    }
    QVector<CommandType> receivedTypes() const { return received_types_; }

private:
    void serveTwoRequests() {
        for (int request_index = 0; request_index < 2; ++request_index) {
            zmq::message_t request;
            if (!socket_.recv(request, zmq::recv_flags::none)) {
                return;
            }

            Envelope envelope;
            if (!envelope.ParseFromArray(request.data(), static_cast<int>(request.size()))) {
                return;
            }
            received_types_.append(envelope.control_command().type());

            const competition::AckResult ack = competition::applyCommandEnvelope(envelope, &state_);
            const QByteArray reply = competition::buildAckBytes(ack, &state_);
            if (!socket_.send(zmq::buffer(reply.constData(), reply.size()), zmq::send_flags::none)) {
                return;
            }
        }
    }

    zmq::context_t context_{1};
    zmq::socket_t socket_;
    competition::CommandState state_;
    QString endpoint_;
    std::thread worker_;
    QVector<CommandType> received_types_;
};

}

class ZmqCommandClientIntegrationTests : public QObject {
    Q_OBJECT

private slots:
    void armAndDisarmUseWireEnumsAndReceiveStatefulAcks();
    void serverCleanupIsBoundedWhenSecondRequestIsMissing();
};

void ZmqCommandClientIntegrationTests::armAndDisarmUseWireEnumsAndReceiveStatefulAcks() {
    LocalCommandServer server;
    ZmqCommandClient client(server.endpoint());

    const CommandSendResult arm_result = client.sendEnvelope(
        ZmqCommandClient::buildControlCommandEnvelope(
            501,
            GroundControlCommandType::ArmTargeting,
            "local-rep-task"));
    QVERIFY2(arm_result.ok, qPrintable(arm_result.message));
    QCOMPARE(arm_result.task_id, QString("local-rep-task"));
    QVERIFY(arm_result.mission_loaded);
    QVERIFY(!arm_result.mission_running);
    QCOMPARE(arm_result.last_accepted_sequence, 501ULL);
    QVERIFY(arm_result.vision_armed);

    const CommandSendResult reset_result = client.sendEnvelope(
        ZmqCommandClient::buildControlCommandEnvelope(
            502,
            GroundControlCommandType::DisarmTargeting,
            "local-rep-task"));
    QVERIFY2(reset_result.ok, qPrintable(reset_result.message));
    QCOMPARE(reset_result.task_id, QString("local-rep-task"));
    QVERIFY(reset_result.mission_loaded);
    QVERIFY(!reset_result.mission_running);
    QCOMPARE(reset_result.last_accepted_sequence, 502ULL);
    QVERIFY(!reset_result.vision_armed);

    server.wait();
    const QVector<CommandType> received_types = server.receivedTypes();
    QCOMPARE(received_types.size(), 2);
    QCOMPARE(received_types.at(0), COMMAND_TYPE_ARM_TARGETING);
    QCOMPARE(received_types.at(1), COMMAND_TYPE_RESET_TARGETING);
}

void ZmqCommandClientIntegrationTests::serverCleanupIsBoundedWhenSecondRequestIsMissing() {
    QElapsedTimer elapsed;
    elapsed.start();
    {
        LocalCommandServer server;
        ZmqCommandClient client(server.endpoint());
        const CommandSendResult arm_result = client.sendEnvelope(
            ZmqCommandClient::buildControlCommandEnvelope(
                601,
                GroundControlCommandType::ArmTargeting,
                "local-rep-task"));
        QVERIFY2(arm_result.ok, qPrintable(arm_result.message));
    }
    QVERIFY(elapsed.elapsed() < 1500);
}

QTEST_MAIN(ZmqCommandClientIntegrationTests)
#include "test_zmq_command_client_integration.moc"
