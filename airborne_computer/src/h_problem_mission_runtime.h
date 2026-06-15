#pragma once

#include "airborne_runtime.h"

#include "h_problem_core/common/models.h"

#include <optional>

namespace airborne {

class HProblemMissionRuntime final : public MissionRuntime {
public:
    HProblemMissionRuntime(
        hcore::CaseConfig case_config,
        std::optional<hcore::MissionPlan> mission_plan,
        EventPublisher *publisher);

    int execute(
        competition::CommandState &state,
        double sleep_scale) override;
    QString lastError() const;

private:
    hcore::CaseConfig case_config_;
    std::optional<hcore::MissionPlan> mission_plan_;
    EventPublisher *publisher_ = nullptr;
    QString last_error_;
};

std::optional<hcore::MissionPlan> loadOptionalMissionPlan(const QString &path, QString *error_message = nullptr);

hcore::MissionPlan selectMissionPlan(
    const hcore::CaseConfig &case_config,
    std::optional<hcore::MissionPlan> stored_plan,
    QString *error_message = nullptr);

} // namespace airborne
