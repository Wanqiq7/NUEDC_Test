#pragma once

#include <QString>

struct NetworkConfig {
    QString airborne_host = "127.0.0.1";
    int telemetry_port = 5557;
    int command_port = 5558;

    static NetworkConfig fromEnvironment();
    QString telemetryEndpoint() const;
    QString commandEndpoint() const;
};
