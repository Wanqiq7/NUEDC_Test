#include <QtTest/QtTest>

#include "framework/config/network_config.h"

class NetworkConfigTests : public QObject {
    Q_OBJECT

private slots:
    void usesDefaultsWhenEnvMissing();
    void readsEnvironmentOverrides();
};

void NetworkConfigTests::usesDefaultsWhenEnvMissing() {
    qunsetenv("NUEDC_AIRBORNE_HOST");
    qunsetenv("NUEDC_TELEMETRY_PORT");
    qunsetenv("NUEDC_COMMAND_PORT");

    const auto config = NetworkConfig::fromEnvironment();
    QCOMPARE(config.telemetryEndpoint(), QString("tcp://127.0.0.1:5557"));
    QCOMPARE(config.commandEndpoint(), QString("tcp://127.0.0.1:5558"));
}

void NetworkConfigTests::readsEnvironmentOverrides() {
    qputenv("NUEDC_AIRBORNE_HOST", "192.168.10.20");
    qputenv("NUEDC_TELEMETRY_PORT", "6001");
    qputenv("NUEDC_COMMAND_PORT", "6002");

    const auto config = NetworkConfig::fromEnvironment();
    QCOMPARE(config.telemetryEndpoint(), QString("tcp://192.168.10.20:6001"));
    QCOMPARE(config.commandEndpoint(), QString("tcp://192.168.10.20:6002"));

    qunsetenv("NUEDC_AIRBORNE_HOST");
    qunsetenv("NUEDC_TELEMETRY_PORT");
    qunsetenv("NUEDC_COMMAND_PORT");
}

QTEST_MAIN(NetworkConfigTests)
#include "test_network_config.moc"
