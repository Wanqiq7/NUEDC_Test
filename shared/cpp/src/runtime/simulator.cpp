#include "h_problem_core/runtime/simulator.h"

#include "h_problem_core/mission/mission_planning.h"

namespace hcore {

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

} // namespace hcore
