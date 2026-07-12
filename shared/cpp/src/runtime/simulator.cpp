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

    QSet<QString> visited_cells;
    QSet<QString> reported_detection_cells;
    QMap<QString, quint32> totals;
    for (int index = 0; index < config_payload.waypoints.size(); ++index) {
        const QString cell = config_payload.waypoints.at(index).id;
        visited_cells.insert(cell);

        QJsonObject telemetry_payload;
        telemetry_payload["current_cell"] = cell;
        telemetry_payload["visited_cells"] = static_cast<int>(visited_cells.size());
        telemetry_payload["tick_interval_ms"] = case_config.tick_interval_ms;
        stream.events.append(competition::TaskEvent{
            stream.plan.task_id,
            "telemetry",
            static_cast<quint32>(index),
            cell,
            compactJson(telemetry_payload),
        });

        if (animals_by_cell.contains(cell) && !reported_detection_cells.contains(cell)) {
            reported_detection_cells.insert(cell);
            const Animal animal = animals_by_cell.value(cell);
            totals[animal.name] = totals.value(animal.name) + animal.count;

            QJsonObject detection_payload;
            detection_payload["cell_code"] = animal.cell;
            detection_payload["animal_name"] = animal.name;
            detection_payload["count"] = static_cast<int>(animal.count);
            stream.events.append(competition::TaskEvent{
                stream.plan.task_id,
                "detection",
                static_cast<quint32>(index),
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
        static_cast<quint32>(visited_cells.size()),
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
