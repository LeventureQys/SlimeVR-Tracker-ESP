# SubStage 1 完成报告：工程基座与统一契约

- 所属 Stage：SubStage 1
- 依赖前置：无
- 并行状态：独立可执行
- 所属阶段：阶段三·开发
- 执行结果：完成（全部验收标准达成）

## 1. 结论摘要

| 验收项 | 结果 |
| --- | --- |
| C++20 工程基座 + `handstudio_core` 静态库 + `HandSkeletonStudio` 可执行目标 | ✅ 达成 |
| `src/core/` 与 `src/config/` 契约符合设计文档第 7 节 | ✅ 逐字段核对合规 |
| Release 构建通过 | ✅ 0 错误 0 警告（无新增） |
| 新增 + 受影响的既有 QtTest 全部通过 | ✅ 6/6 通过 |
| 产品链接图不含 `six_imu_network` | ✅ 证据见第 5 节 |
| 构建全程无 Python 探测/调用 | ✅ 证据见第 6 节 |
| `test_main_window` 从默认 CTest 摘除 | ✅ 已摘除并在报告说明 |

本次执行对象为被中断的前执行者留下的半成品。审查结论：**半成品已完整且契约合规，无需补写源代码**；本次实际完成的是逐文件契约核对、干净构建目录重建、Release 构建、CTest 执行、链接图与 Python 依赖验证，以及本完成报告的产出。

## 2. 文件清单

### 2.1 新增/修改文件（半成品，本次已逐文件审查并构建验证）

根 `CMakeLists.txt`（工程改名 `HandSkeletonStudio`、C++17→C++20、`AUTOMOC OFF` 手动 moc、新目标）：

- `CMakeLists.txt`

`src/core/` 统一契约（命名空间 `handstudio`，四元数 Hamilton wxyz，矩阵 `QMatrix4x4`，时间 `qint64` 单调纳秒）：

- `src/core/sensor_id.h` — `SensorId`（0x50..0x55）、地址/索引双向安全转换、`AllSensorIds`
- `src/core/imu_frames.h` — `RawImuFrame`、`SixImuSampleGroup`
- `src/core/calibrated_types.h` — `CalibratedImuSample`
- `src/core/fusion_types.h` — `FusionMode`/`MagneticHealth`/`CalibrationState`、`FusedImuPose`
- `src/core/hand_observation_types.h` — `HandSide`、`FingerObservation`、`HandObservationFrame`
- `src/core/hand_skeleton_frame.h` — `BoneSource`、`HandBoneFrame`、`HandSkeletonFrame`
- `src/core/diagnostic.h` — `DiagnosticSeverity`、`Diagnostic { severity, code, message, detail, timestampNs }`
- `src/core/schema_version.h` — `RuntimeConfigSchemaVersion=1`、`RecordingSchemaVersion=1`、`ApplicationVersion="2.0.0"`
- `src/core/metatype_registration.h/.cpp` — 集中 `qRegisterMetaType`

`src/config/` 版本化 JSON 配置骨架：

- `src/config/runtime_config.h/.cpp` — `loadRuntimeConfig`/`loadRuntimeConfigFile`，结构化 `Diagnostic` 返回

`src/app/`：

- `src/app/main.cpp` — 最小可执行入口，打印应用名、版本与 schema

`tests/`：

- `tests/test_core_contracts.cpp`
- `tests/test_runtime_config.cpp`

### 2.2 本次未修改的源代码

审查结论为半成品已满足全部契约与构建要求，本次未对任何 `.h/.cpp/CMakeLists.txt` 做内容改动；仅新增了 `build-v2/` 构建产物目录与本完成报告。

## 3. 构建命令与结果

环境：CMake 4.3.2；生成器 Visual Studio 17 2022（MSVC 19.44.35226.0）；Qt 6.8.3 `msvc2022_64`；Windows SDK 10.0.26100.0。

沿用旧 `build/` 目录存在陈旧状态（含旧工程名 `SixImuSolverQt`、Assimp `_deps`），故按设计文档第 14 节新建干净 `build-v2/`。

配置命令（工程根 `Document/Research/手套IMU解算MVP`）：

```powershell
cmake -S . -B build-v2 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH="D:/Devtools/Qt/6.8.3/msvc2022_64"
```

配置输出摘要：

```text
-- Selecting Windows SDK version 10.0.26100.0 to target Windows 10.0.26200.
-- The CXX compiler identification is MSVC 19.44.35226.0
-- Configuring done (12.9s)
-- Generating done (0.3s)
-- Build files have been written to: .../手套IMU解算MVP/build-v2
```

未设置 `HAND_SKELETON_FETCH_ASSIMP`（SubStage 1 不需要 Assimp，未触发网络下载）。

构建命令：

```powershell
cmake --build build-v2 --config Release
```

构建结果（exit code 0，无错误）：

```text
handstudio_core.vcxproj  -> .../build-v2/Release/handstudio_core.lib
HandSkeletonStudio.vcxproj -> .../build-v2/Release/HandSkeletonStudio.exe
six_imu_core.vcxproj     -> .../build-v2/Release/six_imu_core.lib
test_core_contracts.vcxproj  -> .../Release/test_core_contracts.exe
test_madgwick_filter.vcxproj -> .../Release/test_madgwick_filter.exe
test_protocol.vcxproj    -> .../Release/test_protocol.exe
test_runtime_config.vcxproj -> .../Release/test_runtime_config.exe
test_sequence_grouper.vcxproj -> .../Release/test_sequence_grouper.exe
test_settings.vcxproj    -> .../Release/test_settings.exe
```

`six_imu_network` 因 `EXCLUDE_FROM_ALL` 未参与默认构建（仅作为遗留独立目标保留）。

## 4. CTest 结果

命令：

```powershell
ctest --test-dir build-v2 -C Release --output-on-failure
```

结果：

```text
1/6 Test #1: test_core_contracts ......   Passed    0.16 sec
2/6 Test #2: test_runtime_config ......   Passed    0.06 sec
3/6 Test #3: test_protocol ............   Passed    0.06 sec
4/6 Test #4: test_sequence_grouper ....   Passed    0.05 sec
5/6 Test #5: test_madgwick_filter .....   Passed    0.07 sec
6/6 Test #6: test_settings ............   Passed    0.07 sec

100% tests passed, 0 tests failed out of 6
```

启动测试（临时最小 main）：

```powershell
& .../build-v2/Release/HandSkeletonStudio.exe
# 输出：HandSkeletonStudio 2.0.0 runtime-config-schema=1 recording-schema=1
# 退出码 0，无崩溃
```

## 5. 网络剥离证据（`six_imu_network` 不在产品链接图）

基于 `build-v2/` 生成的 `.vcxproj` 与链接依赖交叉验证：

1. `HandSkeletonStudio.vcxproj` 的 `ProjectReference` 仅包含 `handstudio_core.vcxproj`（外加自动生成的 `ZERO_CHECK.vcxproj`），不含 `six_imu_network.vcxproj`。
2. `HandSkeletonStudio.vcxproj` Release 配置 `AdditionalDependencies` 为 `Release\handstudio_core.lib` + `Qt6Gui.lib` + `Qt6Core.lib` + 系统库，无 `Qt6Network.lib`，无 `six_imu_network.lib`。
3. 全量扫描 `build-v2/*.vcxproj`：**无任何目标**通过 `ProjectReference` 引用 `six_imu_network.vcxproj`，**无任何目标**在 `AdditionalDependencies` 出现 `Qt6Network.lib` 或 `six_imu_network.lib`。
4. `handstudio_core` 为静态库，无自身链接依赖；其 `PUBLIC` 传递 `Qt6::Core`、`Qt6::Gui`，故 `HandSkeletonStudio` 仅继承 Core/Gui。
5. `six_imu_network` 目标定义带 `EXCLUDE_FROM_ALL`，默认 `ALL_BUILD` 不构建它。

结论：`HandSkeletonStudio` 及产品运行库 `handstudio_core` 的链接图均不包含 `six_imu_network` 与 Qt6 Network。

## 6. 遗留摘除项与 Python 依赖

### 6.1 从产品目标排除（文件保留）

以下 UI 源因引用网络类，未纳入任何产品目标编译，文件原样保留：

- `src/main_window.h/.cpp`（含 `slimevr_udp_client.h`/`slimevr_pose_sender.h` 依赖及成员 `SlimeVrUdpClient`）
- `src/settings_dialog.h/.cpp`、`src/slimevr_mount_dialog.h/.cpp`、`src/sensor_panel.h/.cpp`
- `src/serial_data_source.*`、`src/demo_data_source.*`（留待 SubStage 2 统一数据源）
- `src/hand_skeleton_*.*`（留待 SubStage 4/5）

### 6.2 从默认 CTest 摘除

- `tests/test_main_window.cpp`：其 `#include "main_window.h"` 间接依赖 SlimeVR 网络类（`slimevr_udp_client.h` 等），无法脱离 `six_imu_network` 编译，故从默认 CTest 摘除。摘除依据：`tests/test_main_window.cpp:3` 引入 `main_window.h`；`src/main_window.h:8-11` 引入网络头，`:69` 声明成员 `SlimeVrUdpClient slimeClient_`。
- 其余 slimevr 专项测试（`test_slimevr_*`、`test_hand_skeleton_*` 等）同样未注册进 CTest。

CTest 实际注册清单仅 6 项：`test_core_contracts`、`test_runtime_config`、`test_protocol`、`test_sequence_grouper`、`test_madgwick_filter`、`test_settings`。

### 6.3 无 Python 依赖

- `CMakeLists.txt` 全文无 `find_package(Python)`/`PySide`/`pip` 引用。
- `build-v2/CMakeCache.txt` 全文无 `Python`/`PySide`/`pip` 条目。

## 7. 契约核对结论

对照设计文档第 7 节逐字段核对 `src/core/`，全部合规：

- 7.1 `RawImuFrame`（`sensorId/address/sequence/receivedMonotonicNs/accelerationRaw/gyroscopeRaw/magnetometerRaw/crcValid/allZero`）✅
- 7.2 `SixImuSampleGroup`（`sequence/emittedMonotonicNs/samples/complete/presentMask`）✅
- 7.3 `CalibratedImuSample`（含 `calibrationState`）✅
- 7.4 `FusedImuPose`（`FusionMode {Invalid,SixD,NineD}`、`MagneticHealth`、`CalibrationState`）✅
- 7.5 `FingerObservation`/`HandObservationFrame`（`HandSide`、`wristWorldOrientation`、`fingers[5]`）✅
- 7.6 `BoneSource {Measured,Estimated,Held,Recovered,Invalid}`、`HandBoneFrame`、`HandSkeletonFrame`（含 `handSide/skeletonId/rootTransform/coupledApproximation/diagnostics`）✅
- 四元数默认单位（`QQuaternion` 默认恒等）、矩阵默认单位（`QMatrix4x4` 默认恒等）、时间 `qint64` 单调纳秒 ✅
- `Q_DECLARE_METATYPE` + 集中 `registerCoreMetaTypes()` ✅
- `schemaVersion` 与应用数据 schema 常量 ✅

未发现契约歧义，无需向 MainAgent 上报。

## 8. 风险与遗留问题

1. **旧全局命名空间类型并存**：`src/imu_types.h`（全局 `SensorId/ImuFrame/ImuSampleGroup`）与新的 `handstudio::*` 并存。这是刻意的迁移适配层，二者命名空间隔离、互不冲突；后续 SubStage 2-4 迁移协议/融合链路时应逐步以 `handstudio::` 契约为唯一权威定义，避免长期双轨。
2. **`test_main_window` 摘除未迁移**：其 UI 用例（对象名、设置对话框、demo 管线）在 SubStage 6 重建 UI 时需以新 `HandSkeletonStudio` 契约重新落地；当前摘除不影响本 Stage 验收。
3. **`six_imu_network` 仍以遗留目标保留**：源码未删除（符合禁止事项），但无任何产品目标引用；后续若确认彻底废弃，可在新 SubStage 明确移除。
4. **`runtime_config.cpp` 依赖 `<utility>`（`std::move`）为 Qt 头间接引入**：本次 MSVC 构建通过；若后续更换编译器或 Qt 版本，建议在 `runtime_config.cpp` 显式补 `#include <utility>`（本次未改动源码以保持半成品最小变更）。
5. **构建产物目录 `build-v2/` 未纳入版本控制**（`Document/` 整体未跟踪），证据以本报告记录的文件内容与命令为准。
