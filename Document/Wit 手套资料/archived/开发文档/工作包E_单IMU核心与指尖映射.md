# 工作包 E：单 IMU 核心与指尖映射

## 1. 目标与前置

在研究项目内复用 WIT BLE 协议核心，新增单个指尖 IMU 的校准、手指路由和整链约束映射。无需修改渲染模块或 Adapter 历史代码。

- 设计依据：`document/单蓝牙IMU指尖联动设计.md`。
- 依赖前置：现有 `PoseSolver`、`RigConfig` 和 `config/hand_rig.json` 可用。
- 可并行内容：BLE 源文件迁移与纯运动控制器可以并行实现，但测试目标统一后再集成。

## 2. 修改范围

- 新增 `src/imu/imu_data.h`。
- 新增 `src/imu/wit_protocol_parser.h/.cpp`。
- 新增 `src/imu/wit_ble_manager.h/.cpp`。
- 新增 `src/imu/CMakeLists.txt`。
- 新增 `src/motion/single_imu_finger_controller.h/.cpp`。
- 修改 `src/motion/CMakeLists.txt`、根 `CMakeLists.txt`。
- 新增协议和单 IMU 控制器测试。

不得修改研究项目外文件，不得从主工程链接 BLE 代码，不得删除六路模拟接口。

## 3. 接口契约

严格实现 `document/单蓝牙IMU指尖联动设计.md` 第 5 节的 `SingleImuDriveState`、`SingleImuMappingOutput` 和 `SingleImuFingerController`。四元数公式、换手指清除校准、断开复位、时间戳单调和无效帧保持策略不得在 UI 中重复实现。

BLE 模块复制 Adapter 中已验证实现，命名空间统一为 `handdemo::imu`。如需要调整 API，只允许增加用于主窗口装配的信号或只读查询，不改变 WIT UUID、20 字节帧格式和换算比例。

## 4. 验收标准

- Qt Bluetooth 依赖只增加在研究工程。
- 协议回归测试覆盖分包、粘包、噪声和角度换算。
- 五个手指均可作为绑定目标，单次只改变一条链。
- 任意绝对初始姿态经校准后输出绑定姿态。
- 已知相对屈伸和张合输入产生有限、受限的耦合结果。
- 未连接、未绑定、未校准不能进入驱动状态。
- 换手指、断开、重置校准后停止驱动。
- 零模、非有限和倒退时间戳不产生 NaN。

## 5. 完成记录要求

记录新增文件、接口偏差、测试命令、测试结果和任何真实轴向假设。构建与测试日志保存到 `outputs/`。
