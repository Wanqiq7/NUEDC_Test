# 原始相机图传设计

**日期：** 2026-07-22
**状态：** 已确认，进入实施
**涉及仓库：** `NUEDC_Test`、`Mid360_add_groundstation`

## 目标

在现有 H 题 Web 地面站增加一个独立、只读、按需启停的原始相机图传。操作员点击顶部摄像机按钮后，通过全屏对话框查看 C70 USB 相机的完整 `1280x720@30 FPS` 画面。视频不包含检测框、文字或其他视觉算法叠加。

图传是辅助观察通道，不参与任务决策。编码、RTSP、MediaMTX、WebRTC或浏览器播放器故障不得阻塞 RKNN、Point-LIO、ZeroMQ命令或遥测。

## 非目标

- 不录像、截图、回放或采集音频。
- 不提供处理图、掩码或原图/处理图切换。
- 不通过 Protobuf、ZeroMQ、ROS `sensor_msgs/Image` 或 FastAPI 转发视频负载。
- 本阶段不进行端到端性能验收；`P95 <= 250 ms` 仅保留为设计目标。
- 比赛模式不使用 CPU H.264 自动回退。

## 总体架构

```text
C70 UVC camera
    |
cam_node (single camera owner)
    +-- latest raw frame --> RKNN detection --> existing ROS 2 / ZeroMQ
    +-- latest raw frame --> Rockchip MPP H.264 --> embedded gst-rtsp-server
                                                   |
                                       RTSP control + RTP/UDP media
                                                   |
                                    ground MediaMTX sourceOnDemand
                                                   |
                                      FastAPI same-origin WHEP proxy
                                                   |
                                           Chrome WebRTC video
```

## 机载端

### 相机合同

- 设备：C70，全局曝光，UVC，USB 2.0。
- 稳定设备路径来自 `NUEDC_CAMERA_DEVICE`，比赛模式要求 `/dev/v4l/by-id/...`。
- 协商顺序固定为 `MJPEG 1280x720@30`，其次 `YUYV 1280x720@30`。
- 两种模式均不可用时，相机启动失败；不得静默降低分辨率或帧率。
- 捕获线程只保留最新帧，视觉和视频消费者不能反向阻塞捕获。

### 进程与线程边界

`cam_node`仍是唯一相机所有者，并在同一进程内组合三个职责明确的组件：

- `CameraCapture`：打开、协商、读取和重连 UVC 设备。
- 现有视觉处理：读取最新原始帧，执行 letterbox、RKNN和跟踪。
- `RawVideoServer`：读取同一份未画框帧，按需启动 H.264/RTSP媒体管线。

视频消费者在 `drawDetections()` 之前取得原始帧。图传使用独立有界最新帧缓冲；网络变慢时覆盖旧帧。

### RTSP服务

- 监听端口：`8554/TCP`。
- 路径：`/camera_raw`。
- 媒体：H.264 RTP/UDP，固定UDP端口范围由配置指定。
- 编码：Rockchip `mpph264enc`，`1280x720@30`，CBR `2 Mbps`，最大约 `2.5 Mbps`，GOP 30，无B帧，重复发送SPS/PPS。
- 编码和发送只在RTSP客户端读取时运行；最后一个客户端断开后释放媒体管线。
- 使用部署环境提供的只读用户名和密码。
- 热点负责RTSP/RTP链路加密，不额外启用RTSPS或VPN。

### 故障语义

- GStreamer、MPP或RTSP初始化失败：只禁用图传并记录错误，`cam_node`继续执行视觉任务。
- 比赛模式缺少MPP：不回退软件编码。
- 连续1秒无相机帧：相机标记离线，每1秒按稳定设备路径重试。
- 任务未运行时，相机恢复后视觉和图传自动恢复。
- 视觉任务运行期间相机失效：沿用任务协调器受控失败策略；恢复相机不得自动恢复已失败任务。

## 地面端

### MediaMTX

- 使用固定版本原生 `x86_64` 二进制和固定配置，不使用Docker。
- 由 `start_competition.sh`启动、检查、监督和清理。
- 路径名：`camera_raw`。
- 源：`rtsp://<credentials>@10.42.0.2:8554/camera_raw`。
- `sourceOnDemand`开启；没有浏览器读取时不连接机载RTSP。
- 最后一个读取者关闭后最多保留1秒，随后停止机载无线视频。
- 只做协议转发，不转码、不录像。

### FastAPI WHEP代理

浏览器只访问同源接口：

```text
POST   /api/video/whep
DELETE /api/video/whep/{session_id}
GET    /api/video/health
```

Gateway将SDP offer转发到本机MediaMTX，返回SDP answer和不透明会话ID。浏览器关闭或重建连接时删除会话。代理限制请求类型、SDP大小、超时和上游地址，不允许客户端提交任意URL。视频媒体包在浏览器和地面MediaMTX之间交换，不经过Python。

### Web界面

- 顶部状态栏“检测记录”旁增加 `videocam` 图标按钮和“查看图传”提示。
- 按钮独立于规划、LOAD、START、STOP、`mission_running`和`vision_armed`状态，始终可以打开。
- 对话框全屏，视频使用 `object-fit: contain`；完整画面优先，允许黑边，不裁剪。
- 状态为连接中、实时或图传中断；中断超过1秒后重建WHEP会话。
- 重连退避为250、500、1000、2000毫秒，保持2秒上限。
- 关闭对话框时停止重连、关闭`RTCPeerConnection`、删除WHEP会话并释放`video.srcObject`。
- 视频无音轨并静音，目标浏览器为地面站当前 Chrome `150.0.7871.128`。

界面沿用现有深色工业控制台，不新增页面、路由或装饰性卡片。视频对话框以画面为主体，只保留紧凑状态栏和关闭按钮。

## 网络隔离

系统只能使用现有单热点：地面 `10.42.0.1`，机载 `10.42.0.2`。

- 现有ZeroMQ端口 `5557/5558`保持不变并设置较高TOS/DSCP。
- RTP视频设置较低TOS/DSCP。
- 视频编码器强制码率上限，发送队列可丢弃。
- 不把需要root权限的`tc`规则作为比赛启动硬依赖。

## 配置

### 机载环境

```text
NUEDC_CAMERA_DEVICE=/dev/v4l/by-id/<c70-device>
NUEDC_VIDEO_RTSP_USER=<read-only-user>
NUEDC_VIDEO_RTSP_PASSWORD=<secret>
```

ROS参数提供视频开关、端口、路径、RTP端口范围、码率、GOP和开发编码器。比赛YAML固定启用MPP，不包含凭据。

### 地面环境

```text
NUEDC_MEDIAMTX_BIN=<absolute-path>
NUEDC_MEDIAMTX_CONFIG=<absolute-path>
NUEDC_MEDIAMTX_API_URL=http://127.0.0.1:<api-port>
NUEDC_MEDIAMTX_WHEP_URL=http://127.0.0.1:8889/camera_raw/whep
NUEDC_VIDEO_RTSP_USER=<read-only-user>
NUEDC_VIDEO_RTSP_PASSWORD=<secret>
```

凭据不提交到Git，不返回浏览器，不写入应用日志。

## 开发模式

开发配置可以显式选择 `videotestsrc + x264enc`，通过同一RTSP、MediaMTX、WHEP和Web播放器链路验证。比赛配置不允许自动选择合成源或软件编码。

## 日志

只记录连接状态、相机重连次数、编码器初始化结果、发送帧率、丢帧、码率、WHEP/WebRTC重连次数和错误类别。不得保存视频帧、截图、音频或RTSP密码。

## 首轮验收

1. H题页面摄像机按钮在 `1024x600`、`1366x768`和`1920x1080`可访问。
2. 点击后全屏显示完整、无叠加的C70画面。
3. 关闭后地面MediaMTX停止按需拉取，机载无线视频停止。
4. 中断RTSP或MediaMTX后页面显示中断并自动恢复。
5. 视频慢或断开不会阻塞命令、遥测、RKNN或任务线程。
6. MPP不可用时比赛模式只报告图传不可用，不启动CPU编码器。
7. 全链路不生成录像、截图或音频文件。

## 后续验收

使用LED和120/240 FPS外部相机进行玻璃到玻璃测量，至少30次采样验证端到端延迟 `P95 <= 250 ms`。该项不阻塞首轮交付。
