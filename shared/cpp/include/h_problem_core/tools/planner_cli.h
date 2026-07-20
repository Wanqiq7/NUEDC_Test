#pragma once

#include <QByteArray>

namespace hcore {

struct PlannerCliResult {
    int exit_code = 4;
    QByteArray stdout_bytes;
    QByteArray stderr_bytes;
};

PlannerCliResult runPlannerCliRequest(const QByteArray &request_bytes);

} // namespace hcore
