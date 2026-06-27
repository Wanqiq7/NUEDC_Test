# Similar Risk Audit Plan

> **For reviewers:** 只读审查计划。不要修改文件，不要格式化，不要回退当前工作区已有改动。

**Goal:** 检查当前工程在分布式、多线程、Qt、ZMQ、图形渲染和构建链路中是否还存在与前次问题相似的隐患。

**Scope:** 当前工作区所有源码、测试、CMake、Protobuf/ROS bridge 相关文件；重点看新增改动后的集成风险。

**Review Tracks:**

1. **Qt threading and meta-type safety**
   - 检查跨线程 signal/slot 的参数类型是否 `Q_DECLARE_METATYPE` + `qRegisterMetaType`。
   - 检查 QObject/QThread 生命周期、UI 线程访问、数据库对象线程亲和性。
   - 检查共享状态是否仍有普通字段跨线程读写。

2. **ZMQ protocol and distributed state consistency**
   - 检查 socket 是否跨线程使用。
   - 检查 sequence/stale 判定是否覆盖 mission_load/start/stop。
   - 检查 Ack 状态回传是否被地面站真正消费。
   - 检查重试、Ack 丢包、进程重启后的状态一致性风险。

3. **Graphics and UI runtime performance**
   - 检查 QGraphicsScene 动态 item 更新是否仍存在重建/clear 热路径。
   - 检查检测列表、汇总表、数据库写入是否在高频事件下阻塞 UI。
   - 检查 route/candidate overlays 是否有对象泄露或不必要重绘。

4. **Build, generated code, and test reliability**
   - 检查 `shared/proto/messages.proto` 与 ROS bridge proto 是否漂移。
   - 检查 CMake target sources 是否重复/缺失。
   - 检查测试是否实际覆盖新增逻辑，是否可能运行旧二进制。
   - 检查当前构建链路的 `moc.exe 0xc0000135` 问题是否有可定位原因。

**Output Required From Each Reviewer:**
- Findings first, by severity.
- Every finding must include file path and line number.
- Separate confirmed issues from residual risks.
- If no issues in a track, state that clearly and list remaining test gaps.
