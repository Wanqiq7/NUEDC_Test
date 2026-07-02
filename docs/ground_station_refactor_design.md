# 地面站架构优化设计

本文档定义地面站（`ground_station_computer`）四项架构优化的目标、接口、文件落点、迁移步骤与测试计划。落地前需评审确认。改动只在地面站层，不触碰 `competition_core` / `h_problem_core` 通用契约，也不改协议（proto / Envelope）。

## 背景与现状

现有分层已成型：`competition_core`（通用内核）→ `h_problem_core`（题目算法）→ 地面站 `framework`（Shell / 通信 / Adapter 端口）→ 地面站 `h_problem`（题目 UI / 规则 / 存储）。`tests/test_architecture_boundaries.cpp` 已用字符串扫描把关键边界固化为回归门禁。

债务集中在四处，本设计按性价比排序处理：

1. Adapter 注册表长在题目模块里。
2. `HProblemTaskAdapter` 是上帝对象（691 行 / ~20 成员）。
3. ack / 运行态是 5 个散落标量，在 6 处手工同步。
4. 仓库根探测与硬编码路径嵌死在题目 UI。

落地顺序：① → ③ → ② → ④。① 机械且被边界测试保护，先做打底；③ 消除多处手工同步的 bug 面，为 ② 减负；② 改动面最大放在有测试护栏之后；④ 独立小范围收尾。

## 约束

- 不改 proto、Envelope、`competition::TaskPlan/TaskEvent/TaskSummary` 通用契约。
- 不改 `CompetitionTaskAdapter` 纯虚接口的对外语义（除非某项明确说明，见 ③）。
- 保留并尽量强化 `test_architecture_boundaries.cpp` 的现有断言。
- 每项改动后运行 `cmake --build build` + `ctest --test-dir build --output-on-failure`。本机当前无 Qt6 / protoc（仅 STM32CubeCLT 附带 cmake 3.28），编译验证需先按末节配置工具链。

---

## ① 抽出框架级 Adapter 注册表

### 问题

`competition_task_adapter.h:80-84` 声明的工厂函数——`availableCompetitionTaskAdapters()`、`configuredCompetitionTaskAdapterId()`、`createCompetitionTaskAdapter()`、`createConfiguredCompetitionTaskAdapter()`、`createDefaultCompetitionTaskAdapter()`——其定义全部落在 `h_problem/ui/h_problem_page.cpp:647-688`。`framework/task/` 下没有任何自有 `.cpp`。后果：

- 新增 D 题必须编辑 H 题文件才能注册，违反 `adding_task_adapter.md`「新增题目只加 Adapter、不动主流程」。
- 默认 adapter id `"h_problem"` 在三处硬编码（`configuredCompetitionTaskAdapterId`、`createCompetitionTaskAdapter`、`createDefaultCompetitionTaskAdapter`）。

### 目标结构

注册聚合逻辑迁到框架自有 TU；每个题目模块只暴露自己的 descriptor 提供函数，由注册表汇总。

新增文件：

- `src/framework/task/competition_task_registry.h`
- `src/framework/task/competition_task_registry.cpp`

`competition_task_registry.h`：

```cpp
#pragma once
#include "framework/task/competition_task_adapter.h"
#include <QString>
#include <QVector>

// 框架默认 adapter id 的唯一来源，消除三处硬编码。
QString defaultCompetitionTaskAdapterId();

// 聚合所有题目模块注册的 descriptor。
QVector<CompetitionTaskAdapterDescriptor> availableCompetitionTaskAdapters();
QString configuredCompetitionTaskAdapterId();
std::unique_ptr<CompetitionTaskAdapter> createCompetitionTaskAdapter(
    const QString &adapter_id, QString *error_message = nullptr);
std::unique_ptr<CompetitionTaskAdapter> createConfiguredCompetitionTaskAdapter(
    QString *error_message = nullptr);
std::unique_ptr<CompetitionTaskAdapter> createDefaultCompetitionTaskAdapter();
```

这些声明目前在 `competition_task_adapter.h:80-84`。迁移后保留在 `competition_task_adapter.h` 或转到 registry 头，二选一：

- 方案 A（推荐）：声明留在 `competition_task_adapter.h` 不动，仅把定义迁到 `competition_task_registry.cpp`。对 `main_window.cpp` 和现有测试零 include 改动，最小惊扰。
- 方案 B：声明也迁到 registry 头，`main_window.cpp` 改 include。惊扰大，不采用。

采用方案 A。新增 `competition_task_registry.cpp` 内容：

```cpp
#include "framework/task/competition_task_adapter.h"
#include "h_problem/h_problem_adapter_registration.h"  // 题目模块暴露的 descriptor 提供者

#include <QStringList>

QString defaultCompetitionTaskAdapterId() { return QStringLiteral("h_problem"); }

QVector<CompetitionTaskAdapterDescriptor> availableCompetitionTaskAdapters() {
    QVector<CompetitionTaskAdapterDescriptor> descriptors;
    descriptors.append(hProblemTaskAdapterDescriptor());  // 未来 D 题在此 append
    return descriptors;
}

QString configuredCompetitionTaskAdapterId() {
    const QString configured_id = qEnvironmentVariable("NUEDC_TASK_ADAPTER").trimmed();
    return configured_id.isEmpty() ? defaultCompetitionTaskAdapterId() : configured_id;
}

std::unique_ptr<CompetitionTaskAdapter> createCompetitionTaskAdapter(
    const QString &adapter_id, QString *error_message) {
    const QString selected_id =
        adapter_id.trimmed().isEmpty() ? defaultCompetitionTaskAdapterId() : adapter_id.trimmed();
    QStringList available_ids;
    for (const auto &descriptor : availableCompetitionTaskAdapters()) {
        available_ids.append(descriptor.adapter_id);
        if (descriptor.adapter_id == selected_id) {
            if (error_message) error_message->clear();
            return descriptor.create();
        }
    }
    if (error_message) {
        *error_message = QStringLiteral("unknown task adapter '%1'; available adapters: %2")
                             .arg(selected_id, available_ids.join(QStringLiteral(", ")));
    }
    return nullptr;
}

std::unique_ptr<CompetitionTaskAdapter> createConfiguredCompetitionTaskAdapter(QString *error_message) {
    return createCompetitionTaskAdapter(configuredCompetitionTaskAdapterId(), error_message);
}
std::unique_ptr<CompetitionTaskAdapter> createDefaultCompetitionTaskAdapter() {
    return createCompetitionTaskAdapter(defaultCompetitionTaskAdapterId());
}
```

题目侧新增 `src/h_problem/h_problem_adapter_registration.h` / `.cpp`（或直接放进 `h_problem_page.cpp`，暴露一个 `hProblemTaskAdapterDescriptor()` 函数）：

```cpp
// h_problem_adapter_registration.h
#pragma once
#include "framework/task/competition_task_adapter.h"
CompetitionTaskAdapterDescriptor hProblemTaskAdapterDescriptor();
```

`h_problem_page.cpp` 中把原 `availableCompetitionTaskAdapters()` 定义（647-655 行）替换为：

```cpp
CompetitionTaskAdapterDescriptor hProblemTaskAdapterDescriptor() {
    return CompetitionTaskAdapterDescriptor{
        QStringLiteral("h_problem"),
        QStringLiteral("H 题野生动物巡检"),
        []() -> std::unique_ptr<CompetitionTaskAdapter> {
            return std::make_unique<HProblemTaskAdapter>();
        },
    };
}
```

并删除 `h_problem_page.cpp:657-688` 的四个工厂函数定义（迁到 registry.cpp）。

### 依赖方向校验

`competition_task_registry.cpp` include 了 `h_problem_adapter_registration.h` —— 这是框架 TU 反向依赖题目模块，与 `MainWindow 禁止 include h_problem` 的护栏是否冲突？不冲突，但要明确边界语义：

- 护栏真正约束的是 **Shell（MainWindow）和通信层** 不得依赖题目。注册表是「题目装配点」，本质上就该知道有哪些题目，类似 `main()` 里的依赖注入根。
- 为保持 framework 目录纯净，注册表的题目 include 需在架构测试中显式豁免（见测试计划），且注册表**只** include 题目的 registration 头（仅 descriptor 工厂），不得 include 题目 UI / rules / storage 头。

替代方案（更纯但更重）：自注册模式——题目 TU 用静态初始化把 descriptor 塞进全局 registry，framework 零题目 include。缺点：静态初始化顺序 / 被链接器裁剪（题目 .cpp 未被引用时 static 对象可能不链接进来）的坑，测试里要额外强制引用。**本期不采用**，保留显式装配点，语义更清楚、可测。

### 测试计划

- `tests/test_architecture_boundaries.cpp` 新增：`competition_task_registry.cpp` 存在且包含 `defaultCompetitionTaskAdapterId`；`h_problem_page.cpp` 不再定义 `createConfiguredCompetitionTaskAdapter`（防止回退）。
- 复用现有 `test_main_window`：验证 `createConfiguredCompetitionTaskAdapter` 仍返回 H 题 adapter，`NUEDC_TASK_ADAPTER=nonexistent` 时报错信息含可用列表。
- CMake：把 `competition_task_registry.cpp` 加入 `GROUND_STATION_APP_SOURCES`（与 `h_problem_page.cpp` 同组，因二者都进 app / test_main_window）。

---

## ③ 收敛 ack / 运行态

### 问题

`HProblemTaskAdapter` 用 5 个裸标量表达机载同步状态：

```
bool   mission_synced_to_airborne_
bool   mission_running_
QString acknowledged_task_id_
bool   acknowledged_mission_loaded_
quint64 last_accepted_sequence_
```

它们在 6 处手工读写，且组合约束散落：`applyMissionPlanResult`、`applyCommandAck`、`markAirborneSyncState`、`markControlCommandStarted`、`markControlCommandStopped`、`clearCommandAckState` / `enterNoFlySelectionMode`。典型 bug 面——「在 6 个地方记得清 3 个字段」。且 `missionRuntimeInputs()` 手工把这些标量拼进 `MissionRuntimeInputs`。

### 目标结构

抽出一个纯值类型 + 明确迁移方法，集中约束、可单测。放在框架层（题目无关，未来 D 题同样复用）：

新增文件：

- `src/framework/runtime/airborne_sync_state.h`
- `src/framework/runtime/airborne_sync_state.cpp`

```cpp
#pragma once
#include "framework/communication/envelope_codec.h"   // CommandSendResult
#include "framework/runtime/mission_runtime_state.h"   // MissionRuntimeInputs
#include <QString>
#include <QtGlobal>

// 机载同步 / 运行状态的单一事实源。所有迁移集中在此，消除多处手工同步。
class AirborneSyncState {
public:
    // 状态迁移（对应现有六处散落写入）
    void reset();                                   // = 旧 clearCommandAckState + 同步/运行清零
    void applyCommandAck(const CommandSendResult &result);
    void applyAirborneSync(bool online, bool synced);
    void markControlStarted();
    void markControlStopped();

    // 只读查询
    bool syncedToAirborne() const { return synced_to_airborne_; }
    bool running() const { return running_; }
    bool hasAck() const;                            // task_id 非空 或 seq>0

    // 把自身投射进通用运行态输入（题目侧补 command_sync_enabled / airborne_online / active_task_id）
    void fillRuntimeInputs(MissionRuntimeInputs &inputs) const;

private:
    bool    synced_to_airborne_ = false;
    bool    running_ = false;
    QString acknowledged_task_id_;
    bool    acknowledged_mission_loaded_ = false;
    quint64 last_accepted_sequence_ = 0;
};
```

各迁移方法的语义 **1:1 照搬现有逻辑**（不改行为，只搬家）：

- `reset()` ← `clearCommandAckState()` 三行 + 现有 `mission_synced_to_airborne_=false; mission_running_=false;`（`enterNoFlySelectionMode` 里的组合）。
- `applyCommandAck()` ← `applyCommandAck` 现有实现（含 `result.ok` / 空 ack 提前返回的守卫）。
- `applyAirborneSync()` ← `markAirborneSyncState`。
- `markControlStarted/Stopped()` ← 同名方法体。
- `fillRuntimeInputs()` ← `missionRuntimeInputs()` 里拼装的那部分。

`HProblemTaskAdapter` 改为持有 `AirborneSyncState sync_state_;`，删除 5 个标量。各处调用改为委托，并在迁移后调用 `refreshMissionContextLabels()` + `emitRuntimeChanged()`（UI 副作用留在 adapter，值类型保持纯粹、无 UI 依赖，才可单测）。

### 关键点

- `AirborneSyncState` **不碰 UI、不发信号**，纯状态；UI 刷新由 adapter 在调用后触发。这样值类型可脱 Qt Widgets 单测。
- `applyCommandAck` 的提前返回守卫（`!result.ok || (task_id 空 && seq==0)`）必须原样保留，否则会回归成「无效 ack 覆盖有效状态」。

### 测试计划

新增 `tests/test_airborne_sync_state.cpp`：

- `reset` 后所有字段归零、`hasAck()==false`。
- `applyCommandAck` 有效 ack 后 `syncedToAirborne` / `running` / seq 正确；无效 ack（`ok=false` 或空 task+seq0）不改变既有状态。
- `applyAirborneSync(false, *)` 触发 reset 语义；`applyAirborneSync(true, false)` 清 running / loaded。
- `markControlStarted/Stopped` 翻转 running。
- `fillRuntimeInputs` 产出与旧 `missionRuntimeInputs()` 等价的字段。

回归：`test_mission_runtime_state`、`test_main_window`、`test_mission_command_service` 应保持通过。

---

## ② 拆 Adapter 上帝对象

### 问题

`HProblemTaskAdapter`（`h_problem_page.h` + `h_problem_page.cpp` 691 行）一身六职：

1. 构建 UI（`createTaskView` ~90 行 widget 装配）。
2. 通用协议事件 → UI（`handleTaskPlan/Event/Summary` + `applyGridConfig/Telemetry/Detection/Summary`）。
3. 规划工作流（`enterNoFlySelectionMode` / `generateMissionPlanFromCandidateSelection` / `applyMissionPlanResult` / 候选禁飞格选择）。
4. 命令 / ack 状态（③ 处理后收敛为 `AirborneSyncState`）。
5. 持久化接线（`MissionPlanStore`、`DetectionRepository`）。
6. 路径解析（④ 处理）。

### 目标结构

在 ① ③ ④ 落地后，② 的核心是把「UI 视图」与「工作流控制器」分层。`HProblemTaskAdapter` 退化成薄适配层：实现 `CompetitionTaskAdapter` 接口 + 持有 controller 与 view，转发调用。

拆分为三层（均在 `h_problem/`，题目内部拆分，不动框架契约）：

- `h_problem/ui/h_problem_view.{h,cpp}` —— 纯被动视图。持有 `grid_scene_ / case_label_ / mission_label_ / detection_list_ / summary_table_`，暴露 `buildWidget(parent)` 与一组 setter（`showRoute`、`showDetection`、`showSummaryTotals`、`setStatusText`、`setCandidateCells` …）。无业务逻辑、无 store、无网络。
- `h_problem/mission/h_mission_controller.{h,cpp}` —— 工作流 / 状态持有者。含 `PlanningStateMachine`、`MissionPlanBridge`、`MissionCommandService`、`AirborneSyncState`、`DetectionRepository`、当前案例/禁飞格等业务字段，以及规划 / 事件解码 / 持久化逻辑。通过回调或轻接口驱动 view，不直接 new widget。
- `h_problem/ui/h_problem_page.{h,cpp}`（`HProblemTaskAdapter`）—— 只实现接口、装配 controller + view、转发 `CompetitionTaskAdapter` 的 20 个虚函数到 controller。目标 < 120 行。

数据流：

```
Shell(MainWindow)
  → HProblemTaskAdapter (接口转发)
      → HMissionController (工作流/状态/解码/持久化)
          → HProblemView (被动 UI setter)
```

controller → view 用一个窄回调接口（避免 view 反依赖 controller）：

```cpp
// h_mission_controller.h
class HMissionViewSink {  // view 实现它；controller 只依赖此接口
public:
    virtual ~HMissionViewSink() = default;
    virtual void showRoute(const MissionPlanData &plan) = 0;
    virtual void showDetection(const QString &cell, const QString &animal, int count, qint64 ts) = 0;
    virtual void showSummaryTotals(const QMap<QString,int> &totals) = 0;
    virtual void setStatusText(const QString &text) = 0;
    virtual void setCandidateCells(const QStringList &cells) = 0;
    // …其余 grid_scene 操作聚合成语义方法
};
```

`HProblemTaskAdapter` 原有的 `notifyStatusTextChanged` / `notifyPlanningButtonTextChanged` / `notifyRuntimeChanged`（来自基类回调）保持由 adapter 持有，controller 通过构造注入的 `std::function` 触发，语义不变。

### 迁移步骤（增量、每步可编译可测）

1. 先抽 `HProblemView`：把 `createTaskView` 的 widget 装配和所有 `grid_scene_->` / `summary_table_->` / `detection_list_->` 直接操作搬进 view，adapter 持有 `HProblemView`，暂时仍在 adapter 里调 view 的 setter。跑全测试。
2. 再抽 `HMissionController`：把规划工作流 + 事件解码 + 持久化 + `AirborneSyncState` 搬进 controller，adapter 转发。controller 通过 `HMissionViewSink`（由 view 实现）驱动 UI。跑全测试。
3. adapter 收尾到 < 120 行，仅装配 + 转发。

每步之间保持 `CompetitionTaskAdapter` 对外行为不变，`test_main_window` 全程作为回归网。

### 风险

② 改动面最大，且本机无法编译验证时盲改风险最高——**必须**在 ①③④ 完成、工具链就绪、`ctest` 全绿之后再做，且严格按上面三小步增量提交，每步编译 + ctest。若任一小步无法在本机验证，暂停并上报，不连续盲改（对齐通用准则：同一思路失败两次即停下换法）。

### 测试计划

- 新增 `tests/test_h_problem_view.cpp`：view setter 在 offscreen 平台下正确更新 widget（复用现有 `QT_QPA_PLATFORM=offscreen`）。
- 新增 `tests/test_h_mission_controller.cpp`：给定 case + 候选禁飞格，controller 产出正确 `MissionPlanData` 并调用 sink 的对应方法（用 mock sink 断言）。
- 强化 `test_main_window`：端到端仍能加载初始预览、生成航线、消费 task event/summary。
- `test_architecture_boundaries.cpp` 新增：`h_problem_page.cpp` 行数 / 职责收敛的弱断言（例如不再直接 `new QTableWidget`，widget 装配只在 view TU）。

---

## ④ 路径解析提到框架

### 问题

`h_problem_page.cpp:26-43` 的 `findRepositoryRootImpl()` / `repositoryRootPath()`，以及 `resolveCaseFilePath()`（608-618）和三处硬编码相对路径（`shared/cases/sample_case.json`、`runtime/active_mission_plan.json`、`runtime/ground_control_results.db`）嵌死在题目 UI。每个新题目都要重造仓库根探测。

### 目标结构

新增框架路径助手（题目无关）：

- `src/framework/config/repository_paths.h`
- `src/framework/config/repository_paths.cpp`

```cpp
#pragma once
#include <QString>

// 从可执行文件目录向上探测仓库根（含 shared/cases 与 ground_station_computer/src 的目录）。
// 结果缓存；探测失败回退到当前工作目录。题目模块统一用它，不再各自实现。
class RepositoryPaths {
public:
    static const QString &root();
    static QString resolve(const QString &relative);       // root/relative
    static QString sharedCase(const QString &case_id);     // shared/cases/<id>.json，缺文件返回空
};
```

实现即把 `findRepositoryRootImpl` / `repositoryRootPath` 原样搬入（探测标志文件逻辑不变），`resolveCaseFilePath` 改为薄封装 `RepositoryPaths::sharedCase` + adapter 自身默认值回退。

题目侧：`h_problem_page.cpp` 删掉匿名命名空间里的根探测，改用 `RepositoryPaths`。硬编码相对路径保留在题目（属题目约定），但统一经 `RepositoryPaths::resolve()` 转绝对路径。

CMake：`repository_paths.cpp` 加入 `GROUND_STATION_CORE_SOURCES`（框架核心，多个 test / app 都可能用）。

### 依赖方向校验

`RepositoryPaths` 在 framework/config 下，不 include 任何题目头，方向合规。它探测的标志文件（`shared/cases/sample_case.json`）是仓库结构约定，非题目耦合——但为降低对具体文件名的依赖，探测条件改用更稳的 `ground_station_computer/src` + `shared/cases` 目录存在性（去掉对 `sample_case.json` 具体文件的依赖）。

### 测试计划

- 新增 `tests/test_repository_paths.cpp`：从构建目录能定位到仓库根；`resolve` 拼接正确；`sharedCase("wildlife-demo")` 命中 `shared/cases/wildlife-demo.json`（若存在）或空。
- 回归 `test_main_window` / `test_mission_plan_bridge`：初始预览路径解析不变。

---

## 汇总：新增 / 改动文件

| 项 | 新增 | 改动 |
|----|------|------|
| ① | `framework/task/competition_task_registry.{h,cpp}`、`h_problem/h_problem_adapter_registration.h` | `h_problem_page.cpp`（删工厂定义，留 descriptor 提供者）、`CMakeLists.txt`、`test_architecture_boundaries.cpp` |
| ③ | `framework/runtime/airborne_sync_state.{h,cpp}`、`tests/test_airborne_sync_state.cpp` | `h_problem_page.{h,cpp}`（删 5 标量、委托）、`CMakeLists.txt` |
| ② | `h_problem/ui/h_problem_view.{h,cpp}`、`h_problem/mission/h_mission_controller.{h,cpp}`、`tests/test_h_problem_view.cpp`、`tests/test_h_mission_controller.cpp` | `h_problem_page.{h,cpp}`（瘦身）、`CMakeLists.txt`、`test_architecture_boundaries.cpp` |
| ④ | `framework/config/repository_paths.{h,cpp}`、`tests/test_repository_paths.cpp` | `h_problem_page.cpp`（删根探测）、`CMakeLists.txt` |

## 提交前门禁（每项独立跑）

```bash
cmake --build build
ctest --test-dir build --output-on-failure
rg -n "h_problem_core|h_problem/" shared/cpp/include/competition_core shared/cpp/src/task shared/cpp/src/protocol
rg -n "h_problem/" ground_station_computer/src/app ground_station_computer/src/framework/communication
rg -n "NUEDC_TASK_ADAPTER|availableCompetitionTaskAdapters|createConfiguredCompetitionTaskAdapter" ground_station_computer/src
```

注意：① 之后 `framework/task/competition_task_registry.cpp` 会合法 include 题目 registration 头，需在架构测试中对该 TU 显式豁免（豁免范围仅限 registration 头，不含题目 UI / rules / storage）。

## 编译验证的前置：本机工具链

本机当前仅有 STM32CubeCLT 附带的 cmake 3.28，缺 Qt6 / protobuf(protoc) / cppzmq / libzmq，且无 `build/` 目录。四项落地后要真编译验证，需先装齐并让 CMake 能 find_package。计划（评审通过后单列执行、逐步确认）：

1. 探明顶层 `CMakeLists.txt` 对 Qt6 / Protobuf / ZMQ 的 `find_package` 要求与最低版本。
2. 装 Qt6（含 Widgets / Sql / Test）、protobuf + protoc、libzmq + cppzmq；Windows 下优先 vcpkg 或 aqt + 官方包，路径经 `CMAKE_PREFIX_PATH` / 环境变量注入。
3. `cmake -S . -B build` 配置，`cmake --build build`，`ctest --test-dir build --output-on-failure` 全绿作为基线。
4. 基线绿后再按 ①→③→②→④ 落地，每项后重跑构建 + ctest。

工具链安装涉及联网下载与环境改动，属需确认的动作，落地时逐步征询。
