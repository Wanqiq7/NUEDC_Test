#include "competition_core/mission/task_plan_store.h"
#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"

#include <QCoreApplication>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().size() != 3) {
        QTextStream(stderr) << "usage: generate_h_runtime_fixture CASE OUTPUT\n";
        return 2;
    }
    QString error;
    const auto config = hcore::loadCase(application.arguments().at(1), &error);
    if (!config.has_value()) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    const auto plan = hcore::buildTaskPlan(config.value(), std::nullopt, &error);
    if (!plan.has_value() ||
        !competition::storeTaskPlan(plan.value(), application.arguments().at(2), &error)) {
        QTextStream(stderr) << error << '\n';
        return 1;
    }
    return 0;
}
