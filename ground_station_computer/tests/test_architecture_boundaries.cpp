#include <QtTest/QtTest>

#include <QFile>
#include <QRegularExpression>

namespace {

// 将相对于源码根的路径解析为绝对路径。构建时 CMake 通过 GROUND_PROJECT_SOURCE_DIR
// 注入源码根（= 顶层 project() 的 Ground/），使测试不依赖 ctest 的运行工作目录；
// 未定义时回退为相对路径（直接运行需在 Ground/ 下）。
QString resolveSourcePath(const QString &relative_path) {
#ifdef GROUND_PROJECT_SOURCE_DIR
    return QStringLiteral(GROUND_PROJECT_SOURCE_DIR) + QLatin1Char('/') + relative_path;
#else
    return relative_path;
#endif
}

// 打开待扫描的源文件并返回内容。文件缺失时给出明确诊断，与「架构约束被违反」区分开，
// 避免路径问题伪装成边界回归。
QString readSourceOrFail(const QString &relative_path) {
    const QString absolute_path = resolveSourcePath(relative_path);
    QFile file(absolute_path);
    const bool opened = file.open(QIODevice::ReadOnly | QIODevice::Text);
    [&]() {
        QVERIFY2(
            opened,
            qPrintable(QStringLiteral("无法打开待扫描源文件（路径解析失败，非架构违规）: %1")
                           .arg(absolute_path)));
    }();
    return QString::fromUtf8(file.readAll());
}

} // namespace

class ArchitectureBoundaryTests : public QObject {
    Q_OBJECT

private slots:
    void mainWindowDoesNotIncludeProblemSpecificHeaders();
    void mainWindowUsesMissionCommandServiceForControlCommands();
    void mainWindowUsesConfiguredTaskAdapterFactory();
    void adapterRegistryLivesInFrameworkNotProblemModule();
    void hProblemAdapterIsThinShimOverControllerAndView();
    void frameworkCommunicationDoesNotIncludeProblemSpecificHeaders();
    void subscriberWorkerPublishesGenericTaskSignals();
    void competitionTaskAdapterExposesGenericProtocolHandlers();
    void mainWindowDoesNotOwnProblemSpecificBusinessPanels();
    void documentsCanonicalTaskPlanStorage();
};

void ArchitectureBoundaryTests::mainWindowDoesNotIncludeProblemSpecificHeaders() {
    const QString source = readSourceOrFail("ground_station_computer/src/app/main_window.cpp");

    const QRegularExpression forbidden_include(R"(#include\s+["<][^">]*h_problem[^">]*[">])");
    QVERIFY2(
        !forbidden_include.match(source).hasMatch(),
        "MainWindow must depend on CompetitionTaskAdapter factory instead of h_problem headers");
}

void ArchitectureBoundaryTests::mainWindowUsesConfiguredTaskAdapterFactory() {
    const QString source_text = readSourceOrFail("ground_station_computer/src/app/main_window.cpp");
    QVERIFY(source_text.contains("createConfiguredCompetitionTaskAdapter"));
    QVERIFY(source_text.contains("createDefaultCompetitionTaskAdapter"));
}
void ArchitectureBoundaryTests::mainWindowUsesMissionCommandServiceForControlCommands() {
    const QString header_source = readSourceOrFail("ground_station_computer/src/app/main_window.h");
    QVERIFY(header_source.contains("MissionCommandService"));
    QVERIFY(header_source.contains("SerializedCommandTransport"));
    QVERIFY(header_source.contains("CommandLinkMonitor"));

    const QString source_text = readSourceOrFail("ground_station_computer/src/app/main_window.cpp");
    QVERIFY(source_text.contains("mission_command_service_->sendControlCommand"));
    QVERIFY(source_text.contains("command_link_monitor_->requestImmediateProbe"));
    QVERIFY(source_text.contains("command_link_monitor_->recordExternalCommandResult"));
}

void ArchitectureBoundaryTests::adapterRegistryLivesInFrameworkNotProblemModule() {
    // 注册聚合与工厂选择逻辑必须落在框架自有 TU，题目模块只暴露自己的 descriptor。
    // 这样新增题目无需编辑任何已有题目文件，符合 adding_task_adapter.md 的边界。
    const QString registry = readSourceOrFail(
        "ground_station_computer/src/framework/task/competition_task_registry.cpp");
    QVERIFY2(registry.contains("createCompetitionTaskAdapter"),
             "framework registry must own createCompetitionTaskAdapter");
    QVERIFY2(registry.contains("createConfiguredCompetitionTaskAdapter"),
             "framework registry must own createConfiguredCompetitionTaskAdapter");
    QVERIFY2(registry.contains("defaultCompetitionTaskAdapterId"),
             "default adapter id must have a single source of truth in the framework registry");

    // 题目页面不得再定义工厂选择逻辑（防止回退到题目模块承担注册聚合职责）。
    const QString page = readSourceOrFail("ground_station_computer/src/h_problem/ui/h_problem_page.cpp");
    const QRegularExpression defines_selector(
        R"(std::unique_ptr<CompetitionTaskAdapter>\s+createCompetitionTaskAdapter\s*\()");
    QVERIFY2(!defines_selector.match(page).hasMatch(),
             "h_problem_page.cpp must not define the adapter selection factory");
    QVERIFY2(page.contains("hProblemTaskAdapterDescriptor"),
             "h_problem module must expose its descriptor provider instead");
}

void ArchitectureBoundaryTests::hProblemAdapterIsThinShimOverControllerAndView() {
    // MVC 拆分回归护栏：Adapter 应是薄适配层——装配 view 与 controller 并转发接口，
    // 不再承载工作流 / 状态 / UI 构建。若这些职责回流到 adapter，说明上帝对象在复活。
    const QString adapter = readSourceOrFail("ground_station_computer/src/h_problem/ui/h_problem_page.cpp");
    QVERIFY2(adapter.contains("HMissionController"),
             "adapter must delegate to HMissionController");
    QVERIFY2(adapter.contains("controller_->"),
             "adapter must forward interface calls to the controller");

    // 工作流逻辑（规划状态机 / 航线桥 / 命令服务）不应再直接出现在 adapter TU。
    QVERIFY2(!adapter.contains("PlanningStateMachine"),
             "planning workflow must live in the controller, not the adapter");
    QVERIFY2(!adapter.contains("HRoutePlanner"),
             "plan generation must live in the controller, not the adapter");
    QVERIFY2(!adapter.contains("DetectionRepository"),
             "detection persistence must live in the controller, not the adapter");

    // 控制器仅依赖窄的视图接口，不依赖具体 UI 控件类型（保持可 mock 单测）。
    const QString controller_header =
        readSourceOrFail("ground_station_computer/src/h_problem/mission/h_mission_controller.h");
    QVERIFY2(controller_header.contains("HMissionViewSink"),
             "controller must drive the view through the HMissionViewSink interface");
    const QRegularExpression widget_include(
        R"(#include\s+<Q(Widget|Label|ListWidget|TableWidget|GraphicsView)>)");
    QVERIFY2(!widget_include.match(controller_header).hasMatch(),
             "controller header must not depend on concrete Qt widget types");
}

void ArchitectureBoundaryTests::frameworkCommunicationDoesNotIncludeProblemSpecificHeaders() {
    const QStringList paths = {
        "ground_station_computer/src/framework/communication/zmq_subscriber_worker.h",
        "ground_station_computer/src/framework/communication/zmq_subscriber_worker.cpp",
    };

    for (const QString &path : paths) {
        const QString source = readSourceOrFail(path);
        QVERIFY2(
            !source.contains("h_problem"),
            qPrintable(QString("%1 must not depend on h_problem").arg(path)));
    }
}

void ArchitectureBoundaryTests::subscriberWorkerPublishesGenericTaskSignals() {
    const QString source = readSourceOrFail("ground_station_computer/src/framework/communication/zmq_subscriber_worker.h");

    QVERIFY(source.contains("taskPlanReceived"));
    QVERIFY(source.contains("taskEventReceived"));
    QVERIFY(source.contains("taskSummaryReceived"));
    QVERIFY(!source.contains("gridConfigReceived"));
    QVERIFY(!source.contains("telemetryReceived"));
    QVERIFY(!source.contains("detectionReceived"));
    QVERIFY(!source.contains("summaryReceived(QMap"));
}

void ArchitectureBoundaryTests::competitionTaskAdapterExposesGenericProtocolHandlers() {
    const QString source = readSourceOrFail("ground_station_computer/src/framework/task/competition_task_adapter.h");

    QVERIFY(source.contains("handleTaskPlan"));
    QVERIFY(source.contains("handleTaskEvent"));
    QVERIFY(source.contains("handleTaskSummary"));
    QVERIFY(!source.contains("TaskGridConfig"));
    QVERIFY(!source.contains("handleTelemetry"));
    QVERIFY(!source.contains("handleDetection"));
    QVERIFY(!source.contains("QMap<QString, int>"));
}

void ArchitectureBoundaryTests::mainWindowDoesNotOwnProblemSpecificBusinessPanels() {
    const QString header_source = readSourceOrFail("ground_station_computer/src/app/main_window.h");
    QVERIFY(!header_source.contains("handleGridConfig"));
    QVERIFY(!header_source.contains("handleTelemetry"));
    QVERIFY(!header_source.contains("handleDetection"));
    QVERIFY(!header_source.contains("QTableWidget"));
    QVERIFY(!header_source.contains("QListWidget"));

    const QString source_text = readSourceOrFail("ground_station_computer/src/app/main_window.cpp");
    QVERIFY(!source_text.contains("实时检测记录"));
    QVERIFY(!source_text.contains("统计汇总"));
    QVERIFY(!source_text.contains("动物"));
    QVERIFY(!source_text.contains("H 题混合联调地面站"));
}

void ArchitectureBoundaryTests::documentsCanonicalTaskPlanStorage() {
    const QString contents = readSourceOrFail("docs/framework_architecture.md");
    QVERIFY(contents.contains("运行时任务计划持久化统一使用 `competition::TaskPlan`"));
}

QTEST_MAIN(ArchitectureBoundaryTests)
#include "test_architecture_boundaries.moc"
