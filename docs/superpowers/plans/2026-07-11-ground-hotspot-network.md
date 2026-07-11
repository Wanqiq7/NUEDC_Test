# 地面热点与机载自动连接 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 提供可重复执行的地面站 Wi-Fi 热点脚本与机载端自动连接脚本，使两端在上电后使用固定地址建立地面站通信链路。

**Architecture:** 两个脚本仅使用 NetworkManager 的 `nmcli` 管理各自具名连接。地面站脚本配置 AP 与共享 IPv4 地址 `10.42.0.1/24`，并输出地面站应用使用的环境文件；机载脚本配置静态客户端地址 `10.42.0.2/24` 并开启无限次自动重连。每个脚本由同仓库的 Bash 测试以伪造 `nmcli`、`ip` 和 `ping` 隔离验证。

**Tech Stack:** Bash, NetworkManager/nmcli, POSIX shell utilities, existing Qt/ROS2 launch scripts.

## Global Constraints

- 热点连接名固定为 `NUEDC-Ground-Hotspot`，机载客户端连接名固定为 `NUEDC-Ground-Client`。
- 默认 SSID 为 `NUEDC-Ground`，默认 IPv4 网段为 `10.42.0.0/24`，地面和机载地址分别为 `10.42.0.1` 与 `10.42.0.2`。
- 密码只能来自 `--password` 或 `NUEDC_HOTSPOT_PASSWORD`，不得写入项目文件、运行环境文件或日志。
- 脚本必须幂等，不创建重复的 NetworkManager 连接。
- 不修改 `scripts/setup_ground_control_network.sh`，它继续支持旧的“地面站连接机载热点”拓扑。
- 网络脚本不启动 ROS2；`start_airborne_hardware.sh` 保持机载 ROS2 启动职责。
- 自动化测试不得执行真实的 NetworkManager、IP 或 Ping 命令。

---

### Task 1: 地面热点脚本的测试和实现

**Files:**
- Create: `NUEDC_Test/scripts/tests/test_start_ground_hotspot.sh`
- Create: `NUEDC_Test/scripts/start_ground_hotspot.sh`

**Interfaces:**
- Consumes: `nmcli`, `ip`, `ping`; optional `NUEDC_WIFI_IFACE`, `NUEDC_HOTSPOT_SSID`, `NUEDC_HOTSPOT_PASSWORD`, `NUEDC_GROUND_HOST`, `NUEDC_AIRBORNE_HOST`, `NUEDC_TELEMETRY_PORT`, `NUEDC_COMMAND_PORT`.
- Produces: active NetworkManager connection `NUEDC-Ground-Hotspot`; `runtime/ground_control_network.env` with `NUEDC_AIRBORNE_HOST`, `NUEDC_TELEMETRY_PORT`, and `NUEDC_COMMAND_PORT` exports.

- [x] **Step 1: Write the failing shell test**

Create `scripts/tests/test_start_ground_hotspot.sh`. It creates a temporary `bin/` directory containing fake `nmcli` and `ip` executables that append their arguments to `CALL_LOG`; invoke the not-yet-created target with `PATH="$fake_bin:$PATH"`, `NUEDC_WIFI_IFACE=wlan0`, `NUEDC_HOTSPOT_PASSWORD=12345678`, and a temporary runtime directory override. Assert that the invocation fails because `scripts/start_ground_hotspot.sh` does not exist.

The test's expected command assertions are:

```bash
assert_log_contains 'connection add type wifi ifname wlan0 con-name NUEDC-Ground-Hotspot autoconnect yes ssid NUEDC-Ground'
assert_log_contains 'connection modify NUEDC-Ground-Hotspot 802-11-wireless.mode ap'
assert_log_contains 'connection modify NUEDC-Ground-Hotspot ipv4.method shared ipv4.addresses 10.42.0.1/24'
assert_log_contains 'connection modify NUEDC-Ground-Hotspot wifi-sec.key-mgmt wpa-psk'
assert_log_contains 'connection modify NUEDC-Ground-Hotspot wifi-sec.psk 12345678'
assert_log_contains 'connection up NUEDC-Ground-Hotspot ifname wlan0'
```

The fake `nmcli -t -f NAME connection show` returns no matching connection so the test covers the create path. The fake `ip -4 -o addr show dev wlan0` returns `inet 10.42.0.1/24`; assert the generated environment file contains `export NUEDC_AIRBORNE_HOST=10.42.0.2`, `export NUEDC_TELEMETRY_PORT=5557`, and `export NUEDC_COMMAND_PORT=5558`.

- [x] **Step 2: Run the test to verify it fails**

Run:

```bash
bash scripts/tests/test_start_ground_hotspot.sh
```

Expected: nonzero exit because `scripts/start_ground_hotspot.sh` is absent.

- [x] **Step 3: Write the minimal ground hotspot implementation**

Create `scripts/start_ground_hotspot.sh` with this public interface:

```text
scripts/start_ground_hotspot.sh [--iface WLAN_IFACE] [--ssid SSID] [--password PASSWORD]
                                [--ground-host IPV4/CIDR] [--airborne-host IPV4]
                                [--launch-app]
```

Implement `require_command`, `detect_wifi_iface`, `connection_exists`, `configure_connection`, `verify_address`, and `usage`. Use `nmcli -t -f NAME connection show` to select `connection add` or only `connection modify`; always set AP mode, WPA-PSK, IPv4 `shared`, the fixed CIDR, autoconnect, and activate with `connection up`. Use `NUEDC_RUNTIME_DIR` only as a testable override, defaulting to `${ROOT_DIR}/runtime`; write the environment file via a temporary file then `mv` it atomically. Validate password length is at least eight characters and reject values containing newlines. Do not echo the password. `--launch-app` sources the generated environment file and `exec`s the existing ground station binary.

- [x] **Step 4: Run the focused test to verify it passes**

Run:

```bash
bash scripts/tests/test_start_ground_hotspot.sh
bash -n scripts/start_ground_hotspot.sh scripts/tests/test_start_ground_hotspot.sh
```

Expected: zero exit; the fake command log contains all expected NetworkManager operations and the environment file contains the three expected exports.

- [x] **Step 5: Commit the tested ground script**

```bash
git add scripts/start_ground_hotspot.sh scripts/tests/test_start_ground_hotspot.sh
git commit -m "feat: add ground hotspot setup script"
```

### Task 2: 机载自动连接脚本的测试和实现

**Files:**
- Create: `point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/scripts/tests/test_connect_ground_hotspot.sh`
- Create: `point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh`

**Interfaces:**
- Consumes: `nmcli`, `ip`, `ping`; optional `NUEDC_WIFI_IFACE`, `NUEDC_HOTSPOT_SSID`, `NUEDC_HOTSPOT_PASSWORD`, `NUEDC_GROUND_HOST`, and `NUEDC_AIRBORNE_HOST`.
- Produces: active NetworkManager client connection `NUEDC-Ground-Client` that automatically reconnects to the configured SSID with the declared static address.

- [x] **Step 1: Write the failing shell test**

Create `scripts/tests/test_connect_ground_hotspot.sh` using a temporary fake command directory and `CALL_LOG`. The fake `nmcli -t -f NAME connection show` reports no matching connection; fake `ip -4 -o addr show dev wlan0` returns `inet 10.42.0.2/24`; fake `ping` returns zero. Invoke the absent script with `NUEDC_WIFI_IFACE=wlan0` and `NUEDC_HOTSPOT_PASSWORD=12345678` and assert it fails.

After the script exists, the test must assert these calls:

```bash
assert_log_contains 'connection add type wifi ifname wlan0 con-name NUEDC-Ground-Client autoconnect yes ssid NUEDC-Ground'
assert_log_contains 'connection modify NUEDC-Ground-Client 802-11-wireless.mode infrastructure'
assert_log_contains 'connection modify NUEDC-Ground-Client wifi-sec.key-mgmt wpa-psk'
assert_log_contains 'connection modify NUEDC-Ground-Client wifi-sec.psk 12345678'
assert_log_contains 'connection modify NUEDC-Ground-Client ipv4.method manual ipv4.addresses 10.42.0.2/24 ipv4.gateway 10.42.0.1 ipv4.never-default yes'
assert_log_contains 'connection modify NUEDC-Ground-Client connection.autoconnect yes connection.autoconnect-retries 0'
assert_log_contains 'connection up NUEDC-Ground-Client ifname wlan0'
assert_log_contains 'ping -c 1 -W 2 10.42.0.1'
```

- [x] **Step 2: Run the test to verify it fails**

Run:

```bash
bash src/nuedc_airborne/airborne_bringup/scripts/tests/test_connect_ground_hotspot.sh
```

Expected: nonzero exit because `connect_ground_hotspot.sh` is absent.

- [x] **Step 3: Write the minimal airborne client implementation**

Create `connect_ground_hotspot.sh` with this public interface:

```text
connect_ground_hotspot.sh [--iface WLAN_IFACE] [--ssid SSID] [--password PASSWORD]
                           [--ground-host IPV4] [--airborne-host IPV4/CIDR]
```

Reuse the same bounded helpers as Task 1. Create or update only `NUEDC-Ground-Client`, configure client/infrastructure mode, WPA-PSK, `ipv4.method manual`, static address, gateway, `ipv4.never-default yes`, `connection.autoconnect yes`, and `connection.autoconnect-retries 0`. Activate the connection, confirm the specified address is assigned to the requested interface, then run one bounded `ping -c 1 -W 2` to the ground host. Print connection name, SSID, Wi-Fi interface, ground IP and airborne IP; never print the password. Keep all values ASCII and reject password newlines and passwords shorter than eight characters.

- [x] **Step 4: Run the focused test to verify it passes**

Run:

```bash
bash src/nuedc_airborne/airborne_bringup/scripts/tests/test_connect_ground_hotspot.sh
bash -n src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh \
  src/nuedc_airborne/airborne_bringup/scripts/tests/test_connect_ground_hotspot.sh
```

Expected: zero exit; the fake command log confirms connection creation, static addressing, auto-retry, activation, address validation and reachability check.

- [x] **Step 5: Commit the tested airborne script**

```bash
git add src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh \
  src/nuedc_airborne/airborne_bringup/scripts/tests/test_connect_ground_hotspot.sh
git commit -m "airborne: add ground hotspot auto-connect script"
```

### Task 3: 双机操作文档

**Files:**
- Modify: `NUEDC_Test/docs/dual_nuc_setup_guide.md:70-108`
- Modify: `point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/README.md:5-24`

**Interfaces:**
- Consumes: the exact script interfaces produced by Tasks 1 and 2.
- Produces: a deployment sequence that configures the ground station before the airborne device and retains the legacy hotspot topology as a separately named compatibility path.

- [x] **Step 1: Write documentation acceptance checks**

Before editing, run these checks and record the expected failure because the new command names are not yet documented:

```bash
rg -n 'start_ground_hotspot\.sh|connect_ground_hotspot\.sh|10\.42\.0\.1|10\.42\.0\.2' \
  docs/dual_nuc_setup_guide.md \
  ../point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/README.md
```

Expected: no matches for both new script names.

- [x] **Step 2: Update the ground-station guide**

Replace the primary network setup section with an ordered deployment sequence:

```bash
# Ground station, once per configuration or after a Wi-Fi adapter change.
./scripts/start_ground_hotspot.sh --iface wlan0 --ssid NUEDC-Ground --password '12345678'

# Airborne computer, once per configuration or after a Wi-Fi adapter change.
./src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh \
  --iface wlan0 --ssid NUEDC-Ground --password '12345678'
```

Document automatic recovery after later power cycles, the fixed `.1`/`.2` addresses, the `source runtime/ground_control_network.env` and `check_ground_control_network.sh` commands, and that the existing `setup_ground_control_network.sh` is only for the legacy reverse topology.

- [x] **Step 3: Update the airborne bringup README**

Add a "Network Setup" section before hardware mode that provides the client command from Step 2, states that it should be run after the ground hotspot is configured, lists the expected ground/airborne addresses, and explicitly says it does not launch ROS2. Preserve hardware and simulation invocation instructions unchanged.

- [x] **Step 4: Verify the documentation and all focused tests**

Run:

```bash
rg -n 'start_ground_hotspot\.sh|connect_ground_hotspot\.sh|10\.42\.0\.1|10\.42\.0\.2|旧.*拓扑' \
  docs/dual_nuc_setup_guide.md \
  ../point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/README.md
bash scripts/tests/test_start_ground_hotspot.sh
bash ../point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/scripts/tests/test_connect_ground_hotspot.sh
```

Expected: documentation contains both script names and addresses; both tests exit zero.

- [x] **Step 5: Commit the documentation**

```bash
git add docs/dual_nuc_setup_guide.md
git commit -m "docs: document ground hotspot deployment"
git -C ../point_lio_mid360_ros2 add src/nuedc_airborne/airborne_bringup/README.md
git -C ../point_lio_mid360_ros2 commit -m "docs: document airborne hotspot client"
```

### Task 4: 端到端静态验证

**Files:**
- Verify only: `NUEDC_Test/scripts/start_ground_hotspot.sh`
- Verify only: `point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh`
- Verify only: `NUEDC_Test/docs/dual_nuc_setup_guide.md`
- Verify only: `point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/README.md`

**Interfaces:**
- Consumes: all artifacts from Tasks 1-3.
- Produces: evidence that the scripts are syntactically valid, tests are isolated from real hardware, help text exposes the documented interfaces, and repository diffs contain only intended changes.

- [x] **Step 1: Run all script tests and syntax checks**

```bash
bash scripts/tests/test_start_ground_hotspot.sh
bash ../point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/scripts/tests/test_connect_ground_hotspot.sh
bash -n scripts/start_ground_hotspot.sh \
  ../point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh
```

Expected: all commands exit zero.

- [x] **Step 2: Check the public command interfaces without touching NetworkManager**

```bash
scripts/start_ground_hotspot.sh --help
../point_lio_mid360_ros2/src/nuedc_airborne/airborne_bringup/scripts/connect_ground_hotspot.sh --help
```

Expected: each command exits zero and lists all documented options.

- [x] **Step 3: Inspect scope and commit state**

```bash
git diff --check
git status --short
git -C ../point_lio_mid360_ros2 diff --check
git -C ../point_lio_mid360_ros2 status --short
```

Expected: no whitespace errors; only the script, test, and documentation changes created by this plan are present. Do not revert pre-existing untracked files.
