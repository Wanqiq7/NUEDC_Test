#include "framework/config/network_config.h"

#include <QByteArray>
#include <QProcessEnvironment>

namespace {

int parsePort(const QByteArray &value, int fallback) {
    bool ok = false;
    const int parsed = QString::fromUtf8(value).toInt(&ok);
    return ok && parsed > 0 ? parsed : fallback;
}

QString endpointFor(const QString &host, int port) {
    return QString("tcp://%1:%2").arg(host).arg(port);
}

} // namespace

NetworkConfig NetworkConfig::fromEnvironment() {
    NetworkConfig config;
    const auto environment = QProcessEnvironment::systemEnvironment();

    const QString host = environment.value("NUEDC_AIRBORNE_HOST");
    if (!host.isEmpty()) {
        config.airborne_host = host;
    }

    const QByteArray telemetry_port = qgetenv("NUEDC_TELEMETRY_PORT");
    if (!telemetry_port.isEmpty()) {
        config.telemetry_port = parsePort(telemetry_port, config.telemetry_port);
    }

    const QByteArray command_port = qgetenv("NUEDC_COMMAND_PORT");
    if (!command_port.isEmpty()) {
        config.command_port = parsePort(command_port, config.command_port);
    }

    return config;
}

QString NetworkConfig::telemetryEndpoint() const {
    return endpointFor(airborne_host, telemetry_port);
}

QString NetworkConfig::commandEndpoint() const {
    return endpointFor(airborne_host, command_port);
}
