# 双 NUC 部署与联调操作手册

## 1. 目标

本文档用于指导以下完整流程：

1. 两台 Ubuntu NUC 的环境配置
2. 从 GitHub 拉取当前项目
3. 明确地面端与机载端各自应关注和保留的文件
4. 完成双机网络配置
5. 按正确顺序启动机载端与地面端
6. 完成任务下发、启动执行和遥测回传联调

本文档对应当前仓库结构：

```text
XT/
├─ ground_control/
├─ airborne/
├─ shared/
├─ runtime/
├─ docs/
├─ CMakeLists.txt
└─ README.md
```

---

## 2. 环境配置

### 2.1 两台 NUC 的系统要求

建议两台机器都满足：

- Ubuntu 20.04 / 22.04 / 24.04
- Python 3.10 或更高
- CMake 3.16 或更高
- g++ / clang++，支持 C++17
- Qt6（至少包含 `Core`、`Widgets`、`Sql`、`Test`）
- Protobuf 编译器与运行库
- ZeroMQ / cppzmq

### 2.2 推荐安装命令

若环境还未装齐，可先执行：

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  protobuf-compiler \
  libprotobuf-dev \
  libzmq3-dev \
  cppzmq-dev \
  python3 \
  python3-pip \
  qt6-base-dev \
  qt6-base-dev-tools
```

### 2.3 Python 依赖

当前仓库 Python 侧主要依赖：

- `protobuf`
- `pyzmq`

若未安装，可执行：

```bash
python3 -m pip install --user protobuf pyzmq
```

### 2.4 环境自检

可在两台机器分别检查：

```bash
python3 --version
cmake --version
protoc --version
pkg-config --modversion Qt6Core
```

---

## 3. 从 GitHub 拉取项目

### 3.1 首次克隆

在两台 NUC 上都执行：

```bash
git clone https://github.com/Wanqiq7/NUEDC_Test.git
cd NUEDC_Test
```

### 3.2 后续更新

后续更新代码时执行：

```bash
cd /path/to/NUEDC_Test
git pull
```

### 3.3 编译

在任一机器拉到最新代码后，执行：

```bash
cmake -S . -B build
cmake --build build
```

### 3.4 测试

Qt / C++ 测试：

```bash
ctest --test-dir build --output-on-failure
```

Python 测试：

```bash
python3 -m unittest discover -s airborne/tests -v
```

---

## 4. 拉取后应该如何取舍

## 4.1 推荐原则

**推荐两台 NUC 都保留完整仓库。**

原因：

- 共享协议 `shared/proto/` 两端都会用到
- 共享案例 `shared/cases/` 两端都会用到
- `runtime/` 是运行态目录，两端都会产生本地状态
- 后续调试时，完整仓库更方便查日志、跑测试、排错

也就是说，**不建议人为删除整块目录来“裁剪仓库”**。  
正确做法是：两边都保留完整仓库，但运行和维护时各自只关注自己的子系统目录。

---

## 4.2 地面端重点保留与关注的目录

地面端 NUC 主要关注：

- `ground_control/`
  - 地面站 Qt 代码
- `shared/`
  - 协议与样例
- `runtime/`
  - 当前任务计划与运行数据库
- `build/`
  - 地面站构建产物
- `docs/`
  - 说明文档

### 地面端核心文件

- `ground_control/src/main_window.cpp`
- `ground_control/src/main_window.h`
- `ground_control/src/zmq_command_client.cpp`
- `ground_control/src/network_config.cpp`
- `shared/proto/messages.proto`
- `runtime/active_mission_plan.json`
- `runtime/ground_control_results.db`

### 地面端不需要日常关注但建议保留

- `airborne/`

不建议删掉，因为：

- 地面端本地仍会通过 Python 生成任务计划
- 调试时可能需要直接运行 `export_mission_plan`

---

## 4.3 机载端重点保留与关注的目录

机载端 NUC 主要关注：

- `airborne/`
  - 机载端 Python 代码
- `shared/`
  - 协议与样例
- `runtime/`
  - 当前任务计划
- `build/`
  - 协议生成产物（如需要）

### 机载端核心文件

- `airborne/uav_testbed/run_simulator.py`
- `airborne/uav_testbed/command_server.py`
- `airborne/uav_testbed/publisher.py`
- `airborne/uav_testbed/simulator.py`
- `shared/proto/messages.proto`
- `runtime/active_mission_plan.json`

### 机载端不需要日常关注但建议保留

- `ground_control/`

不建议删掉，因为：

- 后续排查协议或联调行为时，完整仓库更容易对照
- 两端代码版本更容易保持一致

---

## 4.4 可以理解为“逻辑保留”而不是“物理删除”

换句话说：

- **地面端逻辑保留**：`ground_control/ + shared/ + runtime/`
- **机载端逻辑保留**：`airborne/ + shared/ + runtime/`

但在实际文件管理上：

- 两边都保留完整仓库
- 各自只修改自己负责的子目录

这是最稳妥的方式。

---

## 5. 双机网络配置

### 5.1 推荐网络结构

两台 NUC 在同一局域网，使用固定 IP。

示例：

- 地面端 NUC：`192.168.10.10`
- 机载端 NUC：`192.168.10.20`

### 5.2 端口规划

- `5557`：机载端 -> 地面端，遥测上报
- `5558`：地面端 -> 机载端，任务下发与控制命令

### 5.3 通信方向

机载端：

- `PUB bind tcp://0.0.0.0:5557`
- `REP bind tcp://0.0.0.0:5558`

地面端：

- `SUB connect tcp://192.168.10.20:5557`
- `REQ connect tcp://192.168.10.20:5558`

### 5.4 网络检查

在地面端机器执行：

```bash
ping 192.168.10.20
nc -vz 192.168.10.20 5557
nc -vz 192.168.10.20 5558
```

如果启用了防火墙：

```bash
sudo ufw allow 5557/tcp
sudo ufw allow 5558/tcp
```

---

## 6. 双机联调操作

## 6.1 机载端启动

在机载 NUC 上执行：

```bash
cd /path/to/NUEDC_Test
PYTHONPATH=airborne python3 -m uav_testbed.run_simulator \
  --case shared/cases/sample_case.json \
  --mission-plan runtime/active_mission_plan.json \
  --endpoint tcp://0.0.0.0:5557 \
  --command-endpoint tcp://0.0.0.0:5558 \
  --wait-for-start \
  --sleep-scale 0.02
```

这条命令会同时完成：

- 启动机载端命令接收服务
- 绑定遥测上报端口
- 等待地面端发送任务并发出 `START_MISSION`

---

## 6.2 地面端启动

在地面站 NUC 上执行：

```bash
cd /path/to/NUEDC_Test
export NUEDC_AIRBORNE_HOST=192.168.10.20
export NUEDC_TELEMETRY_PORT=5557
export NUEDC_COMMAND_PORT=5558
./build/ground_control/ground_control_app
```

启动后，地面站会：

- 自动尝试 `PING` 机载端
- 界面显示 `机载状态: 在线` 或 `机载状态: 离线`

---

## 6.3 任务下发流程

在地面端界面中：

1. 点击 `设置禁飞区`
2. 在左侧地图选择 3 个横向或纵向连续格子
3. 按钮变成 `航线生成`
4. 点击 `航线生成`

若成功，你应看到：

- 地面站本地航线刷新
- 状态栏提示：`任务已同步至机载端，可执行任务`
- `runtime/active_mission_plan.json` 更新
- `执行任务` 按钮可点击

---

## 6.4 执行与停止

在地面端界面中：

- 点击 `执行任务`
  - 地面站发送 `START_MISSION`
  - 机载端收到后开始执行

- 点击 `停止任务`
  - 地面站发送 `STOP_MISSION`
  - 机载端收到后停止当前执行流程

---

## 6.5 联调时你应该看到什么

正常情况下，你会看到：

### 地面端

- `机载状态: 在线`
- 任务下发成功后：`任务已同步至机载端，可执行任务`
- 点击执行后：`已发送开始执行命令`
- 红点沿新航线移动
- 检测结果持续追加

### 机载端

- 成功接收并写入 `runtime/active_mission_plan.json`
- 收到 `START_MISSION` 后开始推送遥测
- 收到 `STOP_MISSION` 后停止当前执行

---

## 7. 出问题时先看哪里

### 7.1 地面端显示机载离线

先检查：

```bash
ping 192.168.10.20
nc -vz 192.168.10.20 5558
```

### 7.2 任务生成成功但同步失败

先检查：

- 地面端环境变量是否设置正确
- 机载端 `--command-endpoint` 是否绑定 `0.0.0.0:5558`
- 两端防火墙是否放行

### 7.3 点击执行后机载端没反应

先检查：

- 机载端是否带了 `--wait-for-start`
- 是否已经先完成任务同步
- 地面端是否显示 `任务已同步至机载端，可执行任务`

### 7.4 遥测没有回传

先检查：

- 机载端 `--endpoint` 是否是 `tcp://0.0.0.0:5557`
- 地面端 `NUEDC_TELEMETRY_PORT` 是否正确
- `NUEDC_AIRBORNE_HOST` 是否指向机载端真实 IP

---

## 8. 推荐日常操作顺序

每次联调建议都按这个顺序：

1. 两边 `git pull`
2. 两边重新编译 / 至少确认协议一致
3. 机载端先启动
4. 地面端再启动
5. 先确认 `机载状态: 在线`
6. 再设置禁飞区并生成航线
7. 再点击 `执行任务`

---

## 9. 建议

短期建议：

- 两台机器都保留完整仓库
- 每次只改各自负责目录
- 联调前务必先 `git pull`

中期建议：

- 把 `NUEDC_AIRBORNE_HOST` 等环境变量写成启动脚本
- 后续补一个更明确的“执行状态栏/指示灯”
- 若比赛前还要增强稳定性，再加心跳超时和重试机制
