#include "zmq_subscriber_worker.h"

#include "message_dispatcher.h"
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
            case Envelope::kGridConfig: {
                const auto &config = envelope.grid_config();
                emit gridConfigReceived(
                    QString::fromStdString(config.case_id()),
                    QString::fromStdString(config.start_cell()),
                    MessageDispatcher::toQStringList(config.no_fly_cells()),
                    MessageDispatcher::toQStringList(config.route()),
                    QString::fromStdString(config.terminal_cell()),
                    config.landing_enabled(),
                    static_cast<double>(config.descent_angle_deg()),
                    static_cast<double>(config.takeoff_anchor_x_cm()),
                    static_cast<double>(config.takeoff_anchor_y_cm()));
                break;
            }
            case Envelope::kTelemetry: {
                const auto &telemetry = envelope.telemetry();
                emit telemetryReceived(
                    QString::fromStdString(telemetry.current_cell()),
                    static_cast<int>(telemetry.step_index()),
                    static_cast<int>(telemetry.visited_cells()));
                break;
            }
            case Envelope::kAnimalDetection: {
                const auto &detection = envelope.animal_detection();
                emit detectionReceived(
                    QString::fromStdString(detection.cell_code()),
                    QString::fromStdString(detection.animal_name()),
                    static_cast<int>(detection.count()),
                    envelope.timestamp_ms());
                break;
            }
            case Envelope::kMissionSummary: {
                const auto &summary = envelope.mission_summary();
                emit summaryReceived(
                    MessageDispatcher::toSummaryMap(summary),
                    static_cast<int>(summary.visited_cells()));
                break;
            }
            case Envelope::PAYLOAD_NOT_SET:
                emit errorOccurred("收到空载荷消息");
                break;
            }
        }
    } catch (const std::exception &exception) {
        emit errorOccurred(QString::fromUtf8(exception.what()));
    }
}
