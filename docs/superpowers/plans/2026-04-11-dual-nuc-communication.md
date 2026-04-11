# Dual NUC Communication Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在现有 `ZeroMQ + Protobuf` 架构上补齐地面站 NUC 到机载 NUC 的任务下发确认闭环，同时保留机载端到地面站的遥测上报链路。

**Architecture:** 继续保留机载端 `PUB -> SUB` 的上行链路用于 `GridConfig/Telemetry/AnimalDetection/MissionSummary`，新增地面站 `REQ -> REP` 的下行任务同步链路。协议层复用 `GridConfig` 承载任务计划，新增 `Ack` 用于确认结果；机载端增加 `command_server.py` 写入 `runtime/active_mission_plan.json`，地面站端增加 `ZmqCommandClient` 并在 `MainWindow` 中接入下发结果状态。

**Tech Stack:** Protobuf, ZeroMQ, Python 3.10, unittest, C++17, Qt6 Widgets, QtTest, CMake

---

### Task 1: 扩展共享协议以支持任务确认

**Files:**
- Modify: `shared/proto/messages.proto`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: 在协议里先写出最小确认结构**

```proto
message Ack {
  bool success = 1;
  string message = 2;
}
```

- [ ] **Step 2: 扩展 `Envelope` 的 `oneof payload`**

```proto
oneof payload {
  GridConfig grid_config = 10;
  Telemetry telemetry = 11;
  AnimalDetection animal_detection = 12;
  MissionSummary mission_summary = 13;
  GridConfig mission_load = 14;
  Ack ack = 15;
}
```

- [ ] **Step 3: 重新配置并生成协议代码**

Run: `cmake -S /home/qwe/XT -B /home/qwe/XT/build`
Expected: PASS，`build/generated/proto/messages.pb.*` 与 `airborne/uav_testbed/generated/messages_pb2.py` 更新成功。

### Task 2: 实现机载端任务接收服务

**Files:**
- Create: `airborne/uav_testbed/command_server.py`
- Create: `airborne/tests/test_command_server.py`
- Modify: `airborne/uav_testbed/run_simulator.py`

- [ ] **Step 1: 先写失败测试，锁定任务落盘与 Ack 语义**

```python
def test_store_received_mission_plan_persists_runtime_file(tmp_path):
    payload = {
        "case_id": "demo",
        "start_cell": "A9B1",
        "no_fly_cells": ["A1B2", "A2B2", "A3B2"],
        "route": ["A9B1", "A8B1"],
        "terminal_cell": "A8B1",
        "landing_enabled": True,
        "descent_angle_deg": 45.0,
        "takeoff_anchor_x_cm": 450.0,
        "takeoff_anchor_y_cm": 350.0,
    }
    ack = store_mission_plan(payload, tmp_path / "active_mission_plan.json")
    assert ack["success"] is True
```

- [ ] **Step 2: 实现最小持久化与校验函数**

```python
def validate_mission_plan(plan: dict) -> tuple[bool, str]:
    required = ("case_id", "start_cell", "route", "terminal_cell", "no_fly_cells")
    for key in required:
        if key not in plan:
            return False, f"missing {key}"
    if not plan["route"]:
        return False, "route is empty"
    if set(plan["route"]) & set(plan["no_fly_cells"]):
        return False, "route intersects no_fly_cells"
    return True, ""

def store_mission_plan(plan: dict, output_path: Path) -> dict:
    ok, message = validate_mission_plan(plan)
    if not ok:
        return {"success": False, "message": message}
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(plan, ensure_ascii=False), encoding="utf-8")
    return {"success": True, "message": "mission stored"}
```

- [ ] **Step 3: 实现 REP 服务循环**

```python
def serve_command_endpoint(endpoint: str, output_path: Path) -> None:
    context = zmq.Context.instance()
    socket = context.socket(zmq.REP)
    socket.bind(endpoint)
    while True:
        envelope = messages_pb2.Envelope()
        envelope.ParseFromString(socket.recv())
        if envelope.WhichOneof("payload") != "mission_load":
            socket.send(build_ack(False, "unsupported payload"))
            continue
        plan = mission_load_to_dict(envelope.mission_load)
        ack = store_mission_plan(plan, output_path)
        socket.send(build_ack(ack["success"], ack["message"]))
```

- [ ] **Step 4: 给 `run_simulator.py` 补一个可选命令服务入口**

```python
parser.add_argument("--command-endpoint", default="", help="任务接收 REP 地址；为空时不启动")

if args.command_endpoint:
    server_thread = threading.Thread(
        target=serve_command_endpoint,
        args=(args.command_endpoint, Path(args.mission_plan)),
        daemon=True,
    )
    server_thread.start()
```

- [ ] **Step 5: 运行 Python 测试**

Run: `python3 -m unittest discover -s /home/qwe/XT/airborne/tests -v`
Expected: PASS，新增的 `test_command_server.py` 与既有机载端测试全部通过。

### Task 3: 实现地面站端任务发送客户端

**Files:**
- Create: `ground_control/src/zmq_command_client.h`
- Create: `ground_control/src/zmq_command_client.cpp`
- Create: `ground_control/tests/test_zmq_command_client.cpp`
- Modify: `ground_control/CMakeLists.txt`

- [ ] **Step 1: 先写失败测试，锁定发送结果对象**

```cpp
void ZmqCommandClientTests::parsesAckPayload() {
    const QByteArray bytes = ...; // 带 ack=true, message="ok" 的 Envelope
    const auto result = ZmqCommandClient::parseAck(bytes);
    QVERIFY(result.ok);
    QCOMPARE(result.message, QString("ok"));
}
```

- [ ] **Step 2: 定义最小结果类型与解析函数**

```cpp
struct CommandSendResult {
    bool ok = false;
    QString message;
};

class ZmqCommandClient {
public:
    explicit ZmqCommandClient(QString endpoint = "tcp://127.0.0.1:5558");
    static CommandSendResult parseAck(const QByteArray &payload);
    CommandSendResult sendMissionPlan(const MissionPlanData &plan) const;
private:
    QString endpoint_;
};
```

- [ ] **Step 3: 实现 `sendMissionPlan(...)`**

```cpp
Envelope envelope;
auto *payload = envelope.mutable_mission_load();
payload->set_case_id(plan.case_id.toStdString());
payload->set_start_cell(plan.start_cell.toStdString());
...
zmq::context_t context(1);
zmq::socket_t socket(context, zmq::socket_type::req);
socket.connect(endpoint_.toStdString());
socket.send(zmq::buffer(serialized));
auto reply = socket.recv(...);
return parseAck(reply_bytes);
```

- [ ] **Step 4: 在 CMake 中注册客户端测试**

```cmake
list(APPEND GROUND_CONTROL_CORE_SOURCES
    src/zmq_command_client.cpp
)
add_ground_control_test(test_zmq_command_client tests/test_zmq_command_client.cpp)
```

- [ ] **Step 5: 构建并跑 Qt 测试**

Run: `ctest --test-dir /home/qwe/XT/build --output-on-failure -R "test_zmq_command_client|test_mission_plan_bridge"`
Expected: PASS

### Task 4: 在地面站主窗口接入任务下发确认

**Files:**
- Modify: `ground_control/src/main_window.h`
- Modify: `ground_control/src/main_window.cpp`
- Modify: `ground_control/tests/test_main_window.cpp`

- [ ] **Step 1: 增加命令客户端字段与目标 IP/端口设置**

```cpp
#include "zmq_command_client.h"

ZmqCommandClient command_client_{"tcp://127.0.0.1:5558"};
```

- [ ] **Step 2: 将 `applyMissionPlanResult(...)` 改成“本地更新 + 网络下发”两段式**

```cpp
void MainWindow::applyMissionPlanResult(const MissionPlanResult &result) {
    ... // 现有本地 scene/label 更新
    QString persist_error;
    if (!persistMissionPlan(result.plan, &persist_error)) {
        status_label_->setText(...);
        return;
    }
    const auto send_result = command_client_.sendMissionPlan(result.plan);
    if (!send_result.ok) {
        status_label_->setText(QString("错误: 任务已本地生成，但机载端未确认 | %1").arg(send_result.message));
        return;
    }
    status_label_->setText("状态: 任务已同步至机载端，可执行任务");
}
```

- [ ] **Step 3: 扩展 `test_main_window.cpp`，验证成功路径状态文案**

```cpp
// 使用测试替身或可注入客户端，让 sendMissionPlan 返回 ok=true
QCOMPARE(statusLabel->text(), QString("状态: 任务已同步至机载端，可执行任务"));
```

- [ ] **Step 4: 运行主窗口相关测试**

Run: `ctest --test-dir /home/qwe/XT/build --output-on-failure -R "test_main_window|test_grid_scene|test_planning_state_machine"`
Expected: PASS

### Task 5: 端到端双机伪联调验证

**Files:**
- Verify only

- [ ] **Step 1: 启动机载命令服务**

```bash
cd /home/qwe/XT
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator \
  --case shared/cases/sample_case.json \
  --mission-plan runtime/active_mission_plan.json \
  --command-endpoint tcp://127.0.0.1:5558 \
  --sleep-scale 0.02
```

- [ ] **Step 2: 启动地面站**

```bash
cd /home/qwe/XT
./build/ground_control/ground_control_app
```

- [ ] **Step 3: 在地面站中设置禁飞区并生成航线**

```text
1. 点击“设置禁飞区”
2. 选择三格直线禁飞区
3. 点击“航线生成”
4. 观察状态栏是否变为“任务已同步至机载端，可执行任务”
```

- [ ] **Step 4: 检查机载端 runtime 文件**

```bash
cd /home/qwe/XT
python3 - <<'PY'
import json
with open('runtime/active_mission_plan.json', 'r', encoding='utf-8') as f:
    data = json.load(f)
print(data['no_fly_cells'], data['terminal_cell'], len(data['route']))
PY
```

- [ ] **Step 5: 记录限制**

```text
当前联调用本机双进程模拟双 NUC；下一阶段再把 IP 从 127.0.0.1 切换为真实机载 NUC 地址。
```

## Self-Review

- **Spec coverage:** 覆盖了协议扩展、机载命令接收服务、地面站命令客户端、主窗口接线与端到端伪联调。
- **Placeholder scan:** 无 `TODO`/`TBD`/`implement later` 占位符。
- **Type consistency:** 统一使用 `MissionPlanData`、`MissionPlanResult`、`CommandSendResult`、`ZmqCommandClient`、`serve_command_endpoint` 这些命名。
