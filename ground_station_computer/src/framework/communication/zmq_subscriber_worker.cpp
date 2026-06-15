#include "framework/communication/zmq_subscriber_worker.h"

#include "h_problem/mission/h_protocol_adapter.h"
#include "messages.pb.h"

#include <QStringList>
#include <zmq.hpp>

ZmqSubscriberWorker::ZmqSubscriberWorker(QString endpoint, QObject *parent)
    : QThread(parent), endpoint_(std::move(endpoint)) {}

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
                HGridConfigData config;
                QString error_message;
                const TaskPlanMessage &message = envelope.payload_case() == Envelope::kTaskPlan
                    ? envelope.task_plan()
                    : envelope.mission_load();
                if (!HProtocolAdapter::decodeGridConfig(message, &config, &error_message)) {
                    emit errorOccurred(error_message);
                    break;
                }
                emit gridConfigReceived(
                    config.case_id,
                    config.start_cell,
                    config.no_fly_cells,
                    config.route,
                    config.terminal_cell,
                    config.landing_enabled,
                    config.descent_angle_deg,
                    config.takeoff_anchor_x_cm,
                    config.takeoff_anchor_y_cm);
                break;
            }
            case Envelope::kTaskEvent: {
                const auto &event = envelope.task_event();
                QString error_message;
                if (QString::fromStdString(event.event_type()) == "detection") {
                    HDetectionData detection;
                    if (!HProtocolAdapter::decodeDetection(event, &detection, &error_message)) {
                        emit errorOccurred(error_message);
                        break;
                    }
                    emit detectionReceived(
                        detection.cell_code,
                        detection.animal_name,
                        detection.count,
                        envelope.timestamp_ms());
                    break;
                }

                HTelemetryData telemetry;
                if (!HProtocolAdapter::decodeTelemetry(event, &telemetry, &error_message)) {
                    emit errorOccurred(error_message);
                    break;
                }
                emit telemetryReceived(telemetry.current_cell, telemetry.step_index, telemetry.visited_cells);
                break;
            }
            case Envelope::kTaskSummary: {
                HSummaryData summary;
                QString error_message;
                if (!HProtocolAdapter::decodeSummary(envelope.task_summary(), &summary, &error_message)) {
                    emit errorOccurred(error_message);
                    break;
                }
                emit summaryReceived(summary.totals, summary.visited_cells);
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
