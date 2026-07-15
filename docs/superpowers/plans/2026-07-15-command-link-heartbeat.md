# 地面站命令链路真实心跳实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让地面站通过持续命令 PING 显示真实链路状态，静置时保持在线，连续三次未确认后才安全地显示离线。

**Architecture:** 新增可单测的健康状态机、线程安全的串行命令传输层和后台 `CommandLinkMonitor`。`MainWindow` 消费监视器状态而不再使用单次 ACK 的 5 秒 TTL；任务命令与后台 PING 通过同一传输层串行发送，协议和机载端不变。

**Tech Stack:** C++17、Qt 6 Core/Widgets/Test、ZeroMQ cppzmq、CMake/CTest。

## Global Constraints

- 心跳周期固定为 `2000 ms`，连续失败阈值固定为 `3`。
- 自动 PING 必须在后台线程执行；UI 线程不得等待自动网络请求。
- `Online` 仅由有效命令 ACK 产生；telemetry 和 TCP 端口探测不得提升命令链路状态。
- 一次或两次失败为 `Checking`；第三次连续失败为 `Offline`；任一次有效 ACK 立即恢复 `Online`。
- `Checking` 与 `Offline` 均禁用执行、停止和视觉控制；任务和遥测显示继续工作。
- 所有 ZeroMQ 请求必须经过一个进程内串行传输层，防止后台 PING 与用户命令并发占用机载 REP。
- 保持现有 `1500 ms` ZMQ 单次收发超时、最多 `3` 次尝试和 `120 ms` 重试间隔。
- 不修改 Protobuf、5557/5558 端口、机载 ROS 2 包、热点配置或飞控执行器语义。
- 所有新增行为必须先写 Qt Test 并确认它在实现前失败；不新增 Qt Concurrent 或第三方依赖。

---

### Task 1: 提取命令链路健康状态机

**Files:**
- Create: `ground_station_computer/src/framework/communication/command_link_health.h`
- Create: `ground_station_computer/src/framework/communication/command_link_health.cpp`
- Create: `ground_station_computer/tests/test_command_link_health.cpp`
- Modify: `ground_station_computer/CMakeLists.txt`

**Interfaces:**
- Produces `enum class CommandLinkHealth { Checking, Online, Offline };`.
- Produces `struct CommandLinkSnapshot { CommandLinkHealth health; int consecutive_failures; QString detail; };`.
- Declares `Q_DECLARE_METATYPE(CommandLinkSnapshot)` and requires registration before `healthChanged` crosses threads.
- Produces `CommandLinkHealthTracker(int failure_threshold = 3)`, `recordSuccess(const QString &)`, `recordFailure(const QString &)`, and `snapshot() const`.
- Later tasks consume `CommandLinkSnapshot` and preserve the exact third-failure transition.

- [ ] **Step 1: Write the failing state-transition tests.**

```cpp
void CommandLinkHealthTests::startsChecking() {
    CommandLinkHealthTracker tracker;
    const CommandLinkSnapshot snapshot = tracker.snapshot();
    QCOMPARE(snapshot.health, CommandLinkHealth::Checking);
    QCOMPARE(snapshot.consecutive_failures, 0);
}

void CommandLinkHealthTests::marksOfflineOnlyAfterThirdConsecutiveFailure() {
    CommandLinkHealthTracker tracker;
    QCOMPARE(tracker.recordFailure("timeout").health, CommandLinkHealth::Checking);
    QCOMPARE(tracker.recordFailure("timeout").health, CommandLinkHealth::Checking);
    const CommandLinkSnapshot third = tracker.recordFailure("timeout");
    QCOMPARE(third.health, CommandLinkHealth::Offline);
    QCOMPARE(third.consecutive_failures, 3);
    QCOMPARE(third.detail, QString("timeout"));
}

void CommandLinkHealthTests::successImmediatelyRestoresOnlineAndClearsFailures() {
    CommandLinkHealthTracker tracker;
    tracker.recordFailure("first timeout");
    tracker.recordFailure("second timeout");
    const CommandLinkSnapshot snapshot = tracker.recordSuccess("pong");
    QCOMPARE(snapshot.health, CommandLinkHealth::Online);
    QCOMPARE(snapshot.consecutive_failures, 0);
    QCOMPARE(snapshot.detail, QString("pong"));
}
```

- [ ] **Step 2: Add the test target and verify RED.**

Add to `ground_station_computer/CMakeLists.txt`:

```cmake
add_ground_station_test(test_command_link_health tests/test_command_link_health.cpp)
```

Run:

```bash
cmake --build build --target test_command_link_health
```

Expected: compilation fails because `command_link_health.h` and `CommandLinkHealthTracker` do not exist.

- [ ] **Step 3: Implement the minimal state machine.**

```cpp
enum class CommandLinkHealth { Checking, Online, Offline };

struct CommandLinkSnapshot {
    CommandLinkHealth health = CommandLinkHealth::Checking;
    int consecutive_failures = 0;
    QString detail;
};

class CommandLinkHealthTracker {
public:
    explicit CommandLinkHealthTracker(int failure_threshold = 3);
    CommandLinkSnapshot recordSuccess(const QString &detail);
    CommandLinkSnapshot recordFailure(const QString &detail);
    CommandLinkSnapshot snapshot() const;

private:
    int failure_threshold_ = 3;
    CommandLinkSnapshot snapshot_;
};
```

`recordSuccess` sets `Online` and clears failures. `recordFailure` increments the count and uses `Offline` only at the threshold; otherwise it uses `Checking`. A constructor threshold below `1` falls back to `3`.
Place `Q_DECLARE_METATYPE(CommandLinkSnapshot)` after the struct declaration.

- [ ] **Step 4: Add the production source and verify GREEN.**

Add this source to `GROUND_STATION_CORE_SOURCES`:

```cmake
src/framework/communication/command_link_health.cpp
```

Run:

```bash
cmake --build build --target test_command_link_health
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^test_command_link_health$' --output-on-failure
```

Expected: `1/1` test passes.

- [ ] **Step 5: Commit Task 1.**

```bash
git add ground_station_computer/CMakeLists.txt \
  ground_station_computer/src/framework/communication/command_link_health.h \
  ground_station_computer/src/framework/communication/command_link_health.cpp \
  ground_station_computer/tests/test_command_link_health.cpp
git commit -m "feat: add command link health state"
```

### Task 2: Implement serialized transport and background heartbeat monitoring

**Files:**
- Create: `ground_station_computer/src/framework/communication/serialized_command_transport.h`
- Create: `ground_station_computer/src/framework/communication/serialized_command_transport.cpp`
- Create: `ground_station_computer/src/framework/communication/command_link_monitor.h`
- Create: `ground_station_computer/src/framework/communication/command_link_monitor.cpp`
- Create: `ground_station_computer/tests/test_command_link_monitor.cpp`
- Modify: `ground_station_computer/CMakeLists.txt`

**Interfaces:**
- Consumes `CommandTransport`, `ReliableCommandClient`, `ZmqCommandClient`, `CommandLinkHealthTracker`, and `CommandLinkSnapshot`.
- Produces `SerializedCommandTransport`, owning a `std::unique_ptr<CommandTransport>` and locking inner `sendEnvelope` with a `std::mutex`.
- Produces `CommandLinkMonitor : public QThread` with `setActiveTaskId(QString)`, `requestImmediateProbe()`, `recordExternalCommandResult(const CommandSendResult &)`, `startMonitoring()`, `stopMonitoring()`, and `healthChanged(CommandLinkSnapshot)`.
- Later, the window and adapter commands share one `SerializedCommandTransport`; the monitor owns its own `ReliableCommandClient` only inside `run()`.

- [ ] **Step 1: Write failing monitor and serialization tests.**

Define a thread-safe `QueueTransport` test double that returns queued `CommandSendResult` values and atomically tracks concurrent `sendEnvelope` calls. Add these test slots:

```cpp
void CommandLinkMonitorTests::doesNotReportOfflineForFirstTwoHeartbeatFailures();
void CommandLinkMonitorTests::reportsOfflineAfterThirdHeartbeatFailure();
void CommandLinkMonitorTests::successfulHeartbeatRecoversFromOffline();
void CommandLinkMonitorTests::externalSuccessfulCommandResetsFailureCount();
void CommandLinkMonitorTests::neverOverlapsHeartbeatAndExternalTransportSend();
void CommandLinkMonitorTests::stopWaitsForInFlightProbeBeforeDestroyingThread();
```

For three failed heartbeats, assert the first two emitted snapshots are `Checking` and the third is `Offline`. For recovery, enqueue `CommandSendResult{true, "pong"}` after the three failures and assert `Online` with zero failures. The overlap test must assert maximum physical sends equals `1`.
The stop test uses a `BlockingTransport` that signals when `sendEnvelope` has begun and waits on a test-controlled release. Request monitor shutdown while it is blocked, release the transport, and assert `stopMonitoring()` returns only after `isRunning()` is false. It must exercise the grace-timeout fallback without destroying a running thread.

- [ ] **Step 2: Add the test target and verify RED.**

```cmake
add_ground_station_test(test_command_link_monitor tests/test_command_link_monitor.cpp)
```

Run:

```bash
cmake --build build --target test_command_link_monitor
```

Expected: compilation fails because `SerializedCommandTransport` or `CommandLinkMonitor` does not exist.

- [ ] **Step 3: Implement the serialized transport.**

```cpp
class SerializedCommandTransport final : public CommandTransport {
public:
    explicit SerializedCommandTransport(std::unique_ptr<CommandTransport> inner);
    CommandSendResult sendEnvelope(const Envelope &envelope) const override;

private:
    std::unique_ptr<CommandTransport> inner_;
    mutable std::mutex mutex_;
};

CommandSendResult SerializedCommandTransport::sendEnvelope(const Envelope &envelope) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return inner_ == nullptr
        ? CommandSendResult{false, "command transport is not configured"}
        : inner_->sendEnvelope(envelope);
}
```

The lock must cover the whole physical exchange. Do not change `ReliableCommandClient` retries.

- [ ] **Step 4: Implement the monitor thread.**

```cpp
class CommandLinkMonitor final : public QThread {
    Q_OBJECT
public:
    static constexpr int kHeartbeatIntervalMs = 2000;
    explicit CommandLinkMonitor(std::shared_ptr<const CommandTransport> transport,
        int heartbeat_interval_ms = kHeartbeatIntervalMs, QObject *parent = nullptr);
    ~CommandLinkMonitor() override;
    void setActiveTaskId(QString task_id);
    void requestImmediateProbe();
    void recordExternalCommandResult(const CommandSendResult &result);
    void startMonitoring();
    void stopMonitoring();
signals:
    void healthChanged(CommandLinkSnapshot snapshot);
protected:
    void run() override;
};
```

Use `QMutex` plus `QWaitCondition` to guard task ID, immediate-probe flag, and tracker. Each loop runs one `reliable_client.ping(task_id)` after a two-second wait or immediate request; success records success, failure records failure, then emits the snapshot. Do not begin another heartbeat while one is running. `stopMonitoring()` must request interruption and wake the wait condition. It may make one bounded grace `wait()` for diagnostics, but when that wait expires it must log the condition and then call unconditional `wait()` before any destructor can release the thread object; a live `QThread` must never be destroyed.
Call `qRegisterMetaType<CommandLinkSnapshot>("CommandLinkSnapshot")` before the monitor starts so the signal is valid for queued delivery to `MainWindow`.

- [ ] **Step 5: Add production sources and verify GREEN.**

Add:

```cmake
src/framework/communication/serialized_command_transport.cpp
src/framework/communication/command_link_monitor.cpp
```

Run:

```bash
cmake --build build --target test_command_link_health test_command_link_monitor
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(test_command_link_health|test_command_link_monitor)$' --output-on-failure
```

Expected: `2/2` tests pass.

- [ ] **Step 6: Commit Task 2.**

```bash
git add ground_station_computer/CMakeLists.txt \
  ground_station_computer/src/framework/communication/serialized_command_transport.h \
  ground_station_computer/src/framework/communication/serialized_command_transport.cpp \
  ground_station_computer/src/framework/communication/command_link_monitor.h \
  ground_station_computer/src/framework/communication/command_link_monitor.cpp \
  ground_station_computer/tests/test_command_link_monitor.cpp
git commit -m "feat: monitor command link heartbeats"
```

### Task 3: Integrate command health into the window and task adapter

**Files:**
- Modify: `ground_station_computer/src/app/main_window.h`
- Modify: `ground_station_computer/src/app/main_window.cpp`
- Modify: `ground_station_computer/src/framework/task/competition_task_adapter.h`
- Modify: `ground_station_computer/src/h_problem/ui/h_problem_page.h`
- Modify: `ground_station_computer/src/h_problem/ui/h_problem_page.cpp`
- Modify: `ground_station_computer/tests/test_main_window.cpp`

**Interfaces:**
- Consumes `CommandLinkMonitor`, `CommandLinkSnapshot`, `CommandLinkHealth`, and `SerializedCommandTransport`.
- Adds `virtual void setCommandTransport(const CommandTransport *transport) = 0;` to `CompetitionTaskAdapter`, forwarding through `HProblemTaskAdapter` to `HMissionController::setCommandTransport`.
- Replaces `MainWindow::recordCommandLinkResult(bool)` and command-TTL state with `handleCommandLinkHealthChanged(CommandLinkSnapshot)` plus one current snapshot.
- Changes the Adapter command-link callback from a boolean to `const CommandSendResult &`, preserving the result detail for monitor failure accounting.

- [ ] **Step 1: Write failing window behavior tests.**

Replace legacy TTL tests with:

```cpp
void MainWindowTests::checkingCommandHealthDisablesControlsWithoutShowingOffline();
void MainWindowTests::thirdCommandHealthFailureShowsOffline();
void MainWindowTests::healthyHeartbeatRemainsOnlinePastLegacyFiveSecondTtl();
void MainWindowTests::probeButtonRequestsImmediateHeartbeat();
```

The central assertions must be:

```cpp
window.handleCommandLinkHealthChanged(
    CommandLinkSnapshot{CommandLinkHealth::Checking, 1, "command ack timed out"});
QVERIFY(!execute_button->isEnabled());
QVERIFY(window.airborne_status_label_->text().contains("链路确认中（1/3）"));
QVERIFY(!window.airborne_status_label_->text().contains("机载状态: 离线"));

window.handleCommandLinkHealthChanged(
    CommandLinkSnapshot{CommandLinkHealth::Offline, 3, "command ack timed out"});
QVERIFY(window.airborne_status_label_->text().startsWith("机载状态: 离线"));
```

The healthy test waits `5500 ms`, applies a later success snapshot, and asserts online without relying on a command TTL. The existing test friendship permits direct private-slot calls.

- [ ] **Step 2: Verify RED.**

```bash
cmake --build build --target test_main_window
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^test_main_window$' --output-on-failure
```

Expected: compilation or assertion failure because the window still uses the old five-second TTL.

- [ ] **Step 3: Wire one shared serialized transport into all command users.**

Build the layers in `MainWindow` in this order:

```cpp
auto raw_transport = std::make_unique<ZmqCommandTransport>(command_client_);
command_transport_ = std::make_shared<SerializedCommandTransport>(std::move(raw_transport));
mission_command_service_ = std::make_unique<MissionCommandService>(command_transport_.get());
task_adapter_->setCommandTransport(command_transport_.get());
command_link_monitor_ = std::make_unique<CommandLinkMonitor>(command_transport_);
connect(command_link_monitor_.get(), &CommandLinkMonitor::healthChanged,
        this, &MainWindow::handleCommandLinkHealthChanged);
command_link_monitor_->setActiveTaskId(task_adapter_->activeTaskId());
command_link_monitor_->startMonitoring();
```

Start monitoring only when `command_sync_enabled_` is true. Update the monitor task ID from the adapter runtime callback and after task ACK processing. Every existing command result calls `command_link_monitor_->recordExternalCommandResult(result)` after normal ACK handling. Do not call `markAirborneSyncState(false, ...)` merely because one command fails.
For adapter-originated mission-load and lifecycle visual-reset results, change the callback plumbing to carry the full `CommandSendResult`; invoke it only after existing `applyCommandAck`, reset, or local failure-state handling has completed.
`disarmVisionTargetingForLifecycle()` must return its result without notifying the command-link callback. Each caller must notify exactly once after its complete task replacement, ACK clear, running-state, or summary transition is complete; add a task-replacement regression proving the callback cannot observe the old task's ACK state.

- [ ] **Step 4: Remove TTL logic and implement the three UI states.**

Delete `kCommandLinkTtlMs`, `command_link_online_`, `last_successful_command_reply_ms_`, `command_health_expiry_timer_`, and `recordCommandLinkResult(bool)`. Replace them with:

```cpp
bool MainWindow::commandLinkHealthy() const {
    return command_link_snapshot_.health == CommandLinkHealth::Online;
}

void MainWindow::handleCommandLinkHealthChanged(CommandLinkSnapshot snapshot) {
    command_link_snapshot_ = std::move(snapshot);
    refreshAirborneStatusLabel();
    refreshExecutionControls();
}
```

`refreshAirborneStatusLabel()` uses exact `机载状态: 链路确认中（%1/3）` for `Checking`, `机载状态: 离线` for `Offline`, and existing detailed `MissionRuntimeState::airborneStatusText` for `Online`. Append current telemetry substatus in every case. The refresh button calls `requestImmediateProbe()` only. The destructor calls `stopMonitoring()` before destroying the shared transport and subscriber worker.

- [ ] **Step 5: Verify GREEN for window and nearby command behavior.**

```bash
cmake --build build --target test_main_window test_mission_command_service test_h_mission_controller test_architecture_boundaries
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(test_main_window|test_mission_command_service|test_h_mission_controller|test_architecture_boundaries)$' --output-on-failure
```

Expected: `4/4` tests pass and none expects command health to expire merely because five seconds elapsed.
The focused window tests must also execute one failed user command through `FixedCommandTransport` and assert that the emitted monitor snapshot is `Checking` with the command error detail while the Adapter's existing task-sync state is not rewritten to offline.

- [ ] **Step 6: Commit Task 3.**

```bash
git add ground_station_computer/src/app/main_window.h ground_station_computer/src/app/main_window.cpp ground_station_computer/src/framework/task/competition_task_adapter.h ground_station_computer/src/h_problem/ui/h_problem_page.h ground_station_computer/src/h_problem/ui/h_problem_page.cpp ground_station_computer/tests/test_main_window.cpp
git commit -m "fix: keep command link health current"
```

### Task 4: Document and verify the persistent dual-machine behavior

**Files:**
- Modify: `docs/dual_nuc_setup_guide.md`
- Modify: `README.md`
- Test: `ground_station_computer/tests/test_command_link_health.cpp`
- Test: `ground_station_computer/tests/test_command_link_monitor.cpp`
- Test: `ground_station_computer/tests/test_main_window.cpp`

**Interfaces:**
- Consumes Task 1-3 UI semantics and fixed heartbeat parameters.
- Produces operational acceptance instructions with no new runtime configuration.

- [ ] **Step 1: Write the documentation RED check.**

```bash
rg -n "静置 30 秒|链路确认中|第三次连续失败|自动恢复在线" docs/dual_nuc_setup_guide.md README.md
```

Expected: no match before documentation changes.

- [ ] **Step 2: Update the manuals.**

Add these exact rules to the dual-NUC guide: a PING runs every two seconds; first or second missing confirmation is `链路确认中`; third consecutive failure is offline; a later ACK recovers online without restart; the refresh button requests one immediate probe and is not needed to keep online. In README's ACK contract, state that only command ACK and background PING establish command health, never telemetry.

- [ ] **Step 3: Build and run the full automated suite.**

```bash
cmake -S . -B build
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

Expected: configuration and build exit `0`; all CTest cases pass.

- [ ] **Step 4: Run live two-machine acceptance.**

Ground station computer:

```bash
cd /home/sb/Ground_station/NUEDC_Test
source runtime/ground_control_network.env
./scripts/check_ground_control_network.sh --host 10.42.0.2
./build/ground_station_computer/ground_station_app
```

Airborne computer remains running:

```bash
cd /path/to/point_lio_mid360_ros2
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch airborne_bringup sim_airborne.launch.py
```

Acceptance sequence: leave the connected system idle for at least 30 seconds and observe continuous online state; generate and synchronize a task; execute, stop, arm, and reset targeting with ACKs; stop `ground_link` and wait until the third heartbeat shows offline; restart it and verify automatic online recovery without a restart or refresh click.

- [ ] **Step 5: Commit Task 4.**

```bash
git add docs/dual_nuc_setup_guide.md README.md
git commit -m "docs: describe command link heartbeats"
```

## Original Plan Self-Review

- Coverage: Task 1 provides the three-state threshold semantics; Task 2 provides background and serialized sends; Task 3 changes UI and control safety; Task 4 covers documentation, build, tests, and live two-machine acceptance.
- Consistency: every task uses 2000 ms, three failures, 1500 ms single ZMQ timeout, and existing three retries; none treats telemetry as command health.
- Scope: no task modifies airborne packages, networking, Protobuf, or flight-control behavior.
- Placeholder scan: no unresolved implementation marker or undefined interface remains.

## Original Execution Handoff

Use `subagent-driven-development` in this session to execute Tasks 1-4 in order. After every task, produce a diff package and dispatch a new reviewer for spec compliance and code quality before moving to the next task.

### Task 5: Prevent stale heartbeat results from overwriting newer command ACKs

**Files:**
- Modify: `ground_station_computer/src/framework/communication/command_link_monitor.h`
- Modify: `ground_station_computer/src/framework/communication/command_link_monitor.cpp`
- Modify: `ground_station_computer/tests/test_command_link_monitor.cpp`

**Interfaces:**
- Consumes the Task 2 monitor/tracker APIs and preserves all existing heartbeat interval, retry, serialization, and shutdown behavior.
- Adds an internal monotonic health-result generation guarded by the existing monitor mutex; it is not exposed to UI or protocol code.

- [ ] **Step 1: Write a deterministic failing stale-result regression.**

Add `staleHeartbeatFailureCannotOverwriteNewerExternalSuccess()` using a blocking transport. Start an immediate heartbeat and wait until its first physical send is blocked. Call:

```cpp
monitor.recordExternalCommandResult(CommandSendResult{true, "mission started"});
```

Assert the emitted snapshot is `Online`. Release the blocked heartbeat so its reliable operation returns failures. Under the old implementation, the later heartbeat failure emits `Checking`; the desired assertion is that no later stale snapshot replaces `Online`.

- [ ] **Step 2: Verify RED.**

```bash
cmake --build build --target test_command_link_monitor
QT_QPA_PLATFORM=offscreen ./build/ground_station_computer/test_command_link_monitor -v1
```

Expected: the new regression fails because a heartbeat begun before the external success emits a later `Checking` snapshot.

- [ ] **Step 3: Implement generation-based stale-result suppression.**

```cpp
// Under the existing mutex.
quint64 health_generation_ = 0;

// recordExternalCommandResult:
++health_generation_;
const CommandLinkSnapshot snapshot = result.ok
    ? tracker_.recordSuccess(result.message)
    : tracker_.recordFailure(result.message);

// run(), before ping:
const quint64 probe_generation = health_generation_;

// run(), after ping, under the mutex:
if (probe_generation != health_generation_) {
    continue;  // A newer external command result is authoritative.
}
const CommandLinkSnapshot snapshot = result.ok
    ? tracker_.recordSuccess(result.message)
    : tracker_.recordFailure(result.message);
```

Do not increment the generation for heartbeat results. Do not emit a signal for a discarded stale heartbeat. Keep `SerializedCommandTransport` and `ReliableCommandClient` unchanged.

- [ ] **Step 4: Verify GREEN.**

```bash
cmake --build build --target test_command_link_monitor test_main_window
QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^(test_command_link_monitor|test_main_window)$' --output-on-failure
```

Expected: both tests pass; the stale heartbeat test observes no degradation after the newer external success.

- [ ] **Step 5: Commit Task 5.**

```bash
git add ground_station_computer/src/framework/communication/command_link_monitor.h ground_station_computer/src/framework/communication/command_link_monitor.cpp ground_station_computer/tests/test_command_link_monitor.cpp
git commit -m "fix: ignore stale heartbeat results"
```

## Amendment Self-Review

- Coverage: Task 5 closes the only remaining command-health ordering race by proving and suppressing an old heartbeat result that returns after a newer external ACK.
- Consistency: the amendment preserves the 2000 ms cadence, three-failure threshold, existing reliable retry policy, serialization, and no-telemetry health rule.
- Scope: it changes only monitor-internal state ordering and its focused test; no UI, Adapter, protocol, airborne, or network behavior is broadened.

## Amendment Execution Handoff

Execute Task 5 using the same subagent-driven implementation and independent review gates as Tasks 1-4. Run live dual-machine acceptance only after Task 5 is reviewed clean.
