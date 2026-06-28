#include "h_problem_core/runtime/simulator.h"

#include "h_problem_core/mission/mission_planning.h"
#include "h_problem_core/protocol/envelope_builder.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace hcore {

namespace {

QString compactJson(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

QVector<SimMessage> simulateMessages(
    const CaseConfig &case_config,
    std::optional<MissionPlan> mission_plan,
    QString *error_message) {
    MissionPlan config_payload;
    if (!mission_plan.has_value() || mission_plan->route.isEmpty()) {
        const auto generated_plan = buildMissionPlan(case_config, std::nullopt, error_message);
        if (!generated_plan.has_value()) {
            return {};
        }
        config_payload = generated_plan.value();
    } else {
        config_payload = mission_plan.value();
    }

    QVector<SimMessage> messages;
    SimMessage config_message;
    config_message.type = SimMessageType::Config;
    config_message.mission_plan = config_payload;
    messages.append(config_message);

    QMap<QString, Animal> animals_by_cell;
    for (const Animal &animal : case_config.animals) {
        animals_by_cell.insert(animal.cell, animal);
    }

    QSet<QString> visited_cells;
    QSet<QString> reported_detection_cells;
    QMap<QString, quint32> totals;
    for (int index = 0; index < config_payload.route.size(); ++index) {
        const QString cell = config_payload.route.at(index);
        visited_cells.insert(cell);

        SimMessage telemetry;
        telemetry.type = SimMessageType::Telemetry;
        telemetry.cell = cell;
        telemetry.step_index = static_cast<quint32>(index);
        telemetry.visited_cells = static_cast<quint32>(visited_cells.size());
        telemetry.tick_interval_ms = case_config.tick_interval_ms;
        messages.append(telemetry);

        if (animals_by_cell.contains(cell) && !reported_detection_cells.contains(cell)) {
            reported_detection_cells.insert(cell);
            const Animal animal = animals_by_cell.value(cell);
            totals[animal.name] = totals.value(animal.name) + animal.count;

            SimMessage detection;
            detection.type = SimMessageType::Detection;
            detection.cell = animal.cell;
            detection.animal_name = animal.name;
            detection.count = animal.count;
            messages.append(detection);
        }
    }

    SimMessage summary;
    summary.type = SimMessageType::Summary;
    summary.totals = totals;
    summary.visited_cells = static_cast<quint32>(visited_cells.size());
    messages.append(summary);

    if (error_message != nullptr) {
        error_message->clear();
    }
    return messages;
}

std::optional<SimulatedTaskStream> simulateTaskStream(
    const CaseConfig &case_config,
    std::optional<MissionPlan> mission_plan,
    QString *error_message) {
    const QVector<SimMessage> messages = simulateMessages(case_config, mission_plan, error_message);
    if (messages.isEmpty()) {
        return std::nullopt;
    }

    SimulatedTaskStream stream;
    for (const SimMessage &message : messages) {
        switch (message.type) {
        case SimMessageType::Config:
            stream.plan = taskPlanFromMissionPlan(message.mission_plan);
            break;
        case SimMessageType::Telemetry: {
            QJsonObject payload;
            payload["current_cell"] = message.cell;
            payload["visited_cells"] = static_cast<int>(message.visited_cells);
            stream.events.append(competition::TaskEvent{
                stream.plan.task_id,
                "telemetry",
                message.step_index,
                message.cell,
                compactJson(payload),
            });
            break;
        }
        case SimMessageType::Detection: {
            QJsonObject payload;
            payload["cell_code"] = message.cell;
            payload["animal_name"] = message.animal_name;
            payload["count"] = static_cast<int>(message.count);
            stream.events.append(competition::TaskEvent{
                stream.plan.task_id,
                "detection",
                0,
                message.cell,
                compactJson(payload),
            });
            break;
        }
        case SimMessageType::Summary: {
            QJsonObject totals;
            for (auto iterator = message.totals.cbegin(); iterator != message.totals.cend(); ++iterator) {
                totals[iterator.key()] = static_cast<int>(iterator.value());
            }
            QJsonObject payload;
            payload["totals"] = totals;
            stream.summary = competition::TaskSummary{
                stream.plan.task_id,
                "h_problem",
                true,
                message.visited_cells,
                compactJson(payload),
            };
            break;
        }
        }
    }

    if (stream.plan.task_id.isEmpty() || stream.summary.task_id.isEmpty()) {
        if (error_message != nullptr) {
            *error_message = "simulator failed to produce a complete task stream";
        }
        return std::nullopt;
    }
    if (error_message != nullptr) {
        error_message->clear();
    }
    return stream;
}

} // namespace hcore
