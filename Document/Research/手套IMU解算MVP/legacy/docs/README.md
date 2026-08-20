# 手套六 IMU 实时解算 Qt 工具 MVP

这是一个独立的 Qt 6 Widgets 桌面工具，通过串口接收手套上六个 IMU 的九轴原始数据，完成协议同步、CRC16 校验、同序号六路分组、Madgwick 姿态融合、数据显示、链路统计和六路零位标定。程序也提供演示数据源，演示数据与真实串口数据共用完整的解析、分组和融合链路。

## 功能概述

- 接收固定 25 字节二进制帧并进行 Modbus CRC16 校验。
- 按 `SEQ` 聚合地址 `0x50` 至 `0x55` 的六路同步采样。
- 分别显示手腕、拇指、食指、中指、无名指和小指的原始九轴值。
- 输出单位四元数，并显示 ZYX Roll、Pitch、Yaw 欧拉角。
- 磁场有效时采用九轴融合，磁场无效或禁用时自动退化为六轴融合。
- 提供串口诊断统计、姿态参数对话框、六路零位标定和演示模式。
- 提供协议、分组、姿态、设置和 UI 五个 QtTest 测试目标。
- 提供 SlimeVR UDP 输出：一个手套设备、六个传感器节点，姿态发送率可配置。
- 提供左右手节点映射、每路安装旋转配置和无界面真实 Server 验证探针。

## 范围外

本 MVP 不包含人体骨骼约束、MCP/PIP/DIP 关节角求解、三维手模渲染、数据录制、设备寄存器配置写入或固件修改。SlimeVR 输出为 Windows 接入 PoC，不修改 ESP 固件。

## 依赖

- Windows 10/11 x64。
- Visual Studio 2022，安装 MSVC x64 C++ 工具链。
- CMake 3.20 或更高版本。
- Qt 6，组件：`Core`、`Gui`、`Widgets`、`SerialPort`、`Network`、`Test`。
- 本机已验证 Qt：`D:\Devtools\Qt\6.8.3\msvc2022_64`。

## 配置与构建

在 PowerShell 中进入本 README 所在目录：

```powershell
cd D:\workshop\Processing\SlimeVR-Tracker-ESP-Leventure\Document\Research\手套IMU解算MVP
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="D:\Devtools\Qt\6.8.3\msvc2022_64"
cmake --build build --config Debug
```

Debug 主程序位于：

```text
build\Debug\SixImuSolverQt.exe
```

构建应同时生成测试程序：

- `test_protocol.exe`
- `test_sequence_grouper.exe`
- `test_madgwick_filter.exe`
- `test_settings.exe`
- `test_main_window.exe`
- `test_slimevr_protocol.exe`
- `test_slimevr_udp_client.exe`
- `test_slimevr_sensor_mapping.exe`
- `test_slimevr_pose_sender.exe`
- `test_slimevr_coordinate_transform.exe`
- `test_slimevr_integration.exe`

## 测试

运行全部 CTest：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

UI 测试通过 CTest 自动设置 `QT_QPA_PLATFORM=offscreen`。若直接运行 UI 测试程序，先设置：

```powershell
$env:QT_QPA_PLATFORM='offscreen'
.\build\Debug\test_main_window.exe
```

UI 测试使用工程目录下的 `build/test_ui_settings.ini`，不会写入系统临时目录或生产设置。

## 运行

正常启动：

```powershell
.\build\Debug\SixImuSolverQt.exe
```

演示模式启动并在 3 秒后自动退出：

```powershell
.\build\Debug\SixImuSolverQt.exe --demo --quit-after-ms 3000
```

参数说明：

- `--demo`：启动确定性合成六 IMU 数据，界面明确显示“演示模式 · 非真实设备数据”。
- `--quit-after-ms <毫秒>`：指定自动退出时间；值必须大于 0，非法值返回非零退出码。

演示模式不能替代真实硬件验收。

## 真实串口使用

1. 连接手套接收设备并确认 Windows 已识别串口。
2. 启动程序，点击“刷新串口”。
3. 从串口列表选择目标端口。
4. 确保演示模式未启用，点击“打开串口”。
5. 程序固定使用 `921600 baud`、`8` 数据位、无校验、`1` 停止位、无流控，即 `921600/8N1`。
6. 检查六个传感器面板持续更新，完整组、有效帧和各传感器帧计数持续增长。
7. 设备静止且六路姿态均有效后执行“六路零位标定”。
8. 结束时点击“关闭串口”。

真实串口与演示数据源互斥；启用演示会关闭真实串口并重置解析、分组、融合和零位状态。

## SlimeVR 网络输出

“SlimeVR 输出”分组提供：

- 启用开关（默认关闭，关闭即完全回退原工具行为）；
- 自动发现（广播）或固定地址（IPv4 字面量）；
- Server 端口（默认 `6969`）、左手/右手、发送率 `50–100 Hz`（默认 75）；
- 六路安装旋转编辑（单位四元数，默认单位值）。

连接后注册一个设备六个节点（Wrist/Thumb/Index/Middle/Ring/Little），每秒重发注册；Server 重启后自动重连。发送的始终是融合 `worldOrientation`，工具端“六路零位标定”只影响本地显示；挂载校准请在 SlimeVR Server 中执行。

设置键为 `slimevr/*`，缺失或非法时逐键回退默认。真实 Server 验证可用无界面探针：

```powershell
.\build\Debug\slimevr_bridge_probe.exe --host 127.0.0.1 --port 6969 --side left --rate 75 --quit-after-ms 60000
```

详细操作与排障见 `Document/Develop/V1.0/Stage2/S2.4_集成稳定性与PoC验收/用户操作说明.md`。

## 默认换算

默认 `SolverSettings`：

- 加速度量程：`±16 g`，换算为 `raw / 32768 × 16 g`。
- 角速度量程：`±2000 °/s`，换算为 `raw / 32768 × 2000 °/s`，融合前再转为 `rad/s`。
- 磁场参考值：`raw / 120`；该值不声明为 µT。
- Madgwick `beta`：`0.10`。
- 磁场融合：默认启用。
- 磁场有效模长范围：`[0.01, 1.0e9]`。

## 姿态参数对话框

点击“姿态参数”可修改：

- 加速度量程。
- 陀螺仪量程。
- 磁场除数。
- Madgwick `beta`。
- 是否启用磁场融合。
- 磁场最小、最大有效模长。

“恢复默认值”只更新对话框草稿；点击“取消”不会保存。点击“确定”时会校验所有数值以及 `minNorm < maxNorm`。合法设置保存后立即应用，并重置融合状态和零位；串口连接与协议统计保持不变。

## QSettings 持久化

生产设置标识：

```text
organizationName = SlimeVRResearch
applicationName  = SixImuSolverQt
```

设置键：

```text
solver/accelerometerRangeG
solver/gyroscopeRangeDps
solver/magnetometerDivisor
solver/madgwickBeta
solver/magnetometerEnabled
solver/magnetometerMinNorm
solver/magnetometerMaxNorm
```

缺失或无法转换的单个键回退到默认值；若组合校验失败，则整组回退默认设置。

## 零位标定

“六路零位标定”仅在最新快照的六路姿态全部有效时启用。标定会保存每路当前世界四元数作为零位，后续显示相对于该零位的四元数和欧拉角。应用新设置、重新打开串口、切换演示模式或显式重置都会清除零位。

“清除零位”恢复未标定状态，不修改原始数据或持久化参数。

## FusionMode

- `NineAxis`：加速度、陀螺仪和有效磁场共同参与融合。
- `SixAxis`：磁场被关闭、无效、非有限或模长越界时，自动使用加速度与陀螺仪融合。
- `Invalid`：尚无有效姿态，或当前输入无法形成有效更新。

六轴退化是安全运行路径，不代表程序故障；界面会显示每路当前模式。

## 已知限制

- 当前 DSH 沙箱会阻止 CMake `AUTOMOC` 的嵌套进程启动，因此工程使用 configure 阶段的手动 `moc`。不要将该实现误解为普通 Qt 环境的强制要求。
- UI 刷新约 30 Hz，设备数据约 200 组/秒；界面展示最新快照，而不是每组逐帧渲染。
- 演示模式只验证软件链路，不能证明串口电气连接、实际帧质量、传感器轴向或现场磁环境正确。
- 没有真实硬件时，真实串口验收处于阻塞状态，必须在验收报告中明确记录，不能用演示模式声明通过。

## 真实硬件验收要求

连接真实六 IMU 手套后至少连续运行 60 秒，并记录以下证据：

- 串口按 `921600/8N1` 成功打开，期间程序无崩溃、Qt fatal 或 critical 日志。
- 六个地址 `0x50` 至 `0x55` 均持续有帧，完整组计数持续增长。
- 原始九轴值随设备运动合理变化，静止时数据不出现持续全零或明显异常跳变。
- 六路四元数有限且保持单位化，欧拉角随运动连续变化。
- 正常磁场时显示 `NineAxis`；禁用磁场或制造无效磁场条件时可观察到 `SixAxis` 退化。
- 六路全部有效时标定按钮启用；静止标定后相对姿态接近单位旋转。
- 关闭、重开串口以及切换设置后，融合状态和零位按设计重置。

若没有可用真实硬件或驱动，记录具体设备缺失情况、执行日期和未完成条目，并将该项保留为验收阻塞。
