#include "h_problem_mission_runtime.h"

#include "h_problem_core/mission/mission_plan_store.h"
#include "h_problem_core/mission/mission_planning.h"
#include "h_problem_core/protocol/envelope_builder.h"
#include "h_problem_core/runtime/simulator.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <utility>

namespace airborne {

namespace {

QString compactJson(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

competition::TaskEvent telemetryEventFromMessage(
    const hcore::SimMessage &message,
    const hcore::MissionPlan &mission_plan) {
    QJsonObject payload;
    payload["current_cell"] = message.cell;
    payload["visited_cells"] = static_cast<int>(message.visited_cells);

    competition::TaskEvent event;
    event.task_id = mission_plan.case_id;
    event.event_type = "telemetry";
    event.sequence_index = message.step_index;
    event.waypoint_id = message.cell;
    event.payload_json = compactJson(payload);
    return event;
}

competition::TaskEvent detectionEventFromMessage(
    const hcore::SimMessage &message,
    const hcore::MissionPlan &mission_plan) {
    QJsonObject payload;
    payload["cell_code"] = message.cell;
    payload["animal_name"] = message.animal_name;
    payload["count"] = static_cast<int>(message.count);

    competition::TaskEvent event;
    event.task_id = mission_plan.case_id;
    event.event_type = "detection";
    event.waypoint_id = message.cell;
    event.payload_json = compactJson(payload);
    return event;
}

competition::TaskSummary summaryFromMessage(
    const hcore::SimMessage &message,
    const hcore::MissionPlan &mission_plan) {
    QJsonObject totals;
    for (auto iterator = message.totals.cbegin(); iterator != message.totals.cend(); ++iterator) {
        totals[iterator.key()] = static_cast<int>(iterator.value());
    }

    QJsonObject payload;
    payload["totals"] = totals;

    competition::TaskSummary summary;
    summary.task_id = mission_plan.case_id;
    summary.task_type = "h_problem";
    summary.success = true;
    summary.visited_waypoints = message.visited_cells;
    summary.payload_json = compactJson(payload);
    return summary;
}

} // namespace

CallbackEventPublisher::CallbackEventPublisher(
    TaskEventPublishCallback event_callback,
    TaskSummaryPublishCallback summary_callback)
    : event_callback_(std::move(event_callback)),
      summary_callback_(std::move(summary_callback)) {}

bool CallbackEventPublisher::publishEvent(quint64 sequence, const competition::TaskEvent &event) {
    return event_callback_ != nullptr && event_callback_(sequence, event);
}

bool CallbackEventPublisher::publishSummary(quint64 sequence, const competition::TaskSummary &summary) {
    return summary_callback_ != nullptr && summary_callback_(sequence, summary);
}

HProblemMissionRuntime::HProblemMissionRuntime(
    hcore::CaseConfig case_config,
    std::optional<hcore::MissionPlan> mission_plan,
    EventPublisher *publisher)
    : case_config_(std::move(case_config)),
      mission_plan_(std::move(mission_plan)),
      publisher_(publisher) {}

int HProblemMissionRuntime::execute(
    competition::CommandState &state,
    double sleep_scale) {
    last_error_.clear();
    if (publisher_ == nullptr) {
        last_error_ = "事件发布器未配置";
        return 1;
    }

    QString error;
    const QVector<hcore::SimMessage> messages = hcore::simulateMessages(case_config_, mission_plan_, &error);
    if (!error.isEmpty() || messages.isEmpty()) {
        last_error_ = error.isEmpty() ? "仿真消息为空" : error;
        return 1;
    }

    hcore::MissionPlan active_plan = mission_plan_.value_or(hcore::MissionPlan{});
    quint64 sequence = 1;
    for (const hcore::SimMessage &message : messages) {
        if (state.stop_requested) {
            break;
        }

        switch (message.type) {
        case hcore::SimMessageType::Config:
            active_plan = message.mission_plan;
            continue;
        case hcore::SimMessageType::Telemetry:
            if (!publisher_->publishEvent(sequence++, telemetryEventFromMessage(message, active_plan))) {
                return 1;
            }
            if (message.tick_interval_ms > 0 && sleep_scale > 0.0) {
                QThread::msleep(static_cast<unsigned long>(message.tick_interval_ms * sleep_scale));
            }
            break;
        case hcore::SimMessageType::Detection:
            if (!publisher_->publishEvent(sequence++, detectionEventFromMessage(message, active_plan))) {
                return 1;
            }
            break;
        case hcore::SimMessageType::Summary:
            if (!publisher_->publishSummary(sequence++, summaryFromMessage(message, active_plan))) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

QString HProblemMissionRuntime::lastError() const {
    return last_error_;
}

std::optional<hcore::MissionPlan> loadOptionalMissionPlan(const QString &path, QString *error_message) {
    if (!QFileInfo::exists(path)) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return std::nullopt;
    }

    QString task_plan_error;
    const auto task_plan = hcore::loadTaskPlan(path, &task_plan_error);
    if (task_plan.has_value()) {
        QString conversion_error;
        const auto mission_plan = hcore::missionPlanFromTaskPlan(task_plan.value(), &conversion_error);
        if (mission_plan.has_value()) {
            if (error_message != nullptr) {
                error_message->clear();
            }
            return mission_plan;
        }
        task_plan_error = conversion_error;
    }

    QString mission_plan_error;
    const auto legacy_plan = hcore::loadMissionPlan(path, &mission_plan_error);
    if (legacy_plan.has_value()) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return legacy_plan;
    }

    if (error_message != nullptr) {
        *error_message = QString("无法加载任务计划为通用 TaskPlan（%1）或旧 MissionPlan（%2）")
                             .arg(task_plan_error, mission_plan_error);
    }
    return std::nullopt;
}

hcore::MissionPlan selectMissionPlan(
    const hcore::CaseConfig &case_config,
    std::optional<hcore::MissionPlan> stored_plan,
    QString *error_message) {
    if (stored_plan.has_value() && !stored_plan->route.isEmpty()) {
        if (error_message != nullptr) {
            error_message->clear();
        }
        return stored_plan.value();
    }

    const auto generated_plan = hcore::buildMissionPlan(case_config, std::nullopt, error_message);
    if (generated_plan.has_value()) {
        return generated_plan.value();
    }
    return {};
}

} // namespace airborne
