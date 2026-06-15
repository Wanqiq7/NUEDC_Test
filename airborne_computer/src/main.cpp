#include "airborne_runtime.h"
#include "command_server.h"
#include "h_problem_mission_runtime.h"

#include "competition_core/protocol/envelope_codec.h"
#include "h_problem_core/mission/case_loader.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QThread>

#include <zmq.hpp>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("airborne_app");

    QCommandLineParser parser;
    parser.setApplicationDescription("H 题 C++ 机载端");
    parser.addHelpOption();
    parser.addOption({{"c", "case"}, "案例文件路径", "path", "shared/cases/sample_case.json"});
    parser.addOption({"mission-plan", "任务计划 JSON 文件路径", "path", "runtime/active_mission_plan.json"});
    parser.addOption({"endpoint", "PUB 发布地址", "endpoint", "tcp://0.0.0.0:5557"});
    parser.addOption({"command-endpoint", "REP 命令地址；为空时不启动命令服务", "endpoint", "tcp://0.0.0.0:5558"});
    parser.addOption({"wait-for-start", "等待 START_MISSION 后再发布"});
    parser.addOption({"startup-delay", "首次发布前延时秒数", "seconds", "0.4"});
    parser.addOption({"sleep-scale", "tick 时间缩放", "scale", "1.0"});
    parser.process(app);

    if (parser.isSet("wait-for-start") && parser.value("command-endpoint").isEmpty()) {
        qCritical("--wait-for-start requires --command-endpoint");
        return 2;
    }

    QString error;
    const auto case_config = hcore::loadCase(parser.value("case"), &error);
    if (!case_config.has_value()) {
        qCritical("无法加载案例: %s", qPrintable(error));
        return 1;
    }

    competition::CommandState command_state;
    const QString mission_plan_path = parser.value("mission-plan");
    std::optional<airborne::CommandServer> server;
    if (!parser.value("command-endpoint").isEmpty()) {
        server.emplace(parser.value("command-endpoint"), mission_plan_path, &command_state);
        server->start();
    }

    const double startup_delay = parser.value("startup-delay").toDouble();
    if (startup_delay > 0.0) {
        QThread::msleep(static_cast<unsigned long>(startup_delay * 1000.0));
    }

    if (parser.isSet("wait-for-start")) {
        while (!command_state.start_requested && !command_state.stop_requested) {
            QThread::msleep(50);
        }
    }

    const auto stored_plan = airborne::loadOptionalMissionPlan(mission_plan_path, &error);
    if (!error.isEmpty()) {
        qWarning("无法加载已保存任务计划 %s: %s", qPrintable(mission_plan_path), qPrintable(error));
        error.clear();
    }
    const hcore::MissionPlan selected_plan = airborne::selectMissionPlan(case_config.value(), stored_plan, &error);
    if (!error.isEmpty()) {
        qCritical("无法生成任务计划: %s", qPrintable(error));
        return 1;
    }

    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::pub);
    socket.bind(parser.value("endpoint").toStdString());

    const double sleep_scale = parser.value("sleep-scale").toDouble();
    airborne::CallbackEventPublisher publisher(
        [&](quint64 sequence, const competition::TaskEvent &event) {
            const Envelope envelope = competition::buildTaskEventEnvelope(sequence, event);
            std::string bytes;
            envelope.SerializeToString(&bytes);
            socket.send(zmq::buffer(bytes), zmq::send_flags::none);
            return true;
        },
        [&](quint64 sequence, const competition::TaskSummary &summary) {
            const Envelope envelope = competition::buildTaskSummaryEnvelope(sequence, summary);
            std::string bytes;
            envelope.SerializeToString(&bytes);
            socket.send(zmq::buffer(bytes), zmq::send_flags::none);
            return true;
        });
    airborne::HProblemMissionRuntime runtime(case_config.value(), selected_plan, &publisher);
    const int result = runtime.execute(command_state, sleep_scale);
    if (result != 0 && !runtime.lastError().isEmpty()) {
        qCritical("任务运行失败: %s", qPrintable(runtime.lastError()));
    }

    if (server.has_value()) {
        server->requestStop();
        server->wait(1000);
    }
    socket.close();
    context.close();
    return result;
}
