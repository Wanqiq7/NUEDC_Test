# 地面热点和机载自动连接设计

## 目标

将双机网络拓扑固定为“地面站提供 Wi-Fi 热点，机载端作为客户端自动连接”。地面站和机载端在上电后由 NetworkManager 恢复各自的连接配置，使地面站能稳定连接机载端 ZMQ 服务。

## 固定网络契约

| 项目 | 默认值 |
| --- | --- |
| 热点连接名 | `NUEDC-Ground-Hotspot` |
| 机载客户端连接名 | `NUEDC-Ground-Client` |
| SSID | `NUEDC-Ground` |
| 地面站热点地址 | `10.42.0.1/24` |
| 机载端地址 | `10.42.0.2/24` |
| 遥测端口 | `5557` |
| 命令端口 | `5558` |

密码由调用方通过参数或环境变量提供，只保存到 NetworkManager 的连接配置，不写入项目文件或生成的环境文件。

## 地面站脚本

新增 `scripts/start_ground_hotspot.sh`。它接受 `--iface`、`--ssid`、`--password`、`--ground-host`、`--airborne-host` 和 `--launch-app` 参数，也支持对应的 `NUEDC_*` 环境变量。

脚本应：

1. 验证 `nmcli` 可用并自动检测 Wi-Fi 网卡；检测失败时要求传入 `--iface`。
2. 创建或更新具名的 `NUEDC-Ground-Hotspot` 连接，将其设为 AP 模式、WPA-PSK 认证和 IPv4 `shared` 模式。
3. 将热点 IPv4 地址固定为 `10.42.0.1/24`，并启用连接与自动连接。
4. 激活热点连接，检查网卡已处于连接状态且具有指定地址。
5. 写入 `runtime/ground_control_network.env`，导出机载地址和 5557/5558 端口。
6. 当传入 `--launch-app` 时加载该环境文件并执行地面站程序。

脚本必须幂等：再次运行更新同一 NetworkManager 连接而非创建新配置。既有 `setup_ground_control_network.sh` 保持原语义，不做修改。

## 机载脚本

新增 `src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh`。它接受 `--iface`、`--ssid`、`--password`、`--ground-host` 和 `--airborne-host` 参数，也支持对应的 `NUEDC_*` 环境变量。

脚本应：

1. 验证 `nmcli` 可用并检测或使用指定 Wi-Fi 网卡。
2. 创建或更新具名的 `NUEDC-Ground-Client` Wi-Fi 客户端连接，匹配目标 SSID 和 WPA-PSK。
3. 配置 IPv4 `manual`，地址为 `10.42.0.2/24`，网关为 `10.42.0.1`，且不为此 Wi-Fi 连接声明默认路由。
4. 配置 `connection.autoconnect=yes` 和无限自动连接重试，使设备上电后持续恢复该连接。
5. 激活连接，验证网卡获取配置的静态地址，并 ping 地面站热点一次。

该脚本只管理网络，不启动 ROS2。已有 `start_airborne_hardware.sh` 继续负责启动机载 ROS2 栈。

## 错误处理

两个脚本使用 `set -euo pipefail`，在缺少 `nmcli`、未检测到 Wi-Fi 网卡、缺少密码、无法激活连接或地址校验失败时退出非零并给出可操作的中文错误。密码不会打印到终端。

## 测试和验证

在各脚本所属仓库添加 shell 测试，通过伪造的 `nmcli`、`ip` 和 `ping` 命令验证：默认参数、显式参数覆盖、连接创建/更新、静态 IPv4、自动连接属性、环境文件内容与失败路径。实现后运行 Bash 语法检查、测试脚本、两个脚本的 `--help`，并检查生成的 diff。
