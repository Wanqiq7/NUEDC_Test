#include "h_problem/ui/h_problem_page.h"

#include "framework/task/competition_task_adapter.h"
#include "h_problem/h_problem_adapter_registration.h"
#include "h_problem/ui/h_grid_scene.h"

GridScene *HProblemPage::createGridScene(QObject *parent) {
    auto *scene = new GridScene(parent);
    scene->setObjectName("GridScene");
    return scene;
}

HProblemTaskAdapter::HProblemTaskAdapter()
    : controller_(std::make_unique<HMissionController>(
          &view_,
          [this](const QString &text) { notifyStatusTextChanged(text); },
          [this](const QString &text) { notifyPlanningButtonTextChanged(text); },
          [this]() { notifyRuntimeChanged(); },
          [this](const CommandSendResult &result) { notifyCommandLinkStateChanged(result); })) {}

QWidget *HProblemTaskAdapter::createTaskView(QWidget *parent) {
    view_.setCellClickedHandler([this](const QString &cell_code) {
        controller_->handleGridSceneCellClicked(cell_code);
    });
    return view_.buildWidget(parent, controller_->detectionTotals());
}

QString HProblemTaskAdapter::initialPlanningButtonText() const {
    return controller_->initialPlanningButtonText();
}

QString HProblemTaskAdapter::activeTaskId() const {
    return controller_->activeTaskId();
}

bool HProblemTaskAdapter::missionSyncedToAirborne() const {
    return controller_->missionSyncedToAirborne();
}

bool HProblemTaskAdapter::missionRunning() const {
    return controller_->missionRunning();
}

MissionRuntimeInputs HProblemTaskAdapter::missionRuntimeInputs() const {
    return controller_->missionRuntimeInputs();
}

void HProblemTaskAdapter::setCommandSyncEnabled(bool enabled) {
    controller_->setCommandSyncEnabled(enabled);
}

void HProblemTaskAdapter::setCommandClient(const ZmqCommandClient &client) {
    controller_->setCommandClient(client);
}

void HProblemTaskAdapter::setCommandTransport(const CommandTransport *transport) {
    controller_->setCommandTransport(transport);
}

void HProblemTaskAdapter::loadInitialPreview() {
    controller_->loadInitialPreview();
}

void HProblemTaskAdapter::handleTaskPlan(const competition::TaskPlan &plan) {
    controller_->handleTaskPlan(plan);
}

void HProblemTaskAdapter::handleTaskEvent(const competition::TaskEvent &event, qint64 timestamp_ms) {
    controller_->handleTaskEvent(event, timestamp_ms);
}

void HProblemTaskAdapter::handleTaskSummary(const competition::TaskSummary &summary) {
    controller_->handleTaskSummary(summary);
}

void HProblemTaskAdapter::handlePlanningButtonClicked() {
    controller_->handlePlanningButtonClicked();
}

void HProblemTaskAdapter::markControlCommandStarted() {
    controller_->markControlCommandStarted();
}

void HProblemTaskAdapter::markControlCommandStopped() {
    controller_->markControlCommandStopped();
}

void HProblemTaskAdapter::markAirborneSyncState(bool online, bool synced) {
    controller_->markAirborneSyncState(online, synced);
}

void HProblemTaskAdapter::applyCommandAck(const CommandSendResult &result) {
    controller_->applyCommandAck(result);
}

CompetitionTaskAdapterDescriptor hProblemTaskAdapterDescriptor() {
    return CompetitionTaskAdapterDescriptor{
        QStringLiteral("h_problem"),
        QStringLiteral("H 题野生动物巡检"),
        []() -> std::unique_ptr<CompetitionTaskAdapter> {
            return std::make_unique<HProblemTaskAdapter>();
        },
    };
}
