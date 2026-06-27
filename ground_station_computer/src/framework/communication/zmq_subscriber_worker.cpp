#include "framework/communication/zmq_subscriber_worker.h"

#include "competition_core/protocol/envelope_codec.h"
#include "messages.pb.h"

#include <zmq.hpp>

ZmqSubscriberWorker::ZmqSubscriberWorker(QString endpoint, QObject *parent)
    : QThread(parent), endpoint_(std::move(endpoint)) {
    qRegisterMetaType<competition::TaskPlan>("competition::TaskPlan");
    qRegisterMetaType<competition::TaskEvent>("competition::TaskEvent");
    qRegisterMetaType<competition::TaskSummary>("competition::TaskSummary");
}

void ZmqSubscriberWorker::run() {
    try {
        zmq::context_t context(1);
        zmq::socket_t socket(context, zmq::socket_type::sub);
        socket.set(zmq::sockopt::subscribe, "");
        socket.set(zmq::sockopt::rcvtimeo, 200);
        socket.connect(endpoint_.toStdString());

        while (!isInterruptionRequested()) {
            zmq::message_t message;
            const auto received = socket.recv(message, zmq::recv_flags::none);
            if (!received) {
                continue;
            }

            Envelope envelope;
            if (!envelope.ParseFromArray(message.data(), static_cast<int>(message.size()))) {
                emit errorOccurred("收到无法解析的 Protobuf 消息");
                continue;
            }

            switch (envelope.payload_case()) {
            case Envelope::kTaskPlan:
            case Envelope::kMissionLoad: {
                QString error_message;
                const TaskPlanMessage &message = envelope.payload_case() == Envelope::kTaskPlan
                    ? envelope.task_plan()
                    : envelope.mission_load();
                const auto plan = competition::taskPlanFromMessage(message, &error_message);
                if (!plan.has_value()) {
                    emit errorOccurred(error_message);
                    break;
                }
                emit taskPlanReceived(plan.value());
                break;
            }
            case Envelope::kTaskEvent: {
                const auto &event = envelope.task_event();
                emit taskEventReceived(competition::TaskEvent{
                    QString::fromStdString(event.task_id()),
                    QString::fromStdString(event.event_type()),
                    event.sequence_index(),
                    QString::fromStdString(event.waypoint_id()),
                    QString::fromStdString(event.payload_json()),
                }, envelope.timestamp_ms());
                break;
            }
            case Envelope::kTaskSummary: {
                const auto &summary = envelope.task_summary();
                emit taskSummaryReceived(competition::TaskSummary{
                    QString::fromStdString(summary.task_id()),
                    QString::fromStdString(summary.task_type()),
                    summary.success(),
                    summary.visited_waypoints(),
                    QString::fromStdString(summary.payload_json()),
                });
                break;
            }
            case Envelope::PAYLOAD_NOT_SET:
                emit errorOccurred("收到空载荷消息");
                break;
            default:
                break;
            }
        }
    } catch (const std::exception &exception) {
        emit errorOccurred(QString::fromUtf8(exception.what()));
    }
}
