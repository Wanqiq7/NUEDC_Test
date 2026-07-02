#pragma once

#include "framework/task/competition_task_adapter.h"
#include "h_problem/mission/h_mission_controller.h"
#include "h_problem/ui/h_problem_view.h"

#include <QString>
#include <QtGlobal>
#include <QWidget>

#include <memory>

class GridScene;
class QObject;
class ZmqCommandClient;

class HProblemPage {
public:
    static GridScene *createGridScene(QObject *parent);
};

// H 题任务适配器：实现框架 CompetitionTaskAdapter 接口的薄适配层。它只负责装配
// 视图（HProblemView）与控制器（HMissionController）并把接口调用转发给控制器，
// 不含任何业务逻辑 / 状态 / UI 构建（后者已分别下沉到 controller 与 view）。
class HProblemTaskAdapter : public CompetitionTaskAdapter {
public:
    HProblemTaskAdapter();

    QWidget *createTaskView(QWidget *parent) override;
    QString initialPlanningButtonText() const override;
    QString activeTaskId() const override;
    bool missionSyncedToAirborne() const override;
    bool missionRunning() const override;
    MissionRuntimeInputs missionRuntimeInputs() const override;

    void setCommandSyncEnabled(bool enabled) override;
    void setCommandClient(const ZmqCommandClient &client) override;
    void loadInitialPreview() override;
    void handleTaskPlan(const competition::TaskPlan &plan) override;
    void handleTaskEvent(const competition::TaskEvent &event, qint64 timestamp_ms) override;
    void handleTaskSummary(const competition::TaskSummary &summary) override;
    void handlePlanningButtonClicked() override;
    void markControlCommandStarted() override;
    void markControlCommandStopped() override;
    void markAirborneSyncState(bool online, bool synced) override;
    void applyCommandAck(const CommandSendResult &result) override;

private:
    HProblemView view_;
    std::unique_ptr<HMissionController> controller_;
};
