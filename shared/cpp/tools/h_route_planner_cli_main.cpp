#include "h_problem_core/tools/planner_cli.h"

#include <QFile>

int main() {
    QFile input;
    QFile output;
    QFile error;
    input.open(stdin, QIODevice::ReadOnly, QFileDevice::DontCloseHandle);
    output.open(stdout, QIODevice::WriteOnly, QFileDevice::DontCloseHandle);
    error.open(stderr, QIODevice::WriteOnly, QFileDevice::DontCloseHandle);

    const hcore::PlannerCliResult result = hcore::runPlannerCliRequest(input.readAll());
    output.write(result.stdout_bytes);
    error.write(result.stderr_bytes);
    return result.exit_code;
}
