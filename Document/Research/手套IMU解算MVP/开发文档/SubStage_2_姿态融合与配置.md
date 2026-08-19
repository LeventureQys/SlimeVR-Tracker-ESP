# SubStage 2：姿态融合与配置任务书

- 所属 Stage：Stage 2 / 姿态算法核心
- 依赖前置：接口依赖 `src/imu_types.h`，任务书已完整定义；集成时从 SubStage 1 获取
- 并行状态：独立可执行，可与 SubStage 1、3 并行
- 所属阶段：阶段三·开发

## 1. 背景与目标

实现六路 Madgwick 姿态融合、原始量换算、九轴/六轴退化、单位四元数、ZYX 欧拉角、零位标定、持久化设置模型和单元测试。不实现串口和 UI Dialog。

## 2. 修改范围

- `src/solver_settings.h/.cpp`
- `src/settings_store.h/.cpp`
- `src/madgwick_filter.h/.cpp`
- `src/six_imu_solver.h/.cpp`
- `tests/test_madgwick_filter.cpp`
- `tests/test_settings.cpp`

不得修改协议实现、串口、UI 或参考程序。

## 3. 默认参数

- accel：`raw / 32768.0 * 16.0 g`；
- gyro：`raw / 32768.0 * 2000.0 °/s` 后转 rad/s；
- mag：`raw / 120.0` 参考值；
- beta：0.10；
- 首帧 dt：0.005 s；
- 最大 dt：0.1 s；
- 磁场模长默认 `[0.01, 1e9]`；
- 六路轴向完全相同。

## 4. 接口契约

严格实现 `设计文档.md` 第 10、11 节的 `SolverSettings`、`SettingsStore`、`MadgwickFilter`、`SixImuSolver`、`FusionMode`、`SensorPose`、`SixImuSnapshot`。

设置键固定使用 `solver/*`；生产标识由 main 设置为 `SlimeVRResearch/SixImuSolverQt`。测试注入临时 INI `QSettings`。

## 5. 算法要求

- 使用标准 Madgwick IMU 与 MARG 公式；
- Hamilton 四元数 `[w,x,y,z]`；
- 四元数为局部到世界右手旋转；
- 每次更新归一化；
- 梯度接近零时只做陀螺积分；
- 磁场禁用、非有限、近零或超阈值时用 SixAxis；
- 非法 dt、非有限 gyro/acc、不可归一四元数为硬错误；
- 六路先在临时状态更新，任一路硬错误时整组不提交；
- 磁场退化不是硬错误；
- 全零源帧整组不更新并发布可诊断失败状态或不发布新快照，不能传播 NaN；
- 相对姿态 `normalized(qZero.conjugated() * qWorld)`；
- 欧拉角使用内禀 ZYX，Roll X、Pitch Y、Yaw Z；
- `q` 与 `-q` 视为等价；
- applySettings 重置六路状态和零位。

## 6. 设置验证

按 `设计文档.md` 第 10.1 节范围；所有 double 先检查有限。加载缺键逐项默认，组合非法整体默认。保存先校验，写入、sync、检查 status；失败返回 false。

## 7. 单元测试

- raw 16384 → 8g、1000°/s，mag 120 → 1；
- int16 最小最大无溢出；
- 静止姿态有限且单位化；
- 已知单轴旋转方向；
- NineAxis 与 SixAxis；
- 磁场恢复；
- 非法 dt 不提交；
- 六路相同输入得到等价姿态；
- 任一路硬错误整组原子回滚；
- 标定后相对姿态单位化；
- 设置默认、边界、NaN/Inf、保存重载、损坏回退；
- reset 与 applySettings 语义。

浮点断言使用角距离与容差，不直接要求四元数分量同号。

## 8. 验收标准

- 算法类不依赖 Widgets、SerialPort 或 UI；
- 所有测试通过；
- 不产生 NaN/Inf；
- 设置保存失败不替换旧配置；
- 完成后报告文件、公式来源、命令和测试结果。

## 9. 禁止事项

- 禁止将 °/s 直接传给算法；
- 禁止用欧拉角作为内部状态；
- 禁止吞掉设置写错误；
- 禁止自行添加每路轴映射；
- 禁止实现关节角或三维模型；
- 遇算法歧义停止并报告 MainAgent。
