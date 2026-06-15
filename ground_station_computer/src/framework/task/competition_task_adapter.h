#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

class ZmqCommandClient;
class QWidget;

struct TaskGridConfig {
    QString case_id;
    QString start_cell;
    QStringList no_fly_cells;
    QStringList route;
    QString terminal_cell;
    bool landing_enabled = false;
    double descent_angle_deg = 0.0;
    double takeoff_anchor_x_cm = 0.0;
    double takeoff_anchor_y_cm = 0.0;
};

class CompetitionTaskAdapter {
public:
    using TextCallback = std::function<void(const QString &)>;
    using RuntimeCallback = std::function<void(bool, bool)>;
    using DetectionCallback = std::function<void(const QString &)>;
    using SummaryCallback = std::function<void(const QMap<QString, int> &)>;

    virtual ~CompetitionTaskAdapter() = default;

    void setStatusTextCallback(TextCallback callback) { status_text_callback_ = std::move(callback); }
    void setCaseTextCallback(TextCallback callback) { case_text_callback_ = std::move(callback); }
    void setMissionTextCallback(TextCallback callback) { mission_text_callback_ = std::move(callback); }
    void setPlanningButtonTextCallback(TextCallback callback) { planning_button_text_callback_ = std::move(callback); }
    void setRuntimeCallback(RuntimeCallback callback) { runtime_callback_ = std::move(callback); }
    void setDetectionCallback(DetectionCallback callback) { detection_callback_ = std::move(callback); }
    void setSummaryCallback(SummaryCallback callback) { summary_callback_ = std::move(callback); }

    virtual QWidget *createTaskView(QWidget *parent) = 0;
    virtual QString initialPlanningButtonText() const = 0;
    virtual QString activeTaskId() const = 0;
    virtual bool missionSyncedToAirborne() const = 0;
    virtual bool missionRunning() const = 0;

    virtual void setCommandSyncEnabled(bool enabled) = 0;
    virtual void setCommandClient(const ZmqCommandClient &client) = 0;
    virtual void loadInitialPreview() = 0;
    virtual void handleGridConfig(const TaskGridConfig &config) = 0;
    virtual void handleTelemetry(const QString &current_cell, int step_index, int visited_cells) = 0;
    virtual void handleDetection(const QString &cell_code, const QString &animal_name, int count, qint64 timestamp_ms) = 0;
    virtual void handleSummary(const QMap<QString, int> &totals, int visited_cells) = 0;
    virtual void handlePlanningButtonClicked() = 0;
    virtual void markControlCommandStarted() = 0;
    virtual void markControlCommandStopped() = 0;
    virtual void markAirborneSyncState(bool online, bool synced) = 0;

protected:
    void notifyStatusTextChanged(const QString &text) const {
        if (status_text_callback_) {
            status_text_callback_(text);
        }
    }

    void notifyCaseTextChanged(const QString &text) const {
        if (case_text_callback_) {
            case_text_callback_(text);
        }
    }

    void notifyMissionTextChanged(const QString &text) const {
        if (mission_text_callback_) {
            mission_text_callback_(text);
        }
    }

    void notifyPlanningButtonTextChanged(const QString &text) const {
        if (planning_button_text_callback_) {
            planning_button_text_callback_(text);
        }
    }

    void notifyRuntimeChanged(bool synced_to_airborne, bool running) const {
        if (runtime_callback_) {
            runtime_callback_(synced_to_airborne, running);
        }
    }

    void notifyDetectionRecordAdded(const QString &text) const {
        if (detection_callback_) {
            detection_callback_(text);
        }
    }

    void notifySummaryTotalsChanged(const QMap<QString, int> &totals) const {
        if (summary_callback_) {
            summary_callback_(totals);
        }
    }

private:
    TextCallback status_text_callback_;
    TextCallback case_text_callback_;
    TextCallback mission_text_callback_;
    TextCallback planning_button_text_callback_;
    RuntimeCallback runtime_callback_;
    DetectionCallback detection_callback_;
    SummaryCallback summary_callback_;
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
