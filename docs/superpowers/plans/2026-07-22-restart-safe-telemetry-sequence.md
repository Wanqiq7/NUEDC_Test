# Restart-Safe Telemetry Sequence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ensure a running Web Gateway immediately accepts telemetry after the airborne `ground_link` process or the complete airborne stack restarts, including repeated executions that reuse `task_id=wildlife-demo`.

**Architecture:** Keep the existing Protobuf schema unchanged. Make the airborne envelope sequence restart-safe by seeding `SequencedPublisher` from a Unix-millisecond boot epoch shifted by 20 counter bits; independently treat every newly applied Web plan as a new execution boundary and reset only Gateway task-runtime state, while preserving process-lifetime detection history and the global command ACK watermark.

**Tech Stack:** C++17, ROS 2 Humble, ZeroMQ, Protobuf, Python 3.10, FastAPI, pytest, GoogleTest, colcon.

## Global Constraints

- Do not modify either repository's `shared/proto/messages.proto`; this fix must not introduce a Protobuf regeneration dependency.
- Preserve the existing wire envelope fields and WebSocket payload shape.
- High-frequency telemetry may drop intermediate frames, but a restarted publisher must never be blocked by a watermark from its previous process.
- Replanning with the same `task_id` starts a fresh task-runtime view: current cell, visited count, totals, recent detections, summary, error, telemetry TTL, PID TTL, deduplication keys, and event watermark reset.
- Process-lifetime detection history does not reset on replanning; it resets only when the Gateway process exits.
- Command ACK sequence deduplication remains global across plans and must not reset.
- Do not commit generated `build/`, `install/`, `log/`, frontend `dist/`, runtime session logs, or `.playwright-cli/` artifacts.
- Both repositories may already contain unrelated user changes; stage only the exact files listed in each task.

---

## File Map

### Airborne repository: `/home/sb/Ground_station/.worktrees/airborne-restart-safe`

- Modify `src/ground_link/include/ground_link/event_gateway.hpp`: declare the restart-safe sequence epoch function and counter-bit constant.
- Modify `src/ground_link/src/event_gateway.cpp`: implement deterministic epoch construction with overflow validation.
- Modify `src/ground_link/src/ground_link_node.cpp`: seed `SequencedPublisher` from the current Unix time instead of `1`.
- Modify `src/ground_link/test/test_envelope_codec.cpp`: prove later boots dominate all ordinary event counts from earlier boots and preserve per-process incrementing.

### Ground-station repository: `/home/sb/Ground_station/.worktrees/nuedc-web`

- Modify `web_ground_station/gateway/nuedc_web_gateway/state.py`: make every applied plan a task-runtime reset boundary without clearing process-lifetime history or ACK watermark.
- Modify `web_ground_station/tests/gateway/test_state.py`: cover same-task replanning, lower post-replan event sequences, history preservation, and ACK watermark preservation.
- Modify `web_ground_station/tests/gateway/test_airborne.py`: cover acceptance of a fresh publisher epoch and rejection of delayed packets from the retired epoch.
- Modify `docs/dual_nuc_setup_guide.md`: document restart recovery and its acceptance check.

---

### Task 1: Seed Airborne Event Sequences From a Boot Epoch

**Files:**
- Modify: `/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/include/ground_link/event_gateway.hpp`
- Modify: `/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/src/event_gateway.cpp`
- Modify: `/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/src/ground_link_node.cpp`
- Test: `/home/sb/Ground_station/.worktrees/airborne-restart-safe/src/ground_link/test/test_envelope_codec.cpp`

**Interfaces:**
- Produces: `constexpr std::uint32_t kPublishSequenceCounterBits = 20;`
- Produces: `std::uint64_t initialPublishSequence(std::int64_t unix_time_ms);`
- Preserves: `SequencedPublisher(Send send, std::uint64_t first_sequence)` and its existing increment-on-publish behavior.

- [ ] **Step 1: Write the failing epoch tests**

Add tests that express the exact restart contract:

```cpp
TEST(EventGateway, PublishSequenceUsesRestartSafeBootEpoch)
{
    const auto first_boot = ground_link::initialPublishSequence(1'000'000);
    const auto later_boot = ground_link::initialPublishSequence(1'000'001);

    EXPECT_EQ(
        first_boot,
        static_cast<std::uint64_t>(1'000'000)
            << ground_link::kPublishSequenceCounterBits);
    EXPECT_GT(later_boot, first_boot + 100'000);
}

TEST(EventGateway, NegativeBootTimeClampsToZero)
{
    EXPECT_EQ(ground_link::initialPublishSequence(-1), 0u);
}
```

In the existing concurrent `SequencedPublisher` test, replace the literal seed and expected values with the epoch base while retaining all 200 concurrent publishes:

```cpp
const auto base = ground_link::initialPublishSequence(1'000'000);
ground_link::SequencedPublisher publisher([&](const std::string &bytes) {
    const auto parsed = ground_link::parseEnvelope(bytes);
    std::lock_guard<std::mutex> lock(sequences_mutex);
    wire_sequences.push_back(parsed.sequence());
    return true;
}, base);

// Existing thread creation and joins remain unchanged.
ASSERT_EQ(wire_sequences.size(), 200u);
for (std::size_t index = 0; index < wire_sequences.size(); ++index) {
    EXPECT_EQ(wire_sequences[index], base + index);
}
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
source /opt/ros/humble/setup.bash
CTEST_PARALLEL_LEVEL=1 colcon test --packages-select ground_link \
  --ctest-args -j1 --output-on-failure
colcon test-result --verbose
```

Expected: compilation fails because `initialPublishSequence` and `kPublishSequenceCounterBits` do not exist.

- [ ] **Step 3: Implement deterministic epoch construction**

Add to `event_gateway.hpp`:

```cpp
constexpr std::uint32_t kPublishSequenceCounterBits = 20;
std::uint64_t initialPublishSequence(std::int64_t unix_time_ms);
```

Add to `event_gateway.cpp`:

```cpp
std::uint64_t initialPublishSequence(const std::int64_t unix_time_ms)
{
    const auto non_negative = static_cast<std::uint64_t>(std::max<std::int64_t>(0, unix_time_ms));
    constexpr auto max_epoch =
        std::numeric_limits<std::uint64_t>::max() >> kPublishSequenceCounterBits;
    return std::min(non_negative, max_epoch) << kPublishSequenceCounterBits;
}
```

Add the required `<algorithm>` and `<limits>` includes. The clamp makes the function defined even if the system clock is invalid or far outside the representable epoch.

- [ ] **Step 4: Seed the publisher in the ROS node**

In `ground_link_node.cpp`, compute the boot epoch once in the constructor:

```cpp
const auto unix_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
sequenced_publisher_ = std::make_unique<SequencedPublisher>(
    [this](const std::string &bytes) { return send(bytes); },
    initialPublishSequence(unix_time_ms));
```

Do not reseed on LOAD, START, plan revision, or execution ID. The sequence domain belongs to the `ground_link` process and remains monotonic for its entire lifetime.

- [ ] **Step 5: Run package tests and verify GREEN**

Run:

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

Expected: `ground_link` builds and all package tests pass with zero failures.

- [ ] **Step 6: Commit the airborne change**

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

### Task 2: Treat Same-Task Replanning as a New Gateway Execution Boundary

**Files:**
- Modify: `web_ground_station/gateway/nuedc_web_gateway/state.py`
- Test: `web_ground_station/tests/gateway/test_state.py`

**Interfaces:**
- Consumes: restart-safe airborne `Envelope.sequence` values from Task 1.
- Produces: `GroundState.apply_plan(...)` always resets task-runtime fields before installing the new plan.
- Preserves: `GroundState.detection_history(task_id: str | None = None)` across replans.

- [ ] **Step 1: Write the failing same-task replan test**

Add a test that first establishes old runtime state and a high event watermark, then applies another plan with the same task ID:

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

- [ ] **Step 2: Write the ACK-watermark preservation test**

Add this test so plan replacement cannot weaken command replay protection:

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
    assert snapshot.mission_loaded is True
```

- [ ] **Step 3: Run both tests and verify RED**

Run:

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway/test_state.py \
  -k "same_task_replan or ack_watermark" -vv
```

Expected: the runtime-reset test fails because `apply_plan` only calls `_reset_task_state()` when `task_id` changes; the ACK test should pass before and after the implementation.

- [ ] **Step 4: Reset task runtime for every new plan**

Change the `apply_plan` critical section from conditional reset to unconditional reset:

```python
with self._lock:
    self._active_task_id = task_id
    self._plan = plan_copy
    self._reset_task_state()
    snapshot_seq = self._advance_snapshot()
```

Keep `_detection_history`, `_highest_ack_sequence`, `_ack`, `_last_command_success_ms`, and `_command_failures` outside `_reset_task_state()`. Do not clear process-lifetime detection history or command connectivity when replanning.

- [ ] **Step 5: Run focused and full state tests**

Run:

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway/test_state.py -q
```

Expected: all state tests pass, including same-task replan and ACK watermark preservation.

- [ ] **Step 6: Commit the Gateway state change**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git add \
  web_ground_station/gateway/nuedc_web_gateway/state.py \
  web_ground_station/tests/gateway/test_state.py
git commit -m "fix(gateway): reset runtime state for repeated plans"
```

---

### Task 3: Lock the Cross-Restart Contract at the Gateway Protocol Boundary

**Files:**
- Modify: `web_ground_station/tests/gateway/test_airborne.py`

**Interfaces:**
- Consumes: `GroundState.apply_plan(...)` execution reset from Task 2.
- Verifies: a new publisher epoch is accepted immediately and a delayed event from the retired epoch cannot overwrite it.

- [ ] **Step 1: Add the protocol-boundary regression test**

Use the existing `task_event(...)`, `plan_for(...)`, `transport_fixture`, and real Protobuf transport path:

```python
@pytest.mark.asyncio
async def test_restart_safe_publisher_epoch_rejects_retired_packets(
    transport_fixture,
):
    client, ground_state, _, _, pub_server = transport_fixture
    ground_state.apply_plan(plan_for("wildlife-demo"), now_ms())

    old_sequence = (1_000_000 << 20) + 500
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

    ground_state.apply_plan(plan_for("wildlife-demo"), now_ms())
    new_sequence = 1_000_001 << 20
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
```

- [ ] **Step 2: Run the regression test**

Run:

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway/test_airborne.py \
  -k restart_safe_publisher_epoch -vv
```

Expected after Tasks 1 and 2: PASS. To prove the test is meaningful, temporarily restore the conditional same-task reset locally, rerun and observe failure, then restore the implementation before continuing. Do not commit the temporary reversion.

- [ ] **Step 3: Run the full Gateway suite and lint**

Run:

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web/web_ground_station
.venv/bin/pytest tests/gateway -q
.venv/bin/ruff check gateway tests/gateway
```

Expected: all Gateway tests pass; Ruff reports `All checks passed!`.

- [ ] **Step 4: Commit the boundary test**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git add web_ground_station/tests/gateway/test_airborne.py
git commit -m "test(gateway): cover airborne publisher restarts"
```

---

### Task 4: Document and Perform the Dual-Process Restart Acceptance Test

**Files:**
- Modify: `docs/dual_nuc_setup_guide.md`

**Interfaces:**
- Verifies deployed airborne `ground_link` and Gateway behavior over `tcp://10.42.0.2:5557` and `tcp://10.42.0.2:5558`.

- [ ] **Step 1: Add the restart acceptance procedure**

Document this exact sequence:

```markdown
### 机载重启后的遥测恢复

1. 保持 Web Gateway 运行，不要重启地面站服务。
2. 完成一次 `LOAD -> START`，确认 `/api/snapshot` 的 `visited_count` 增长。
3. 重启机载 `ground_link` 或完整 `sim_airborne.launch.py`。
4. 重新规划同一个 `task_id=wildlife-demo`，执行 `LOAD -> START`。
5. 5 秒内确认 `telemetry_link=online`、`current_cell` 从 `A9B1` 开始更新，且 `visited_count` 从 0/1 开始增长。
6. 确认任务结束后收到 `recent_summary`，而不是等待旧序号水位自然追平。
```

Also state that restarting only the airborne publisher must not require a Gateway restart or browser refresh.

- [ ] **Step 2: Build and deploy the focused airborne package**

On the airborne workspace:

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
source /opt/ros/humble/setup.bash
CMAKE_BUILD_PARALLEL_LEVEL=2 MAKEFLAGS=-j2 \
  colcon build --packages-select ground_link --parallel-workers 1 \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash
```

Deploy the resulting `ground_link` package through the repository's existing airborne deployment procedure; do not copy individual shared libraries by hand.

- [ ] **Step 3: Start the competition-mode Gateway**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
web_ground_station/scripts/start_competition.sh
```

Expected: `http://10.42.0.1:8000/api/health` returns `{"ok":true}` and both command and telemetry endpoints pass preflight.

- [ ] **Step 4: Run the live restart scenario**

During both executions, sample the Gateway:

```bash
curl -fsS http://10.42.0.1:8000/api/snapshot
```

Independently sample one airborne wire sequence immediately before and after restart with this exact command from `web_ground_station/`:

```bash
.venv/bin/python -c 'import zmq; from nuedc_web_gateway.proto_runtime import load_messages_module; c=zmq.Context(); s=c.socket(zmq.SUB); s.setsockopt(zmq.SUBSCRIBE,b""); s.setsockopt(zmq.RCVTIMEO,5000); s.connect("tcp://10.42.0.2:5557"); print(load_messages_module().Envelope.FromString(s.recv()).sequence); s.close(); c.term()'
```

Record both printed integers. Assert the first post-restart sequence is greater than the final pre-restart sequence and that the Gateway becomes `telemetry_link=online` within 5 seconds.

- [ ] **Step 5: Verify final repository state**

Ground station:

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git diff --check
git status --short
```

Airborne:

```bash
cd /home/sb/Ground_station/.worktrees/airborne-restart-safe
git diff --check
git status --short
```

Expected: no generated build/install/log/runtime artifacts are staged; only intended source, test, and documentation files appear in commits.

- [ ] **Step 6: Commit the acceptance documentation**

```bash
cd /home/sb/Ground_station/.worktrees/nuedc-web
git add docs/dual_nuc_setup_guide.md
git commit -m "docs: add airborne restart telemetry check"
```

---

## Final Verification Matrix

| Scenario | Required result |
|---|---|
| Same task ID is replanned without any process restart | UI runtime resets immediately; first new telemetry is accepted |
| `ground_link` restarts while Gateway remains running | First new epoch sequence exceeds the previous watermark |
| Delayed packet from the retired publisher arrives | Packet is rejected and cannot overwrite the new execution |
| Command ACK arrives with an older sequence after replan | ACK is rejected; command replay protection remains intact |
| Replan occurs after detections | Current totals clear, process-lifetime history remains available |
| Task completes after airborne restart | `current_cell`, `visited_count`, and `recent_summary` all reach the new execution's final state |

## Explicit Non-Goals

- No Protobuf field additions for `plan_revision`, `execution_id`, or a boot UUID in this fix.
- No database or persistent sequence store.
- No browser-side workaround, polling fallback, or forced Gateway restart.
- No change to command sequence generation; it already uses a time-based sender epoch.
- No attempt to make high-frequency PUB telemetry reliable; only task events and summaries require separate reliability work if packet-loss testing later proves the existing path insufficient.
