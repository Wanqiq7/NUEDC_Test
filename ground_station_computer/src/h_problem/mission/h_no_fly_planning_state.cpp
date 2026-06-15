#include "h_problem/mission/h_no_fly_planning_state.h"

#include <QtGlobal>
#include <QStringLiteral>

PlanningStateMachine::PlanningStateMachine()
    : m_state(PlanningUiState::IdlePreview) {
}

PlanningUiState PlanningStateMachine::state() const {
    return m_state;
}

QString PlanningStateMachine::primaryButtonText() const {
    switch (m_state) {
    case PlanningUiState::IdlePreview:
        return QStringLiteral("设置禁飞区");
    case PlanningUiState::SelectingNoFlyZone:
        return QStringLiteral("设置禁飞区");
    case PlanningUiState::ReadyToGenerate:
        return QStringLiteral("航线生成");
    }
    Q_UNREACHABLE();
    return {};
}

void PlanningStateMachine::handlePrimaryButtonClicked() {
    if (m_state == PlanningUiState::IdlePreview) {
        m_state = PlanningUiState::SelectingNoFlyZone;
    }
}

void PlanningStateMachine::updateSelectionValidity(bool valid) {
    switch (m_state) {
    case PlanningUiState::SelectingNoFlyZone:
        m_state = valid ? PlanningUiState::ReadyToGenerate : PlanningUiState::SelectingNoFlyZone;
        break;
    case PlanningUiState::ReadyToGenerate:
        if (!valid) {
            m_state = PlanningUiState::SelectingNoFlyZone;
        }
        break;
    default:
        break;
    }
}

void PlanningStateMachine::handleGenerationSucceeded() {
    m_state = PlanningUiState::IdlePreview;
}
