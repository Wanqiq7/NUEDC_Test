#pragma once

#include "competition_core/task/models.h"

#include <functional>

namespace airborne {

using TaskEventPublishCallback = std::function<bool(quint64, const competition::TaskEvent &)>;
using TaskSummaryPublishCallback = std::function<bool(quint64, const competition::TaskSummary &)>;

class EventPublisher {
public:
    virtual ~EventPublisher() = default;

    virtual bool publishEvent(quint64 sequence, const competition::TaskEvent &event) = 0;
    virtual bool publishSummary(quint64 sequence, const competition::TaskSummary &summary) = 0;
};

class CallbackEventPublisher final : public EventPublisher {
public:
    CallbackEventPublisher(TaskEventPublishCallback event_callback, TaskSummaryPublishCallback summary_callback);

    bool publishEvent(quint64 sequence, const competition::TaskEvent &event) override;
    bool publishSummary(quint64 sequence, const competition::TaskSummary &summary) override;

private:
    TaskEventPublishCallback event_callback_;
    TaskSummaryPublishCallback summary_callback_;
};

class MissionRuntime {
public:
    virtual ~MissionRuntime() = default;

    virtual int execute(
        competition::CommandState &state,
        double sleep_scale) = 0;
};

} // namespace airborne
