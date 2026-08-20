# SubStage 1：工程基座与统一契约

- 所属 Stage：SubStage 1
- 依赖前置：无
- 并行状态：独立可执行
- 所属阶段：阶段三·开发

## 1. 背景与目标

V2.0 产品必须以 `Document/Research/手套IMU解算MVP/` 为唯一工程根，收敛为纯 C++20/Qt6 应用 `HandSkeletonStudio`。本任务建立后续所有 SubStage 共享的数据契约、目录、配置和构建目标，并从产品运行时剥离 SlimeVR 网络与 Python 依赖。

## 2. 当前代码状态

- `Document/Research/手套IMU解算MVP/CMakeLists.txt:2` 工程名为 `SixImuSolverQt`，`:4` 使用 C++17。
- 同文件 `:393` 定义 SlimeVR 网络源，`:425` 的 `six_imu_runtime` 链接 `six_imu_network`。
- `src/imu_types.h:58`、`:69` 定义旧 `ImuFrame`/`ImuSampleGroup`。
- `src/six_imu_solver.h:20` 的 `SensorPose` 混合融合与上层相对姿态。
- `src/hand_skeleton_types.h:11` 的骨骼来源和 `:36` 的骨骼帧字段不完整。

## 3. 修改范围

- 修改根 `CMakeLists.txt` 和必要的现有核心头文件/实现。
- 新增 `src/core/`：`sensor_id.h`、`imu_frames.h`、`fusion_types.h`、`hand_observation_types.h`、`hand_skeleton_frame.h`、`diagnostic.h`、`schema_version.h`。
- 新增 `src/config/` 的版本化 JSON 配置加载/校验骨架。
- 新增对应 `tests/test_core_contracts.cpp`、`tests/test_runtime_config.cpp`。
- 可保留旧文件作为迁移适配层，但不得形成重复权威定义。

## 4. 接口契约

完整字段以 `设计文档.md` 第 7 节为准。命名空间统一为 `handstudio`。必须提供：

- `SensorId` 与地址/数组索引的安全转换。
- `RawImuFrame`、`SixImuSampleGroup`、`CalibratedImuSample`。
- `FusedImuPose`、`HandObservationFrame`、`HandSkeletonFrame`。
- `Diagnostic { severity, code, message, detail, timestampNs }`。
- Qt queued connection 所需的 `Q_DECLARE_METATYPE` 与集中注册函数。
- 配置 `schemaVersion` 和应用数据 schema 常量。

四元数使用 Hamilton `wxyz`；矩阵使用 `QMatrix4x4`；时间均为单调纳秒，持久化时另记录 UTC。

## 5. 构建要求

- C++ 标准升级为 C++20。
- 创建 `handstudio_core` 静态库与 `HandSkeletonStudio` 可执行目标。
- SlimeVR 网络源码可留在仓库，但不得由 `HandSkeletonStudio` 或其产品运行库链接。
- CMake 配置、编译和测试不得检查 Python、PySide6 或 pip。
- 保留现有 QtTest 能力，允许在 DSH 环境继续使用现有手动 moc 方案。

## 6. 单元测试

- 六个地址与索引双向转换。
- 非法地址返回错误，不越界。
- 所有核心结构可通过 queued signal 元类型系统传递。
- 四元数默认值为单位四元数；矩阵默认值明确。
- 配置缺字段、错误 schema、非有限数字、非单位安装四元数均失败并返回结构化诊断。
- 验证 `HandSkeletonStudio` 链接图不包含 `six_imu_network`。

## 7. 验收标准

- Release 构建通过。
- 本任务新增和受影响的现有 QtTest 全部通过。
- 后续任务只需包含核心头即可获得完整契约。
- 产品构建和测试无 Python 依赖。
- SlimeVR 网络不在产品依赖图中。

## 8. 禁止事项

- 不实现协议、融合、GLB 渲染或 UI 功能。
- 不删除旧网络源码；只剥离产品依赖，避免扩边界。
- 不在多个头文件复制同一结构定义。
- 不改变 `AA 55` 协议字节布局。

## 9. 完成报告要求

报告修改文件、构建命令、测试命令、测试数量与结果、产品依赖验证证据；遇到契约歧义立即停止并报告 MainAgent。