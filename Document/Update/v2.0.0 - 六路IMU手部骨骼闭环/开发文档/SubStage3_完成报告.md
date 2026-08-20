# SubStage 3 完成报告：校准与融合层

- 所属 Stage：SubStage 3
- 依赖前置：SubStage 1（统一契约）
- 并行状态：接口实现独立完成；真实数据集集成测试留接口占位
- 所属阶段：阶段三·开发
- 执行结果：完成（全部验收标准达成，VQF 已本地取得）

## 1. 结论摘要

| 验收项 | 结果 |
| --- | --- |
| `src/calibration/`：量程、轴置换/符号、每设备参数、静止零偏、磁校准接口、持久化 | ✅ 达成 |
| `src/fusion/`：`IFusionFilter`、Madgwick/VQF 封装、`FusionBank`、磁健康状态机、姿态保护 | ✅ 达成 |
| VQF 纯算法源码 | ✅ 已在仓库 `lib/vqf/` 本地取得（MIT），无需网络 |
| Madgwick/VQF 通过同一 `IFusionFilter` 接口运行 | ✅ `test_fusion` 全项覆盖 |
| 六路输出无 NaN/Inf/零范数，范数误差 ≤ 1e-4 | ✅ `PoseGuard` + 单测 |
| 磁干扰不产生未限速航向跳变 | ✅ `FusionBank` Recovering 航向限速 + 单测 |
| Release 构建 + CTest | ✅ 0 错误，13/13 测试通过 |
| 启动测试 | ✅ `HandSkeletonStudio.exe` 退出码 0 |
| 遗留 `test_madgwick_filter` / `test_settings` 保持通过 | ✅ |

真实数据集（SubStage 2）集成测试按任务要求留接口占位：`CalibrationPipeline::calibrate(RawImuFrame)` / `FusionBank::process(CalibratedImuSample)` 即集成入口，未产出“真实精度”结论。

## 2. 文件清单

### 2.1 新增 `src/calibration/`

- `axis_remap.h` — 轴置换/符号映射（3×3 带符号置换矩阵，合法性校验 + `apply`）
- `calibration_parameters.h/.cpp` — 每设备参数（量程、三组轴映射、陀螺零偏、磁硬铁/软铁、设备身份、schema）、`isValid`、`calibrationState`
- `rest_detector.h/.cpp` — 静止检测（加速度稳定性 + 角速度阈值 + 最短持续时长）
- `static_gyro_bias_estimator.h/.cpp` — 启动静止零偏估计（静止累加、运动冻结、收敛判定、限幅）
- `magnetic_calibration.h/.cpp` — 磁硬铁（均值）/软铁（轴向缩放）校准接口 + 校验 + `apply`
- `calibration_pipeline.h/.cpp` — 原始帧 → `CalibratedImuSample`；量程换算、轴映射、磁修正、零偏注入、结构化诊断
- `calibration_store.h/.cpp` — 参数持久化（JSON，绑定设备身份 + schema 版本，往返校验）

### 2.2 新增 `src/fusion/`

- `ifusion_filter.h` — 算法替换边界接口（`reset`/`update`，按任务书第 4 节）
- `quaternion_util.h` — 四元数范数/有限性/归一/符号/航向提取等纯函数
- `madgwick_fusion_filter.h/.cpp` — 包装旧 `MadgwickFilter` 数学核心（pimpl，内部类型不泄漏）
- `vqf_fusion_filter.h/.cpp` — 包装 VQF（pimpl，VQF 类型不泄漏进公共契约）
- `magnetic_health_monitor.h/.cpp` — Unavailable/Healthy/Disturbed/Recovering 迟滞状态机
- `pose_guard.h/.cpp` — 发布前保护：有限/范数误差/符号连续/上一有效姿态保持
- `fusion_bank.h/.cpp` — 六路独立实例、dt 单调时间差计算与回退、SixD/NineD 决策、Recovering 航向限速、置信度、诊断
- `vqf/vqf.h` + `vqf/vqf.cpp` — vendored VQF 纯算法（详见第 3 节）

### 2.3 修改

- `src/six_imu_solver.h/.cpp` — 升级兼容层：`applySettings` 非法值与 `processCompleteGroup` 拒绝路径改为结构化诊断（`takeDiagnostics()`），新增 `latestFusedPoses()` 统一契约适配输出。行为向后兼容，`test_madgwick_filter` 保持通过。
- `cmake/substage3.cmake` — 新建，自包含，接入 `handstudio_core` 源与 `test_calibration`/`test_fusion`（复用根 `add_handstudio_test` 助手，手动 moc）。

### 2.4 新增测试

- `tests/test_calibration.cpp` — 12 项
- `tests/test_fusion.cpp` — 8 项

## 3. VQF 来源与许可证据

| 项 | 值 |
| --- | --- |
| 来源 | 仓库根 `lib/vqf/`（SlimeVR-Tracker-ESP 检出，已本地存在，未联网） |
| 检出 commit | `9ae0df595692bef5b2360bc374a1803a99394c7d`（2026-08-03） |
| remote | origin `https://github.com/LeventureQys/SlimeVR-Tracker-ESP.git`；upstream `https://github.com/SlimeVR/SlimeVR-Tracker-ESP.git` |
| 原始算法 | D. Laidig, "VQF: Highly Accurate IMU Orientation Estimation with Bias Estimation and Magnetic Disturbance Rejection"（https://github.com/dlaidig/vqf） |
| 许可证 | **MIT**，`SPDX-FileCopyrightText: 2021 Daniel Laidig`，`SPDX-License-Identifier: MIT`（头文件第 1–3 行保留） |

本地修改（均记录于 vendored 文件内）：

1. 保留上游 SlimeVR 既有修改：`updateGyr(gyr, gyrTs)` 增加逐调用时间戳、移除 batch 更新函数。
2. MSVC 可移植性：
   - `M_PI` / `M_SQRT2` 加 `#ifndef` 守卫；
   - `vqf.cpp` 将 `#define _USE_MATH_DEFINES` + `#include <math.h>` 移到 `#include "vqf.h"` 之前，避免宏重定义警告；
   - 将 `std::fill(..., 0)` / `0.0` / `-1` 等整型/双精度字面量改为 `vqf_real_t(...)`，消除 C4244 窄化警告。

VQF 已封装为 `VqfFusionFilter`（pimpl），VQF 内部类型（`VQF`/`VQFParams`/`vqf_real_t`）不泄漏到 `ifusion_filter.h` 或核心公共契约。删除 Arduino/ESP/网络/全局配置依赖：vendored 文件仅依赖 `<algorithm>/<limits>/<math.h>/<assert.h>/<stddef.h>`，无任何 ESP/Arduino 头。

## 4. 构建与测试结果

环境：CMake 4.3.2；Visual Studio 17 2022（MSVC 19.44.35226）；Qt 6.8.3 `msvc2022_64`；独立构建目录 `build-s3`（按并行协调指令，未用 build-v2/build）。

```powershell
cmake -S . -B build-s3 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH=D:/Devtools/Qt/6.8.3/msvc2022_64
cmake --build build-s3 --config Release
```

构建结果：**exit 0**，`handstudio_core.lib`、`HandSkeletonStudio.exe`、`test_calibration.exe`、`test_fusion.exe` 全部产出，无错误。

CTest（全量 13 项）：

```text
1/13 test_core_contracts ..... Passed
2/13 test_runtime_config ..... Passed
3/13 test_protocol ........... Passed
4/13 test_sequence_grouper ... Passed
5/13 test_madgwick_filter .... Passed   （遗留，保持通过）
6/13 test_settings ........... Passed   （遗留，保持通过）
7/13 test_protocol_v2 ........ Passed
8/13 test_recording .......... Passed
9/13 test_replay ............. Passed
10/13 test_calibration ....... Passed   （新增）
11/13 test_fusion ............ Passed   （新增）
12/13 test_glb_model ......... Passed
13/13 test_render_offscreen .. Passed
100% tests passed, 0 tests failed out of 13
```

启动测试：

```powershell
& .\build-s3\Release\HandSkeletonStudio.exe
# HandSkeletonStudio 2.0.0 runtime-config-schema=1 recording-schema=1
# exit=0，无崩溃
```

## 5. 保护性处理与测试证据（任务书第 6 节逐项）

| 需求 | 覆盖测试 |
| --- | --- |
| 量程换算 | `test_calibration::rangeScalingConvertsUnits`（16384/16g→8g=78.453 m/s²；16384/2000dps→1000°/s） |
| 轴置换和符号 | `axisRemapIdentityAndPermutation`、`axisRemapRejectsInvalidMatrix`、`axisRemapAndSignAppliedToRaw` |
| 每传感器独立参数，不串路 | `perSensorParamsAreIndependent`（同一原始帧，腕 8g / 拇指 16g 各自换算） |
| 静止零偏收敛与运动时冻结 | `staticGyroBiasConvergesAtRest`、`staticGyroBiasFreezesDuringMotion` |
| 磁硬铁/软铁校准接口与校验 | `magneticHardSoftIronCalibrationAndValidation`、`magneticCalibrationRejectsInsufficientOrDegenerate` |
| 参数持久化绑定设备身份与 schema | `calibrationPersistenceRoundTrip`、`calibrationPersistenceRejectsBadSchema`、`calibrationPersistenceRequiresDeviceId` |
| 已知单轴角速度姿态方向 | `madgwickSingleAxisRotationHasCorrectDirection`、`vqfSingleAxisRotationHasCorrectDirection` |
| dt=0/负值/过大回退 | `dtFallbackOnZeroNegativeAndLarge`（3 次回退 + 结构化诊断） |
| NaN/Inf/零范数保护 + 上一值保持 | `nanInfZeroNormProtectionHoldsLastPose` |
| q/-q 符号连续 | `signContinuityNegatesEquivalentQuaternion` |
| 磁干扰迟滞、SixD 退化、Recovering | `magneticHealthHysteresisStateMachine`、`disturbedDegradesToSixDAndRecoveringRateLimitsHeading` |
| 同一数据集 Madgwick/VQF 输出统一字段 | `madgwickAndVqfProduceUnifiedFieldsOnSameDataset`（valid/有限/范数误差≤1e-4/confidence∈[0,1]） |

范数误差 ≤ 1e-4 由 `PoseGuard`（拒绝 `>1e-4`、归一化 `≤1e-4`）与上述逐帧断言双重保证。

## 6. 升级/兼容与迁移状态

- 旧 `SixImuSolver` 保留为编排适配层：`processCompleteGroup` / `applySettings` 不再静默 `return`，改为写入 `handstudio::Diagnostic`；新增 `latestFusedPoses()` 输出统一 `FusedImuPose`。原行为不变，`test_madgwick_filter` 全部用例通过。
- 旧纯数学核心 `src/madgwick_filter.cpp` 现同时编译进 `handstudio_core`（供新 `MadgwickFusionFilter` 包装）与遗留 `six_imu_core`（供旧 `SixImuSolver`）。二者为同一源文件；当前无任何可执行目标同时链接两库，链接器按需取单一对象，不产生重复符号。**待 `six_imu_core` 退役时，应从其 `LEGACY_CORE_SOURCES` 移除 `madgwick_filter.*`，以消除双编译**（根 CMakeLists 由 MainAgent 独占，本任务未改动）。
- `src/core/` 与 `assets/` 为共享只读区，本任务未修改；新增 `CalibrationSchemaVersion = 1` 定义在 `src/calibration/calibration_store.h`（不触碰 core 的 schema_version.h）。

## 7. 遗留风险

1. **真实量程未确认**：磁力计增益默认 `magnetometerGainMicroTeslaPerLsb = 1.0` 为占位值，未用真实硬件确认；合成测试仅验证算法正确性，不产生真实精度结论（符合禁止事项）。
2. **磁健康仅按模长判异常**：`MagneticHealthMonitor` 以磁场模长偏离参考判 Disturbed，未包含方向/磁倾角判据；永久性磁场环境变化会长期停留 Disturbed（VQF 内部自带“新磁场接受”可部分弥补，但本状态机未实现）。真实数据集集成时需评估。
3. **VQF 采样率近似**：vendored VQF 的 `updateAcc/updateMag` 使用构造期 `accTs/magTs`（固定），仅 `updateGyr` 接受逐调用 `gyrTs`；变 dt 场景下加速度/磁滤波时间常数按标称 200 Hz 近似。
4. **磁硬铁/软铁为轴向缩放简化法**：未做完整 9 参数椭球拟合，真实畸变场下精度有限，仅作为接口与确定性校验基线。
5. **SubStage 2 真实数据集集成未执行**：留接口占位（`calibrateGroup` + `processGroup`），六路量化表（静置抖动/10 分钟漂移/动作响应/恢复时间/无效帧/CPU 时间）待真实数据补齐后填写，本报告不推断结果。
6. 并行开发期间根 `CMakeLists.txt` 由 MainAgent 独占维护，本任务全部 CMake 变更集中在 `cmake/substage3.cmake`。
