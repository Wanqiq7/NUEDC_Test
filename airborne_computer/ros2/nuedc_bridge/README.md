# nuedc_bridge

`nuedc_bridge` 是独立 ROS2 C++ package，用于把 NUEDC 地面站的 ZMQ/Protobuf 任务通信接入机载 ROS2 栈。它不修改 `livox_ros_driver2`、`Point-LIO` 或视觉包，后续可以直接复制或软链接到雷达工作空间的 `src/` 下。

## 功能

- 订阅 ROS2 `nav_msgs/msg/Odometry`，转换为地面站可识别的通用 `TaskEvent` 遥测消息。
- 订阅 ROS2 `vision_msgs/msg/Detection2DArray`，转换为地面站可识别的通用 `TaskEvent` 识别消息。
- 通过 ZMQ PUB 发布遥测，默认端口 `tcp://0.0.0.0:5557`。
- 通过 ZMQ REP 接收地面站命令，默认端口 `tcp://0.0.0.0:5558`。
- 接收 `mission_load` 后写入通用任务计划 JSON，默认路径 `runtime/active_mission_plan.json`。

## 构建

在本仓库中构建：

```bash
cd /home/sb/NUEDC_Test/airborne_computer/ros2
source /opt/ros/humble/setup.bash
colcon build --packages-select nuedc_bridge
colcon test --packages-select nuedc_bridge
```

放入雷达工作空间时，推荐软链接：

```bash
cd /home/sb/livox_360
ln -s /home/sb/NUEDC_Test/airborne_computer/ros2/nuedc_bridge src/nuedc_bridge
source /opt/ros/humble/setup.bash
colcon build --packages-select nuedc_bridge
```

如果比赛现场不想依赖本仓库路径，也可以复制整个 `nuedc_bridge/` 目录到 Livox 工作空间 `src/` 下。

## 启动

单独启动：

```bash
source /home/sb/livox_360/install/setup.bash
ros2 launch nuedc_bridge nuedc_bridge.launch.py \
  odometry_topic:=Odometry \
  detections_topic:=detections \
  mission_plan_path:=/home/sb/livox_360/runtime/active_mission_plan.json
```

和 `Point-LIO` 一起启动时，在现有 `point_lio.launch.py` 中增加一个 `Node`：

```python
from launch_ros.actions import Node

Node(
    package="nuedc_bridge",
    executable="nuedc_bridge_node",
    name="nuedc_bridge_node",
    output="screen",
    parameters=[{
        "odometry_topic": "Odometry",
        "detections_topic": "detections",
        "telemetry_endpoint": "tcp://0.0.0.0:5557",
        "command_endpoint": "tcp://0.0.0.0:5558",
        "mission_plan_path": "/home/sb/livox_360/runtime/active_mission_plan.json",
    }],
)
```

## 参数

- `telemetry_endpoint`：ZMQ PUB 绑定地址，默认 `tcp://0.0.0.0:5557`。
- `command_endpoint`：ZMQ REP 绑定地址，默认 `tcp://0.0.0.0:5558`。
- `mission_plan_path`：接收地面站 `mission_load` 后写入的通用任务计划 JSON。
- `task_id`：未收到任务前的默认任务 ID。
- `odometry_topic`：里程计 topic，默认 `Odometry`。
- `detections_topic`：视觉检测 topic，默认 `detections`。

## 依赖

需要 ROS2 Humble、Protobuf、ZeroMQ 和 cppzmq：

```bash
sudo apt install ros-humble-vision-msgs libprotobuf-dev protobuf-compiler libzmq3-dev cppzmq-dev
```
