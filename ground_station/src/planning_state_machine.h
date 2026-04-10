#pragma once

#include <QString>

enum class PlanningUiState {
    IdlePreview,
    SelectingNoFlyZone,
    ReadyToGenerate
};

class PlanningStateMachine {
public:
    PlanningStateMachine();

    PlanningUiState state() const;
    QString primaryButtonText() const;
    void handlePrimaryButtonClicked();
    void updateSelectionValidity(bool valid);
    void handleGenerationSucceeded();

private:
    PlanningUiState m_state;
};
