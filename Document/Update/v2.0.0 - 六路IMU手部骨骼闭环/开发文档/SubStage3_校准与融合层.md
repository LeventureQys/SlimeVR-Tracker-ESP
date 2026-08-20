# SubStage 3：校准与融合层

- 所属 Stage：SubStage 3
- 依赖前置：等待 SubStage 1；集成测试等待 SubStage 2 的真实录制数据集
- 并行状态：接口实现可与 SubStage 2、5 并行
- 所属阶段：阶段三·开发

## 1. 背景与目标

将旧 `SixImuSolver` 拆为校准流水线和六路融合器组，保留 Madgwick 基线并接入纯 C++ VQF 候选，输出统一 `FusedImuPose`。实现独立零偏、磁健康、SixD/NineD 退化、有限性和符号连续保护。

## 2. 当前代码状态

- `src/six_imu_solver.h:41` 直接处理完整组并持有六个 Madgwick 过滤器。
- `src/six_imu_solver.h:20` 的 `SensorPose` 缺 rest、gyro bias、magnetic health、confidence 等统一字段。
- `src/madgwick_filter.h/.cpp` 与 `tests/test_madgwick_filter.cpp` 可作为基线。
- SlimeVR 网络代码不属于本任务；仅允许从许可证兼容的纯算法来源抽取 VQF/静止/偏置能力，并保留许可说明。

## 3. 修改范围

- 新增 `src/calibration/`：单位换算、轴映射、每设备参数、静止零偏、磁校准和配置持久化。
- 新增 `src/fusion/ifusion_filter.h`、`madgwick_fusion_filter.*`、`vqf_fusion_filter.*`、`fusion_bank.*`、`magnetic_health_monitor.*`。
- 升级旧 `SixImuSolver` 为编排器或提供兼容适配层。
- 新增校准、Madgwick、VQF、状态机和录制数据对比测试。

## 4. 接口契约

```cpp
class IFusionFilter {
public:
  virtual ~IFusionFilter() = default;
  virtual void reset() = 0;
  virtual FusedImuPose update(const CalibratedImuSample&, double dtSeconds) = 0;
};
```

- 六颗传感器各自持有过滤器和校准状态。
- `dt` 优先由单调时间戳计算；异常时回退 0.005 秒并产生诊断。
- 发布前检查四元数有限、范数误差和符号连续；非法输出保持上一有效姿态并降低置信度。
- 磁状态：Unavailable/Healthy/Disturbed/Recovering；Disturbed 使用 SixD，恢复时航向修正限速。
- 配置必须绑定设备身份和 schema 版本。

## 5. VQF 来源要求

- 记录来源仓库、版本/commit、许可证和本地修改。
- 删除 Arduino、ESP、网络和全局配置依赖。
- 封装为 `IFusionFilter`，不得让 VQF 类型泄漏到上层。
- 使用固定输入验证抽取实现的确定性。

## 6. 单元测试

- 量程换算、轴置换和符号。
- 每传感器独立参数，不串路。
- 静止零偏收敛与运动时冻结。
- 已知单轴角速度的姿态方向。
- dt=0、负值、过大值的回退。
- NaN/Inf/零范数保护和上一值保持。
- q/-q 符号连续。
- 磁干扰迟滞、SixD 退化和 Recovering。
- 同一数据集 Madgwick/VQF 均输出统一字段。

## 7. 量化报告

使用 SubStage 2 数据集分别记录六颗 IMU：静置抖动、10 分钟航向漂移（若数据时长允许）、动作响应、恢复时间、无效帧和 CPU 时间。若数据不足 10 分钟，明确标注不满足最终指标，不推断结果。

## 8. 验收标准

- 两种算法通过同一接口运行。
- 六路输出无 NaN、Inf、零范数，范数误差≤`1e-4`。
- 磁干扰不会产生未限速的航向跳变。
- 默认算法选择有数据依据；数据不足时保持配置可选并延期最终选择。
- 新增和相关现有单测全部通过。

## 9. 禁止事项

- 不接入 SlimeVR 网络。
- 不使用未经确认的量程给出真实精度结论。
- 不在 UI 层修正坐标或四元数。
- 不把 VQF 内部类型放入核心公共契约。

## 10. 完成报告要求

列出来源许可证、修改文件、测试结果、六路量化表、默认算法决定或延期理由。