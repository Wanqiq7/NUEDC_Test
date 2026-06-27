#pragma once

#include "competition_core/task/models.h"
#include "framework/communication/envelope_codec.h"
#include "framework/runtime/mission_runtime_state.h"

#include <QtGlobal>
#include <QString>
#include <QVector>

#include <functional>
#include <utility>

class ZmqCommandClient;
class QWidget;

class CompetitionTaskAdapter {
public:
    using TextCallback = std::function<void(const QString &)>;
    using RuntimeCallback = std::function<void()>;

    virtual ~CompetitionTaskAdapter() = default;

    void setStatusTextCallback(TextCallback callback) { status_text_callback_ = std::move(callback); }
    void setPlanningButtonTextCallback(TextCallback callback) { planning_button_text_callback_ = std::move(callback); }
    void setRuntimeCallback(RuntimeCallback callback) { runtime_callback_ = std::move(callback); }

    virtual QWidget *createTaskView(QWidget *parent) = 0;
    virtual QString initialPlanningButtonText() const = 0;
    virtual QString activeTaskId() const = 0;
    virtual bool missionSyncedToAirborne() const = 0;
    virtual bool missionRunning() const = 0;
    virtual MissionRuntimeInputs missionRuntimeInputs() const = 0;

    virtual void setCommandSyncEnabled(bool enabled) = 0;
    virtual void setCommandClient(const ZmqCommandClient &client) = 0;
    virtual void loadInitialPreview() = 0;
    virtual void handleTaskPlan(const competition::TaskPlan &plan) = 0;
    virtual void handleTaskEvent(const competition::TaskEvent &event, qint64 timestamp_ms) = 0;
    virtual void handleTaskSummary(const competition::TaskSummary &summary) = 0;
    virtual void handlePlanningButtonClicked() = 0;
    virtual void markControlCommandStarted() = 0;
    virtual void markControlCommandStopped() = 0;
    virtual void markAirborneSyncState(bool online, bool synced) = 0;
    virtual void applyCommandAck(const CommandSendResult &result) = 0;

protected:
    void notifyStatusTextChanged(const QString &text) const {
        if (status_text_callback_) {
            status_text_callback_(text);
        }
    }

    void notifyPlanningButtonTextChanged(const QString &text) const {
        if (planning_button_text_callback_) {
            planning_button_text_callback_(text);
        }
    }

    void notifyRuntimeChanged() const {
        if (runtime_callback_) {
            runtime_callback_();
        }
    }

private:
    TextCallback status_text_callback_;
    TextCallback planning_button_text_callback_;
    RuntimeCallback runtime_callback_;
};

struct CompetitionTaskAdapterDescriptor {
    QString adapter_id;
    QString display_name;
    std::function<CompetitionTaskAdapter *()> create;
};

QVector<CompetitionTaskAdapterDescriptor> availableCompetitionTaskAdapters();
QString configuredCompetitionTaskAdapterId();
CompetitionTaskAdapter *createCompetitionTaskAdapter(const QString &adapter_id, QString *error_message = nullptr);
CompetitionTaskAdapter *createConfiguredCompetitionTaskAdapter(QString *error_message = nullptr);
CompetitionTaskAdapter *createDefaultCompetitionTaskAdapter();

