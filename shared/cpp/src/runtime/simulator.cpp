#include "h_problem_core/runtime/simulator.h"

#include "h_problem_core/mission/mission_planning.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace hcore {

namespace {

QString compactJson(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

std::optional<SimulatedTaskStream> simulateTaskStream(
    const CaseConfig &case_config,
    std::optional<competition::TaskPlan> task_plan,
    QString *error_message) {
    competition::TaskPlan config_payload;
    if (!task_plan.has_value() || task_plan->waypoints.isEmpty()) {
        const auto generated_plan = buildTaskPlan(case_config, std::nullopt, error_message);
        if (!generated_plan.has_value()) {
            return std::nullopt;
        }
        config_payload = generated_plan.value();
    } else {
        config_payload = task_plan.value();
    }

    SimulatedTaskStream stream;
    stream.plan = config_payload;

    QMap<QString, Animal> animals_by_cell;
    for (const Animal &animal : case_config.animals) {
        animals_by_cell.insert(animal.cell, animal);
    }

    QSet<quint32> visited_sequences;
    QString last_grid_cell;
    QSet<QString> reported_detection_cells;
    QMap<QString, quint32> totals;
    for (const competition::TaskWaypoint &waypoint : config_payload.waypoints) {
        visited_sequences.insert(waypoint.sequence_index);
        const bool grid_action = waypoint.action == "takeoff" || waypoint.action == "navigate";
        if (grid_action) {
            last_grid_cell = waypoint.id;
        }

        QJsonObject telemetry_payload;
        telemetry_payload["current_cell"] = last_grid_cell;
        telemetry_payload["visited_cells"] = static_cast<int>(visited_sequences.size());
        telemetry_payload["tick_interval_ms"] = case_config.tick_interval_ms;
        stream.events.append(competition::TaskEvent{
            stream.plan.task_id,
            "telemetry",
            waypoint.sequence_index,
            waypoint.id,
            compactJson(telemetry_payload),
        });

        if (grid_action && animals_by_cell.contains(waypoint.id) &&
            !reported_detection_cells.contains(waypoint.id)) {
            reported_detection_cells.insert(waypoint.id);
            const Animal animal = animals_by_cell.value(waypoint.id);
            totals[animal.name] = totals.value(animal.name) + animal.count;

            QJsonObject detection_payload;
            detection_payload["cell_code"] = animal.cell;
            detection_payload["animal_name"] = animal.name;
            detection_payload["count"] = static_cast<int>(animal.count);
            stream.events.append(competition::TaskEvent{
                stream.plan.task_id,
                "detection",
                waypoint.sequence_index,
                animal.cell,
                compactJson(detection_payload),
            });
        }
    }

    QJsonObject totals_payload;
    for (auto iterator = totals.cbegin(); iterator != totals.cend(); ++iterator) {
        totals_payload[iterator.key()] = static_cast<int>(iterator.value());
    }
    QJsonObject summary_payload;
    summary_payload["totals"] = totals_payload;
    stream.summary = competition::TaskSummary{
        stream.plan.task_id,
        "h_problem",
        true,
        static_cast<quint32>(visited_sequences.size()),
        compactJson(summary_payload),
    };

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
