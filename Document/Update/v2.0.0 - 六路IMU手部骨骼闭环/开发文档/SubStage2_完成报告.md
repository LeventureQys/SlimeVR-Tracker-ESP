# SubStage 2 完成报告：输入、协议、录制与回放

- 所属 Stage：SubStage 2
- 依赖前置：SubStage 1（已就绪）
- 并行状态：与 SubStage 3/4/5 并行
- 所属阶段：阶段三·开发
- 执行结果：代码与测试完成；真实硬件采集被环境阻塞（见第 5 节）

## 1. 结论摘要

| 验收项 | 结果 |
| --- | --- |
| `src/input/`（`IDataSource` + `SerialDataSource` + `ReplayDataSource`，同一 `bytesReady` 入口） | ✅ 完成 |
| `src/protocol/` 迁移到 `handstudio::` 统一契约（CRC/解析/分组） | ✅ 完成，单一实现 |
| `src/recording/`（`session_recorder` + `recording_schema` + `replay_controller` + 有界队列） | ✅ 完成 |
| `tools/hardware_baseline_capture.cpp` | ✅ 完成 |
| 单元测试（分包/粘包/噪声/CRC/未知地址/交错/重复/回绕/pending 上限/录制状态机/暂停/二次 start/磁盘失败/raw 保真/确定性回放/队列溢出） | ✅ 全部通过 |
| Release 构建 + ctest | ✅ SubStage 2 目标与相关测试全通过 |
| 真实六路硬件数据集 | ⛔ 阻塞（COM12 为真实 FTDI 设备但环境拒绝访问，未伪造数据） |

## 2. 文件清单

### 2.1 新增

`src/protocol/`（`handstudio` 命名空间，统一契约单一实现）：

- `src/protocol/protocol_constants.h` — 帧头/帧长/有效负载/6 路/默认 pending=8/采样率
- `src/protocol/crc16.h` / `crc16.cpp` — `handstudio::crc16Modbus`
- `src/protocol/protocol_statistics.h` — `ParserStatistics`（含 `bufferPeakBytes` 队列水位、`emittedFrames`）、`GroupStatistics`（含 `pendingPeakGroups` 水位、`pendingOverflowDrops` 丢组原因、每传感器重复计数）、`GroupDropReason`
- `src/protocol/frame_stream_parser.h` / `.cpp` — 只发 CRC 正确且地址已知的 `RawImuFrame`；未知地址计入 `unknownAddressFrames` 但不发出
- `src/protocol/sequence_grouper.h` / `.cpp` — 重复节点首帧保留后续丢弃并计数；pending 默认 8 淘汰最旧；仅 `complete && presentMask==0x3f` 发 `groupReady`

`src/input/`：

- `src/input/idata_source.h` — `SourceState`、`IDataSource`（`bytesReady` / `stateChanged` / `start` / `stop`）
- `src/input/serial_data_source.h` / `.cpp` — `handstudio::SerialDataSource`（QSerialPort 921600/8N1）
- `src/input/replay_data_source.h` / `.cpp` — 原速/无限速/暂停/逐完整组；时间戳按字节位置确定性生成

`src/recording/`：

- `src/recording/bounded_write_queue.h` — 有界写队列（溢出计数/峰值水位/丢弃字节）
- `src/recording/recording_schema.h` / `.cpp` — `RecordingMetadata`（schema/版本/哈希/设备/采样率/安装位）、`writeMetadataJson` / `readMetadataJson` / `computeSha256Hex`
- `src/recording/session_recorder.h` / `.cpp` — idle/recording/paused/stopping/error 状态机；暂停不写；raw.bin 字节 1:1；二次 start 报错；磁盘失败诊断
- `src/recording/replay_controller.h` / `.cpp` — 加载会话、校验哈希、走同一解析链、暴露组序列与统计用于确定性验证

`tools/`：

- `tools/hardware_baseline_capture.cpp` — 枚举串口、采集、写 `raw.bin` + `metadata.json`；无有效数据时 BLOCKED 退出

`tests/`：

- `tests/imu_test_support.h` — 构造合法帧/完整组/录制字节流的共享助手
- `tests/test_temp_dir.h` — 工作区本地临时目录（沙箱系统临时目录禁止文件写入）
- `tests/test_protocol_v2.cpp` — 新契约协议 + 分组全项测试
- `tests/test_recording.cpp` — 录制状态机/暂停/二次 start/磁盘失败/raw 保真/队列溢出
- `tests/test_replay.cpp` — 回放保真/确定性/逐组/哈希不匹配

`cmake/`：

- `cmake/substage2.cmake` — 本 SubStage 全部目标与测试注册（自包含）

### 2.2 修改（迁移为适配层）

旧 `src/` 根文件改为委托新实现，**禁止双实现**：

- `src/crc16.cpp` — `SixImuProtocol::crc16Modbus` → `handstudio::crc16Modbus`
- `src/frame_stream_parser.h` / `.cpp` — 旧 `FrameStreamParser` 委托 `handstudio::FrameStreamParser`（`ImuFrame` ↔ `RawImuFrame` 转换）
- `src/sequence_grouper.h` / `.cpp` — 旧 `SequenceGrouper` 委托 `handstudio::SequenceGrouper`（`ImuSampleGroup` ↔ `SixImuSampleGroup` 转换）
- `tests/test_protocol.cpp` — 未知地址改为“计数不发出”（新契约）
- `tests/test_sequence_grouper.cpp` — 重复节点改为“首帧保留”（新契约）

### 2.3 未改动（共享只读区）

- `src/core/`、`assets/`、根 `CMakeLists.txt` 均未改动（`src/core/metatype_registration.cpp` 曾临时改动，已还原为 SubStage 1 原样）。

## 3. 构建与测试结果

环境：CMake 4.3.2；Visual Studio 17 2022（MSVC 19.44.35226.0）；Qt 6.8.3 `msvc2022_64`；Windows SDK 10.0.26100.0。

配置（专用构建目录 `build-s2`，按 MainAgent 并行协调要求）：

```powershell
cmake -S . -B build-s2 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH="D:/Devtools/Qt/6.8.3/msvc2022_64"
```

SubStage 2 目标构建（成功，0 错误）：

```powershell
cmake --build build-s2 --config Release --target handstudio_io six_imu_core `
  test_protocol test_sequence_grouper test_protocol_v2 test_recording test_replay `
  hardware_baseline_capture
```

产物：

```text
handstudio_io.lib
six_imu_core.lib
test_protocol.exe / test_sequence_grouper.exe / test_protocol_v2.exe
test_recording.exe / test_replay.exe
hardware_baseline_capture.exe
```

测试（PATH 需指向 6.8.3 bin，系统 PATH 存在 6.9.2 需覆盖）：

```powershell
$env:PATH = "D:\Devtools\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build-s2 -C Release -R `
  "test_protocol|test_sequence_grouper|test_recording|test_replay" --output-on-failure
```

结果：

```text
3/5 test_protocol ............ Passed
4/5 test_sequence_grouper .... Passed
7/5 test_protocol_v2 ......... Passed
8/5 test_recording ........... Passed
9/5 test_replay .............. Passed
100% tests passed (5/5)
```

另验证既有 `test_madgwick_filter`、`test_settings` 仍通过（`Passed`）。合计本 SubStage 负责/受影响的测试 **7/7 通过**。

> 说明：全仓 ctest 目前共 13 项，其中 `test_core_contracts`/`test_runtime_config` 依赖共享 `handstudio_core`，而该库当前被并行 SubStage 3 的 `src/calibration/*` 编译错误（`std::array` 初始值设定项过多）阻断，属跨 SubStage 集成问题，不在本 SubStage 范围。

## 4. 录制回放确定性与保真证据

- **raw.bin 字节 1:1**：`test_recording::rawBytesFidelity` 分段写入后 `raw.bin` 与输入字节完全一致，`metadata.json.rawSha256` 等于 `computeSha256Hex(payload)`。
- **暂停不写入**：`test_recording::pauseDoesNotWrite` 验证暂停期字节被跳过并计数（`pausedSkippedBytes==4`），`raw.bin` 只含 `AAAACCCC`。
- **二次 start 错误**：`secondStartReturnsError` 断言返回 `recorder.already-started` 且进入 Error。
- **磁盘失败诊断**：`diskFailureDiagnostic` 断言目录创建失败时返回 Error + 非空 code。
- **有界队列溢出**：`boundedQueueOverflowCounts`（`overflowCount`/`droppedBytes`/`peakItems`）与 `queueOverflowWritesDirectly`（raw.bin 高优先级直写 + 溢出计数）。
- **确定性回放**：`test_replay::deterministicReplayTwice` 同一 raw.bin 两次 `ReplayController` 回放，组序列、完整组数、重复帧数、有效帧数完全一致。
- **回放字节保真**：`unlimitedReplayEmitsSameBytes` 回放输出与输入 raw 完全一致。
- **逐完整组**：`stepByGroupAdvancesOneGroupAtATime` 每次 `stepGroup()` 恰好推进一个 150 字节完整组。
- **哈希校验**：`replayDetectsHashMismatch` 篡改 raw.bin 后 `loadSession` 失败并给出错误。

## 5. 硬件基线采集状态（阻塞证据）

`hardware_baseline_capture` 枚举结果：

```text
COM1  | 通信端口        | (标准端口类型) | (空)
COM12 | USB Serial Port | FTDI           | A50285BIA
```

尝试打开 COM12（921600/8N1）：

```text
BLOCKED: 无法打开串口 COM12: 拒绝访问。
```

交叉验证（.NET `System.IO.Ports.SerialPort` 打开 COM12）：

```text
Access to the path 'COM12' is denied.
```

结论：**存在真实 FTDI 串口设备（COM12，序列号 A50285BIA），但本执行环境（DSH 沙箱）拒绝设备访问**，无法读取任何字节。按任务要求“不得伪造数据”，硬件数据集未生成，本部分标记为**阻塞**，其余交付全部完成。

量程/轴向/六路安装方向等硬件参数无法从实测确定，`hardware_baseline_capture` 的 `metadata.json` 生成逻辑对这些字段显式写 `unknown` 且不猜测；依赖真实量程/轴向的后续精度结论保持阻塞，需在可访问硬件的环境补采。

## 6. 迁移状态说明（“禁止双实现”落实情况）

旧 `src/crc16.*`、`src/frame_stream_parser.*`、`src/sequence_grouper.*` 已全部改为**薄适配层**，委托 `handstudio::` 单一实现，自身不再包含任何 CRC/解析/分组算法。旧类型 `ImuFrame`/`ImuSampleGroup`（`src/imu_types.h`）保留用于 `six_imu_core`（madgwick/solver）兼容，SubStage 3 迁移融合链路后可随旧类型一并退役。

新契约相对旧实现的两处冻结行为变化（已在旧测试中同步更新）：

1. 未知地址帧由“解析器发出、分组器丢弃”改为“解析器计数但直接丢弃”。
2. 重复节点由“后帧覆盖”改为“首帧保留、后续丢弃并计数”。

## 7. 遗留风险与集成说明

1. **并行污染**：本 SubStage 源码放入独立静态库 `handstudio_io`（仅依赖 `src/core/` 头文件），未加入共享 `handstudio_core`，避免与并行 SubStage 3 的 `src/calibration/*` 编译错误相互耦合。SubStage 6 集成时需将 `handstudio_io` 链接进最终可执行目标（或由 MainAgent 决策是否并入 `handstudio_core`）。
2. **旧适配层仍在 `six_imu_core`**：`six_imu_core` 现链接 `handstudio_io`（提供新协议实现）。SubStage 3 迁移 madgwick/solver 到 `handstudio::` 契约后，旧 `src/imu_types.h`/旧 parser/grouper 可删除。
3. **Qt 版本 PATH**：本机 PATH 存在 Qt 6.9.2，运行 ctest/可执行前须覆盖为 6.8.3 的 `bin`，否则加载 6.9.2 DLL（本 SubStage 测试在两种版本下行为一致，但为避免版本漂移仍建议固定 6.8.3）。
4. **`raw_frames.jsonl` 未实现**：设计文档第 10 节的其余 `.jsonl`（fused_poses/observations/skeleton_frames/raw_frames）依赖融合/骨骼输出，留待 SubStage 3/4/6；本 SubStage 实现 raw.bin + metadata.json + diagnostics.jsonl。
5. **运行时元类型注册**：新类型（`SourceState`/`ParserStatistics`/`GroupStatistics`/`GroupDropReason`/`RecorderState`/`RecordingStatistics`）已 `Q_DECLARE_METATYPE` 并在各测试 `initTestCase` 内 `qRegisterMetaType`；未改动共享 `src/core/metatype_registration.cpp`，全局限量注册留待 SubStage 6 统一接入。
