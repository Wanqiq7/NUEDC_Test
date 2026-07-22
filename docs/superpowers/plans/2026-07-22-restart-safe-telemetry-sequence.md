# 遥测序号重启安全实施计划

> **面向执行代理：** 必须使用子技能 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans，逐项实施本计划。步骤使用复选框（`- [ ]`）语法跟踪进度。

**目标：** 确保 Web Gateway 持续运行时，在机载 `ground_link` 进程或完整机载栈重启后立即接收遥测，包括重复使用 `task_id=wildlife-demo` 的多次执行。

**架构：** 保持现有 Protobuf 消息模式不变。直接使用 `CLOCK_BOOTTIME` 的纳秒值为 `SequencedPublisher` 提供种子，使机载传输封套序号在同一次 Linux 启动期间具备进程重启安全性；同时，独立地将 Web 端每次应用新计划视为新的执行边界，使完整机载重启并重新加载后只复位 Gateway 的任务运行态，同时保留进程生命周期内的检测历史和全局命令 ACK 水位。

**技术栈：** C++17、ROS 2 Humble、ZeroMQ、Protobuf、Python 3.10、FastAPI、pytest、GoogleTest、colcon。

## 全局约束

- 不得修改任一仓库的 `shared/proto/messages.proto`；本修复不得引入重新生成 Protobuf 的依赖。
- 保持现有传输封套字段和 WebSocket 载荷结构不变。
- 高频遥测可以丢失中间帧，但重启后的发布端绝不能被上一进程留下的水位阻塞。
- 在同一次机载 OS 启动期间重启 `ground_link` 进程时，使用单调递增的 `CLOCK_BOOTTIME`；机载 OS 重启后，操作员必须重新规划并重新加载，让 Gateway 建立新的执行边界。
- 使用相同 `task_id` 重新规划时，启动全新的任务运行态视图：复位当前格、已巡检数量、统计总数、近期检测、摘要、错误、遥测 TTL、PID TTL、去重键和事件水位。
- 进程生命周期内的检测历史不随重新规划复位，只在 Gateway 进程退出时清除。
- Gateway 生命周期内不得因容量限制淘汰检测历史；即使载荷 JSON 中包含其他值，每条存储的检测记录也必须保留 Gateway 提供的规范 `task_id`。
- 命令 ACK 序号去重和命令链路健康状态在不同计划之间保持全局有效，不得复位；缓存的 `mission_loaded`、`mission_running` 和 `vision_armed` 标志必须复位为 `false`，因为它们描述的是已结束的计划执行。
- 不得提交生成的 `build/`、`install/`、`log/`、前端 `dist/`、运行时会话日志或 `.playwright-cli/` 产物。
- 两个仓库中可能已有用户的无关修改；每项任务只暂存其中明确列出的文件。

---

## 文件映射

### 机载仓库：`/home/sb/Ground_station/.worktrees/airborne-restart-safe`

- 修改 `src/ground_link/include/ground_link/event_gateway.hpp`：声明 `bootTimeNanoseconds()` 和接收 64 位有符号输入的 `initialPublishSequence(std::int64_t boot_time_ns)`。
- 修改 `src/ground_link/src/event_gateway.cpp`：读取 `CLOCK_BOOTTIME` 纳秒值，校验溢出，并将该 64 位纳秒种子转换为初始发布序号。
- 修改 `src/ground_link/src/ground_link_node.cpp`：使用 Linux `CLOCK_BOOTTIME` 纳秒种子初始化 `SequencedPublisher`，不再使用 `1`。
- 修改 `src/ground_link/test/test_envelope_codec.cpp`：验证 `bootTimeNanoseconds()` 读取 Linux 单调启动后运行时间、后启动的进程得到更大的纳秒序号，并保留进程内逐次递增行为。

### 地面站仓库：`/home/sb/Ground_station/.worktrees/nuedc-web`

- 修改 `web_ground_station/gateway/nuedc_web_gateway/state.py`：将每次应用计划都设为任务运行态复位边界，但不清除进程生命周期历史或 ACK 水位。
- 修改 `web_ground_station/tests/gateway/test_state.py`：覆盖同任务重新规划、重新规划后的较低事件序号、历史保留和 ACK 水位保留。
- 修改 `web_ground_station/tests/gateway/test_airborne.py`：覆盖同一 OS 启动期间重启后接收更高的发布序号、完整 OS 重启重新规划后接收较低序号，并拒绝已结束发布端的延迟数据包。
- 修改 `docs/dual_nuc_setup_guide.md`：记录重启恢复方式及其验收检查。

---

### 任务 1：使用启动时间为机载事件序号提供种子

**文件：**
- 修改：`/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/include/ground_link/event_gateway.hpp`
- 修改：`/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/src/event_gateway.cpp`
- 修改：`/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/src/ground_link_node.cpp`
- 测试：`/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/test/test_envelope_codec.cpp`

**接口：**
- 新增：`std::int64_t bootTimeNanoseconds();`
- 新增：`std::uint64_t initialPublishSequence(std::int64_t boot_time_ns);`
- 保持：`SequencedPublisher(Send send, std::uint64_t first_sequence)` 及其每次发布后递增的现有行为。

- [ ] **步骤 1：编写失败的启动序号测试**

添加准确表达重启合约的测试：

```cpp
TEST(EventGateway, PublishSequenceUsesRestartSafeBootEpoch)
{
    const auto first_boot = ground_link::initialPublishSequence(1'000'000'000'000);
    const auto later_boot = ground_link::initialPublishSequence(1'000'000'000'001);

    EXPECT_EQ(first_boot, 1'000'000'000'000u);
    EXPECT_GT(later_boot, first_boot);
}

TEST(EventGateway, BootTimeNanosecondsUsesLinuxBoottimeClock)
{
    const auto to_nanoseconds = [](const timespec &time) {
        return static_cast<std::int64_t>(time.tv_sec) * 1'000'000'000 + time.tv_nsec;
    };
    timespec before{};
    timespec after{};
    ASSERT_EQ(clock_gettime(CLOCK_BOOTTIME, &before), 0);

    const auto boot_time_ns = ground_link::bootTimeNanoseconds();

    ASSERT_EQ(clock_gettime(CLOCK_BOOTTIME, &after), 0);
    EXPECT_GE(boot_time_ns, to_nanoseconds(before));
    EXPECT_LE(boot_time_ns, to_nanoseconds(after));
}

TEST(EventGateway, NegativeBootTimeClampsToZero)
{
    EXPECT_EQ(ground_link::initialPublishSequence(-1), 0u);
}
```

在现有并发 `SequencedPublisher` 测试中，用启动序号域基值替换字面量种子和预期值，同时保留全部 200 次并发发布：

```cpp
const auto base = ground_link::initialPublishSequence(1'000'000'000'000);
ground_link::SequencedPublisher publisher([&](const std::string &bytes) {
    const auto parsed = ground_link::parseEnvelope(bytes);
    std::lock_guard<std::mutex> lock(sequences_mutex);
    wire_sequences.push_back(parsed.decoded_sequence);
    return true;
}, base);

// 现有线程创建和 join 保持不变。
ASSERT_EQ(wire_sequences.size(), 200u);
for (std::size_t index = 0; index < wire_sequences.size(); ++index) {
    EXPECT_EQ(wire_sequences[index], base + index);
}
```

- [ ] **步骤 2：运行聚焦测试并确认失败**

运行：

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
source /opt/ros/humble/setup.bash
CMAKE_BUILD_PARALLEL_LEVEL=2 MAKEFLAGS=-j2 \
  colcon build --packages-select ground_link --parallel-workers 1 \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

预期：由于 `bootTimeNanoseconds` 和 `initialPublishSequence` 尚不存在，编译失败。此处仅运行 `colcon test` 不足以验证，因为它不会重新构建已修改的测试可执行文件。

- [ ] **步骤 3：实现纳秒种子的确定性转换**

在 `event_gateway.hpp` 中添加：

```cpp
std::int64_t bootTimeNanoseconds();
std::uint64_t initialPublishSequence(std::int64_t boot_time_ns);
```

在 `event_gateway.cpp` 中添加：

```cpp
std::uint64_t initialPublishSequence(const std::int64_t boot_time_ns)
{
    return static_cast<std::uint64_t>(std::max<std::int64_t>(0, boot_time_ns));
}
```

添加所需的 `<algorithm>` 头文件。下限截断让函数对无效的负数输入也有明确结果；64 位有符号纳秒输入无需移位且不会溢出，可覆盖约 292 年的 OS 运行时间。

- [ ] **步骤 4：在 ROS 节点中初始化发布端种子**

在 `ground_link_node.cpp` 构造函数中只读取一次单调的 OS 启动后运行时间：

```cpp
const auto boot_time_ns = bootTimeNanoseconds();
sequenced_publisher_ = std::make_unique<SequencedPublisher>(
    [this](const std::string &bytes) { return send(bytes); },
    initialPublishSequence(boot_time_ns));
```

在 `event_gateway.cpp` 中用 `clock_gettime(CLOCK_BOOTTIME, ...)` 实现 `bootTimeNanoseconds()`，并添加所需的 `<cerrno>`、`<ctime>`、`<limits>` 和 `<system_error>` 头文件。在 `tv_sec` 乘以 `1'000'000'000` 前校验范围；时钟调用失败或运行时间无法表示时必须抛出异常，不能静默回退到 `system_clock` 或发生回绕。

不得在 LOAD、START、计划修订或执行 ID 变化时重新设置种子。序号域属于 `ground_link` 进程，并在其整个生命周期内保持单调递增。

- [ ] **步骤 5：运行包测试并确认通过**

运行：

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
source /opt/ros/humble/setup.bash
CMAKE_BUILD_PARALLEL_LEVEL=2 MAKEFLAGS=-j2 \
  colcon build --packages-select ground_link --parallel-workers 1 \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
CTEST_PARALLEL_LEVEL=1 colcon test --packages-select ground_link \
  --ctest-args -j1 --output-on-failure
colcon test-result --verbose
```

预期：`ground_link` 构建成功，包内全部测试通过且失败数为零。

- [ ] **步骤 6：提交机载端修改**

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
git add \
  src/ground_link/include/ground_link/event_gateway.hpp \
  src/ground_link/src/event_gateway.cpp \
  src/ground_link/src/ground_link_node.cpp \
  src/ground_link/test/test_envelope_codec.cpp
git commit -m "fix(ground-link): make telemetry sequence restart-safe"
```

---

### 任务 2：将同任务重新规划视为新的 Gateway 执行边界

**文件：**
- 修改：`web_ground_station/gateway/nuedc_web_gateway/state.py`
- 测试：`web_ground_station/tests/gateway/test_state.py`

**接口：**
- 输入：任务 1 生成的重启安全机载 `Envelope.sequence` 值。
- 输出：`GroundState.apply_plan(...)` 在安装新计划前始终复位任务运行态字段。
- 保持：重新规划前后的 `GroundState.detection_history(task_id: str | None = None)`。
- 保持全局：ACK 水位、ACK 身份与消息、命令链路健康状态；重新规划时将已结束计划的 `mission_loaded`、`mission_running` 和 `vision_armed` 清为 `false`。

- [ ] **步骤 1：编写失败的同任务重新规划测试**

添加测试：先建立旧运行态和较高的事件水位，再应用具有相同任务 ID 的另一份计划：

```python
def test_same_task_replan_starts_fresh_runtime_and_accepts_new_sequence():
    state = active_state("wildlife-demo")
    state.apply_task_event(
        "wildlife-demo", "telemetry", 90_000, 101,
        {"current_cell": "A1B2", "visited_cells": 9},
    )
    state.apply_task_event(
        "wildlife-demo", "detection", 90_001, 102,
        {"track_id": "old-track", "animal_name": "hare", "count": 1},
    )

    state.apply_plan(
        {
            "task_id": "wildlife-demo",
            "message_type": "task_plan",
            "waypoints": [{"id": "A9B1"}, {"id": "touchdown"}],
        },
        103,
    )

    reset = state.snapshot(104)
    assert reset.current_cell is None
    assert reset.visited_count == 0
    assert reset.detection_totals == {}
    assert reset.recent_detections == []
    assert state.detection_history() == [
        {
            "task_id": "wildlife-demo",
            "track_id": "old-track",
            "animal_name": "hare",
            "count": 1,
        }
    ]

    accepted = state.apply_task_event(
        "wildlife-demo", "telemetry", 1, 105,
        {"current_cell": "A9B1", "visited_cells": 1},
    )
    assert accepted is not None
    assert state.snapshot(106).current_cell == "A9B1"
```

另添加以下进程生命周期容量和规范归属检查：

```python
def test_detection_history_survives_replan_without_capacity_eviction():
    state = active_state()
    for seq in range(1, 502):
        state.apply_task_event(
            "active",
            "detection",
            seq,
            100 + seq,
            {"track_id": f"track-{seq}", "animal_name": "hare", "count": 1},
        )

    state.apply_plan(
        {"task_id": "active", "message_type": "task_plan", "waypoints": []},
        700,
    )

    assert len(state.detection_history()) == 501


def test_detection_history_uses_canonical_task_id():
    state = active_state()
    state.apply_task_event(
        "active",
        "detection",
        1,
        101,
        {"task_id": "spoofed", "track_id": "track-1", "animal_name": "hare"},
    )

    assert state.detection_history("active")[0]["task_id"] == "active"
    assert state.detection_history("spoofed") == []
```

- [ ] **步骤 2：编写 ACK 水位保留测试**

添加以下测试，确保替换计划不会削弱命令重放保护：

```python
def test_same_task_replan_preserves_global_ack_sequence_watermark():
    state = active_state("wildlife-demo")
    state.apply_ack(
        AckSnapshot(
            ok=True,
            message="newest ack",
            task_id="wildlife-demo",
            mission_loaded=True,
            mission_running=False,
            last_accepted_sequence=100,
            vision_armed=False,
        ),
        101,
    )
    state.apply_plan(
        {
            "task_id": "wildlife-demo",
            "message_type": "task_plan",
            "waypoints": [],
        },
        102,
    )
    state.apply_ack(
        AckSnapshot(
            ok=True,
            message="stale ack",
            task_id="wildlife-demo",
            mission_loaded=False,
            mission_running=False,
            last_accepted_sequence=99,
            vision_armed=False,
        ),
        103,
    )

    snapshot = state.snapshot(104)
    assert snapshot.ack is not None
    assert snapshot.ack.message == "newest ack"
    assert snapshot.ack.last_accepted_sequence == 100
    assert snapshot.command_link == "online"
    assert snapshot.mission_loaded is False
```

- [ ] **步骤 3：运行两组测试并确认失败**

运行：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway/test_state.py \
  -k "same_task_replan or ack_watermark" -vv
```

预期：运行态复位测试失败，因为 `apply_plan` 只在 `task_id` 变化时调用 `_reset_task_state()`；ACK 测试在实现前后都应通过。

- [ ] **步骤 4：对每份新计划复位任务运行态**

将 `apply_plan` 临界区从条件复位改为无条件复位：

```python
with self._lock:
    self._active_task_id = task_id
    self._plan = plan_copy
    self._reset_task_state()
    if self._ack is not None:
        self._ack = self._ack.model_copy(
            update={
                "mission_loaded": False,
                "mission_running": False,
                "vision_armed": False,
            }
        )
    snapshot_seq = self._advance_snapshot()
```

保持 `_detection_history`、`_highest_ack_sequence`、`_ack`、`_last_command_success_ms` 和 `_command_failures` 不受 `_reset_task_state()` 影响。使用不设容量上限、生命周期与进程一致的双端队列存储历史，并以 `{**payload, "task_id": task_id}` 写入检测记录，防止载荷中的键替换规范任务 ID。保留 ACK 的身份、消息和序号，但按上例清除已结束计划的任务标志。重新规划时不得清除进程生命周期检测历史或命令连接状态。

- [ ] **步骤 5：运行聚焦测试和完整状态测试**

运行：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway/test_state.py -q
```

预期：全部状态测试通过，包括同任务重新规划和 ACK 水位保留测试。

- [ ] **步骤 6：提交 Gateway 状态修改**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git add \
  web_ground_station/gateway/nuedc_web_gateway/state.py \
  web_ground_station/tests/gateway/test_state.py
git commit -m "fix(gateway): reset runtime state for repeated plans"
```

---

### 任务 3：在 Gateway 协议边界锁定跨重启合约

**文件：**
- 修改：`web_ground_station/tests/gateway/test_airborne.py`

**接口：**
- 使用：任务 2 中 `GroundState.apply_plan(...)` 建立的执行态复位。
- 验证两种模式：同一 OS 启动期间只重启 `ground_link`，不重新规划，依靠更高的 `CLOCK_BOOTTIME` 纳秒启动序号域立即接收新发布端数据；完整 OS 重启则必须使用相同任务 ID 重新规划并复位运行态，然后接收可能较低的发布序号。两种模式都必须拒绝已结束发布端的延迟事件。

- [ ] **步骤 1：添加协议边界回归测试**

使用现有 `task_event(...)`、`plan_for(...)`、`transport_fixture` 和真实 Protobuf 传输路径：第一项测试模拟同一 OS 启动期间只重启 `ground_link`，不重新规划；第二项测试模拟完整 OS 重启，保留同一任务 ID 的重新规划与运行态复位。

```python
@pytest.mark.asyncio
async def test_restart_safe_publisher_epoch_rejects_retired_packets(
    transport_fixture,
):
    client, ground_state, _, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("wildlife-demo"), now_ms())

    old_sequence = 1_000_000_500
    old_publish = asyncio.create_task(
        pub_server.publish(
            task_event(
                "wildlife-demo",
                "telemetry",
                old_sequence,
                {"current_cell": "A1B2", "visited_cells": 9},
            )
        )
    )
    assert await client.receive_one_telemetry() is not None
    await old_publish

    new_sequence = 1_001_000_000
    new_publish = asyncio.create_task(
        pub_server.publish(
            task_event(
                "wildlife-demo",
                "telemetry",
                new_sequence,
                {"current_cell": "A9B1", "visited_cells": 1},
            )
        )
    )
    assert await client.receive_one_telemetry() is not None
    await new_publish
    assert ground_state.snapshot(now_ms()).current_cell == "A9B1"

    delayed_publish = asyncio.create_task(
        pub_server.publish(
            task_event(
                "wildlife-demo",
                "telemetry",
                old_sequence,
                {"current_cell": "A1B2", "visited_cells": 9},
            )
        )
    )
    assert await client.receive_one_telemetry() is None
    await delayed_publish
    assert ground_state.snapshot(now_ms()).current_cell == "A9B1"


@pytest.mark.asyncio
async def test_full_airborne_reboot_accepts_lower_publisher_epoch(
    transport_fixture,
):
    client, ground_state, _, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("wildlife-demo"), now_ms())

    old_publish = asyncio.create_task(
        pub_server.publish(
            task_event(
                "wildlife-demo",
                "telemetry",
                1_000_000_500,
                {"current_cell": "A1B2", "visited_cells": 9},
            )
        )
    )
    assert await client.receive_one_telemetry() is not None
    await old_publish

    ground_state.apply_plan(plan_for("wildlife-demo"), now_ms())
    rebooted_publish = asyncio.create_task(
        pub_server.publish(
            task_event(
                "wildlife-demo",
                "telemetry",
                100,
                {"current_cell": "A9B1", "visited_cells": 1},
            )
        )
    )
    assert await client.receive_one_telemetry() is not None
    await rebooted_publish
    assert ground_state.snapshot(now_ms()).current_cell == "A9B1"
```

- [ ] **步骤 2：运行回归测试**

运行：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway/test_airborne.py \
  -k "restart_safe_publisher_epoch or full_airborne_reboot" -vv
```

预期：完成任务 1 和任务 2 后，两项测试均通过。为证明较低启动序号域测试有效，可在本地临时恢复同任务条件复位，重新运行并确认只有 `test_full_airborne_reboot_accepts_lower_publisher_epoch` 失败，然后在继续前恢复实现。不得提交该临时回退。

- [ ] **步骤 3：运行完整 Gateway 测试套件和静态检查**

运行：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway -q
.venv/bin/ruff check gateway tests/gateway
```

预期：全部 Gateway 测试通过；Ruff 报告 `All checks passed!`。

- [ ] **步骤 4：提交边界测试**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git add web_ground_station/tests/gateway/test_airborne.py
git commit -m "test(gateway): cover airborne publisher restarts"
```

---

### 任务 4：记录并执行两种重启方式的验收测试

**文件：**
- 修改：`docs/dual_nuc_setup_guide.md`

**接口：**
- 验证已部署机载 `ground_link` 与 Gateway 在 `tcp://10.42.0.2:5557` 和 `tcp://10.42.0.2:5558` 上的行为。

- [ ] **步骤 1：添加重启验收流程**

准确记录以下流程：

```markdown
### 机载重启后的遥测恢复

1. 保持 Web Gateway 运行，不要重启地面站服务。
2. 完成一次 `LOAD -> START`，确认 `/api/snapshot` 的 `visited_count` 增长。
3. 模式 A：仅重启机载 `ground_link`，不重新规划、不刷新浏览器，继续观察当前任务。
4. 模式 B：完整重启机载 OS 或完整 `sim_airborne.launch.py`，重新规划同一个 `task_id=wildlife-demo`，再执行 `LOAD -> START`。
5. 两种模式都在 5 秒内确认 `telemetry_link=online`；模式 A 的 `current_cell` 继续更新，模式 B 的 `current_cell` 从 `A9B1` 开始更新，且新执行的 `visited_count` 从 0/1 开始增长。
6. 确认任务结束后收到对应新执行的 `recent_summary`，而不是等待旧序号水位自然追平。
```

同时注明：仅重启机载发布端时，不得要求重启 Gateway、重新规划或刷新浏览器；它依靠同一 OS 启动期间更高的 `CLOCK_BOOTTIME` 纳秒启动序号域恢复。完整重启机载 OS 后，`CLOCK_BOOTTIME` 从新一次系统启动开始计时，因此新发布序号可以低于重启前的序号；重新规划建立新的 Gateway 执行边界后，首条新遥测必须立即被接收。

- [ ] **步骤 2：构建并部署聚焦的机载包**

在机载工作区运行：

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
source /opt/ros/humble/setup.bash
CMAKE_BUILD_PARALLEL_LEVEL=2 MAKEFLAGS=-j2 \
  colcon build --packages-select ground_link --parallel-workers 1 \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

通过仓库现有的机载部署流程部署生成的 `ground_link` 包；不得手工复制单个共享库。

- [ ] **步骤 3：启动比赛模式 Gateway**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
web_ground_station/scripts/start_competition.sh
```

预期：`http://10.42.0.1:8000/api/health` 返回 `{"ok":true}`，且命令和遥测端点均通过启动前检查。

- [ ] **步骤 4：执行在线重启场景**

在前后两次执行期间对 Gateway 采样：

```bash
curl -fsS http://10.42.0.1:8000/api/snapshot
```

在 `web_ground_station/` 目录使用以下准确命令，分别在重启前和重启后独立采样一个机载传输序号：

```bash
PYTHONPATH=gateway .venv/bin/python -c 'import zmq; from nuedc_web_gateway.proto_runtime import load_messages_module; c=zmq.Context(); s=c.socket(zmq.SUB); s.setsockopt(zmq.SUBSCRIBE, b""); s.setsockopt(zmq.RCVTIMEO, 5000); s.connect("tcp://10.42.0.2:5557"); print(load_messages_module().Envelope.FromString(s.recv()).sequence); s.close(); c.term()'
```

记录两个打印出的整数。模式 A 只在同一次机载 OS 启动期间重启 `ground_link`，不得重新规划，必须断言重启后的第一个序号大于重启前的最后一个序号；模式 B 完整重启机载 OS，允许新序号更小，但必须先使用同一 `task_id=wildlife-demo` 重新规划并重新 `LOAD`。两种场景都必须断言 Gateway 在 5 秒内变为 `telemetry_link=online`，且新执行的首条遥测立即更新运行态。

- [ ] **步骤 5：验证最终仓库状态**

地面站：

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git diff --check
git status --short
```

机载端：

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
git diff --check
git status --short
```

预期：没有生成的 build/install/log/runtime 产物被暂存；提交中只包含预期的源文件、测试文件和文档文件。

- [ ] **步骤 6：提交验收文档**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git add docs/dual_nuc_setup_guide.md
git commit -m "docs: add airborne restart telemetry check"
```

---

## 最终验证矩阵

| 场景 | 必须满足的结果 |
|---|---|
| 不重启任何进程，使用相同任务 ID 重新规划 | UI 运行态立即复位；接收第一条新遥测 |
| Gateway 持续运行，同一次机载 OS 启动期间重启 `ground_link` | 新启动序号域的首个序号大于原水位 |
| 完整重启机载 OS 后使用相同任务 ID 重新规划并重新加载 | 即使新发布序号较低，首条新遥测也会立即被接收 |
| 已结束发布端的延迟数据包到达 | 拒绝该数据包，且不得覆盖新执行状态 |
| 重新规划后收到序号更旧的命令 ACK | 拒绝该 ACK；命令重放保护保持有效 |
| 检测发生后重新规划 | 清除当前统计总数，进程生命周期历史仍可查询 |
| 机载重启后任务完成 | `current_cell`、`visited_count` 和 `recent_summary` 均达到新执行的最终状态 |

## 明确不在范围内的事项

- 本修复不为 `plan_revision`、`execution_id` 或启动 UUID 添加 Protobuf 字段。
- 不引入数据库或持久化序号存储。
- 不引入浏览器端规避方案、轮询回退或强制 Gateway 重启。
- 不修改命令序号生成；它已经使用基于时间的发送端启动序号域。
- 不尝试让高频 PUB 遥测具备可靠传输；如果后续丢包测试证明现有路径不足，只有任务事件和摘要需要另行开展可靠性工作。
