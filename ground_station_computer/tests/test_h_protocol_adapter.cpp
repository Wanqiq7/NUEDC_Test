#include <QtTest/QtTest>

#include <QJsonDocument>
#include <QJsonObject>

#include <limits>
#include <optional>

#include "h_problem/mission/h_protocol_adapter.h"
#include "h_problem_core/mission/case_loader.h"
#include "h_problem_core/mission/mission_planning.h"
#include "h_problem_core/planning/mission_geometry.h"

namespace {

std::optional<competition::TaskPlan> generatedPlan(QString *error_message) {
    const auto maybe_case = hcore::loadCase("shared/cases/sample_case.json", error_message);
    if (!maybe_case.has_value()) {
        return std::nullopt;
    }
    return hcore::buildTaskPlan(maybe_case.value(), {}, error_message);
}

QJsonObject metadataFromPlan(const competition::TaskPlan &plan) {
    return QJsonDocument::fromJson(plan.metadata_json.toUtf8()).object();
}

QString compactJson(const QJsonObject &object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

HGridConfigData sentinelData() {
    HGridConfigData data;
    data.start_cell = "sentinel-start";
    data.route = {"sentinel-route-1", "sentinel-route-2"};
    data.terminal_cell = "sentinel-terminal";
    data.touchdown_x_cm = 123.0;
    data.touchdown_y_cm = 456.0;
    return data;
}

} // namespace

class HProtocolAdapterTests : public QObject {
    Q_OBJECT

private slots:
    void acceptsGeneratedExecutablePlan();
    void rejectsMissingWrongAndNonStringExecutionContract_data();
    void rejectsMissingWrongAndNonStringExecutionContract();
    void decodedRouteExcludesTouchdown();
    void rejectsNonFiniteCoordinates();
    void rejectsUnknownAction();
    void rejectsTakeoffOutsideFirstWaypoint();
    void rejectsNonA9B1Start();
    void rejectsRouteCoordinateMismatch();
    void rejectsLandMetadataMismatch();
};

void HProtocolAdapterTests::acceptsGeneratedExecutablePlan() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    const competition::TaskPlan plan = maybe_plan.value();
    QCOMPARE(plan.waypoints.first().action, QString("takeoff"));
    QCOMPARE(plan.waypoints.last().action, QString("land"));

    QVERIFY2(HProtocolAdapter::validateTaskPlan(plan, &error), qPrintable(error));
    HGridConfigData decoded;
    QVERIFY2(HProtocolAdapter::decodeTaskPlan(plan, &decoded, &error), qPrintable(error));
}

void HProtocolAdapterTests::rejectsMissingWrongAndNonStringExecutionContract_data() {
    QTest::addColumn<QString>("metadata_json");

    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    const QJsonObject valid_metadata = metadataFromPlan(maybe_plan.value());

    QTest::newRow("malformed-json") << QString("{");

    QJsonObject missing = valid_metadata;
    missing.remove("execution_contract");
    QTest::newRow("missing") << compactJson(missing);

    QJsonObject wrong = valid_metadata;
    wrong["execution_contract"] = "wrong-contract";
    QTest::newRow("wrong-string") << compactJson(wrong);

    QJsonObject null_value = valid_metadata;
    null_value["execution_contract"] = QJsonValue::Null;
    QTest::newRow("null") << compactJson(null_value);

    QJsonObject number = valid_metadata;
    number["execution_contract"] = 1;
    QTest::newRow("number") << compactJson(number);

    QJsonObject object = valid_metadata;
    object["execution_contract"] = QJsonObject{{"name", "h_field_m_v1"}};
    QTest::newRow("object") << compactJson(object);
}

void HProtocolAdapterTests::rejectsMissingWrongAndNonStringExecutionContract() {
    QFETCH(QString, metadata_json);

    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    competition::TaskPlan plan = maybe_plan.value();
    plan.metadata_json = metadata_json;

    error.clear();
    QVERIFY(!HProtocolAdapter::validateTaskPlan(plan, &error));
    QVERIFY(!error.isEmpty());

    HGridConfigData decoded = sentinelData();
    error.clear();
    QVERIFY(!HProtocolAdapter::decodeTaskPlan(plan, &decoded, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(decoded.start_cell, QString("sentinel-start"));
    QCOMPARE(decoded.route, QStringList({"sentinel-route-1", "sentinel-route-2"}));
    QCOMPARE(decoded.terminal_cell, QString("sentinel-terminal"));
    QCOMPARE(decoded.touchdown_x_cm, 123.0);
    QCOMPARE(decoded.touchdown_y_cm, 456.0);
}

void HProtocolAdapterTests::decodedRouteExcludesTouchdown() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    const competition::TaskPlan plan = maybe_plan.value();
    const QJsonObject metadata = metadataFromPlan(plan);

    HGridConfigData decoded;
    QVERIFY2(HProtocolAdapter::decodeTaskPlan(plan, &decoded, &error), qPrintable(error));
    QVERIFY(!decoded.route.contains("touchdown"));
    QCOMPARE(plan.waypoints.last().id, QString("touchdown"));
    QCOMPARE(decoded.route.last(), metadata.value("terminal_cell").toString());
}

void HProtocolAdapterTests::rejectsNonFiniteCoordinates() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    competition::TaskPlan non_finite = maybe_plan.value();
    QVERIFY(non_finite.waypoints.size() > 1);
    non_finite.waypoints[1].x = std::numeric_limits<double>::infinity();

    QVERIFY(!HProtocolAdapter::validateTaskPlan(non_finite, &error));
    QVERIFY(!error.isEmpty());
}

void HProtocolAdapterTests::rejectsUnknownAction() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    competition::TaskPlan plan = maybe_plan.value();
    QVERIFY(plan.waypoints.size() > 1);
    plan.waypoints[1].action = "hover";

    QVERIFY(!HProtocolAdapter::validateTaskPlan(plan, &error));
    QVERIFY(!error.isEmpty());
}

void HProtocolAdapterTests::rejectsTakeoffOutsideFirstWaypoint() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    competition::TaskPlan plan = maybe_plan.value();
    QVERIFY(plan.waypoints.size() > 1);
    plan.waypoints[1].action = "takeoff";

    QVERIFY(!HProtocolAdapter::validateTaskPlan(plan, &error));
    QVERIFY(!error.isEmpty());
}

void HProtocolAdapterTests::rejectsNonA9B1Start() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    competition::TaskPlan plan = maybe_plan.value();
    QJsonObject metadata = metadataFromPlan(plan);

    competition::TaskWaypoint alternate_start = plan.waypoints.at(1);
    alternate_start.action = "takeoff";
    plan.waypoints.prepend(alternate_start);
    plan.waypoints[1].action = "navigate";
    for (int index = 0; index < plan.waypoints.size(); ++index) {
        plan.waypoints[index].sequence_index = static_cast<quint32>(index);
    }
    plan.start_waypoint_id = "A8B1";
    metadata["start_cell"] = "A8B1";
    plan.metadata_json = compactJson(metadata);

    QVERIFY(!HProtocolAdapter::validateTaskPlan(plan, &error));
    QVERIFY(!error.isEmpty());
}

void HProtocolAdapterTests::rejectsRouteCoordinateMismatch() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    competition::TaskPlan plan = maybe_plan.value();
    QVERIFY(plan.waypoints.size() > 1);
    plan.waypoints[1].x += 0.01;

    QVERIFY(!HProtocolAdapter::validateTaskPlan(plan, &error));
    QVERIFY(!error.isEmpty());
}

void HProtocolAdapterTests::rejectsLandMetadataMismatch() {
    QString error;
    const auto maybe_plan = generatedPlan(&error);
    QVERIFY2(maybe_plan.has_value(), qPrintable(error));
    const competition::TaskPlan plan = maybe_plan.value();
    QCOMPARE(plan.waypoints.last().action, QString("land"));

    competition::TaskPlan coordinate_mismatch = plan;
    coordinate_mismatch.waypoints.last().x += 0.01;
    QVERIFY(!HProtocolAdapter::validateTaskPlan(coordinate_mismatch, &error));
    QVERIFY(!error.isEmpty());

    competition::TaskPlan altitude_mismatch = plan;
    altitude_mismatch.waypoints.last().z = 0.01;
    error.clear();
    QVERIFY(!HProtocolAdapter::validateTaskPlan(altitude_mismatch, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_MAIN(HProtocolAdapterTests)
#include "test_h_protocol_adapter.moc"
