# WIT 单 IMU 指尖联动手模原型

这是一个基于 **Qt 6 + C++20 + OpenGL 3.3** 的可活动骨架手模原型。项目可以加载带骨架和蒙皮权重的 FBX 手模，并用一枚佩戴在指尖末端的 WIT 蓝牙 IMU，驱动所选手指完成屈伸与小范围张合。

项目的核心不是“直接追踪三维指尖位置”，也不是通过 IK 反解每个关节角，而是：

1. 读取指尖 IMU 的姿态；
2. 相对临时零姿态计算指尖旋转；
3. 从旋转中提取屈伸和张合分量；
4. 按配置把末端观测耦合到整条手指骨链；
5. 通过前向运动学和 GPU 蒙皮，让网格上的指尖随骨链移动。

> 单枚指尖 IMU 无法唯一恢复 MCP/PIP/DIP 的真实关节角。当前实现是连续、受限、可解释的工程近似，不用于医学测量。

![单 IMU 交互与姿态流程](figures/Fig001_单IMU交互与姿态流程.png)

## 当前能力

- 通过 Assimp 导入 `models/3D带骨架手模.fbx` 的网格、骨架层级、绑定变换和蒙皮权重。
- 使用 `QOpenGLWidget`、OpenGL 3.3 Core 和 GLSL 330 实时执行 GPU 蒙皮。
- 支持手动关节编辑、六路 IMU 模拟和真实单 IMU 三种输入模式。
- 通过 Qt Bluetooth 扫描并连接名称以 `WT` 开头的兼容 BLE 设备。
- 解析 WIT 20 字节通知流，获得加速度、角速度、欧拉角、电量、温度和固件版本。
- 将一枚真实 IMU 绑定到拇指、食指、中指、无名指或小指。
- 通过临时零姿态校准消除佩戴时的初始绝对朝向。
- 对关节执行轴锁定、角度限位和屈伸耦合，保持绑定平移、缩放与骨长。
- 提供模型导入、运动映射、WIT 协议解析和首帧渲染 smoke test。

## 指尖是怎么移动的

### 1. 端到端关键路径

```text
WIT BLE 通知
  │
  ▼
WitBleManager::notificationReceived(QByteArray)
  │  订阅 FFE4 特征，接收设备字节流
  ▼
WitProtocolParser::appendBytes()
  │  对齐 0x55 帧头，按 20 字节拆帧
  │  0x61 帧换算为加速度、角速度和欧拉角
  ▼
MainWindow::updateRealImu()
  │  QQuaternion::fromEulerAngles(angleX, angleY, angleZ)
  ▼
SingleImuFingerController::update()
  │  临时零姿态相对化
  │  安装坐标修正
  │  swing-twist 分解屈伸与张合
  │  屈伸归一化为 curl
  ▼
PoseSolver::applyFingerPose()
  │  curl 按 coupling 分配到 MCP/PIP/DIP 骨链
  │  张合只施加到基部关节
  │  锁轴并截断到关节限位
  ▼
PoseSolver::solve()
  │  逐级计算骨骼 globalMatrices
  ▼
MainWindow::applyPose()
  ▼
HandRenderWidget::setPoseResult()
  │  每个网格生成骨骼调色板
  ▼
GLSL 顶点着色器执行蒙皮
  │
  ▼
手指网格和指尖位置随骨链一起移动
```

关键文件：

| 阶段 | 文件 | 关键入口 |
|---|---|---|
| BLE 扫描、连接与通知 | `src/imu/wit_ble_manager.cpp` | `startScan()`、`connectToDevice()`、`handleCharacteristicChanged()` |
| WIT 数据拆帧与换算 | `src/imu/wit_protocol_parser.cpp` | `appendBytes()`、`parseFrame()` |
| UI 装配与刷新 | `src/ui/main_window.cpp` | `buildInterface()`、`updateRealImu()`、`applyPose()` |
| 单 IMU 校准与映射 | `src/motion/single_imu_finger_controller.cpp` | `calibrate()`、`update()` |
| 整链关节求解 | `src/motion/pose_solver.cpp` | `applyFingerPose()`、`solve()` |
| 骨架与蒙皮渲染 | `src/render/hand_render_widget.cpp` | `setPoseResult()`、`drawMeshes()` |
| 手指链、轴与限位 | `config/hand_rig.json` | `joints`、`fingers` |

### 2. BLE 数据如何变成姿态

`WitBleManager` 使用 Qt Low Energy API 完成设备扫描、连接、服务发现和通知订阅：

- 服务 UUID：`FFE5`；
- 通知特征：`FFE4`；
- 写入特征：`FFE9`；
- 只把名称以 `WT` 开头的设备加入候选列表；
- 连接后每 500 ms 轮询磁场、电量、温度和版本寄存器；
- 实时姿态由通知特征持续推送。

`WitProtocolParser` 不假设一次 BLE 通知恰好对应一帧。它先把数据追加到缓存，在噪声中查找 `0x55 0x61` 或 `0x55 0x71` 帧头，再按 20 字节取帧，因此能够处理分包、粘包和帧前噪声。

运动帧 `0x61` 中的原始有符号 16 位整数按 WIT 量程换算：

```text
acceleration = raw / 32768 × 16 g
angularSpeed = raw / 32768 × 2000 °/s
angle        = raw / 32768 × 180 °
```

UI 保存最新的 `ImuData`，并将欧拉角转换为四元数：

```cpp
QQuaternion raw = QQuaternion::fromEulerAngles(angleX, angleY, angleZ);
```

### 3. 临时校准如何消除初始朝向

用户伸直目标手指并点击“临时校准”时，`SingleImuFingerController::calibrate()` 保存当前四元数 `qZero`。之后每一帧都只计算相对校准时刻的变化：

```text
qRelative = inverse(qZero) × qRaw
```

所以 IMU 在校准时不需要处于固定的世界朝向；只要校准后传感器没有相对指尖滑动，相同姿态就会得到单位相对旋转，模型不会在启用驱动时突然跳动。

每根手指还可以在 `config/hand_rig.json` 中配置 `sensorCorrection`，把传感器安装坐标系转换到手指解剖坐标系：

```text
qCorrected = qSensorToTip × qRelative × inverse(qSensorToTip)
```

当前配置使用单位四元数。如果真实佩戴时出现轴交换或方向相反，应调整对应手指的 `sensorCorrection`、`sensorFlexionAxis` 和 `sensorAbductionAxis`，而不是修改 BLE 解析代码。

### 4. 从指尖旋转提取屈伸和张合

`SingleImuFingerController::update()` 使用 swing-twist 思路分两步提取旋转：

1. 沿 `sensorFlexionAxis` 提取有符号 twist，得到 `flexionDegrees`；
2. 从总旋转中移除屈伸旋转；
3. 再沿 `sensorAbductionAxis` 提取剩余 twist，得到 `abductionDegrees`。

```text
flexion  = signedTwist(qCorrected, flexionAxis)
qRemain  = qCorrected × inverse(axisAngle(flexionAxis, flexion))
abduction = signedTwist(qRemain, abductionAxis)
```

当前实现把张合限制在配置范围的 70%，避免单传感器噪声或轴耦合造成过大的侧摆：

```text
abduction ∈ [sensorAbductionMin × 0.7,
             sensorAbductionMax × 0.7]
```

屈伸不会直接等同于某个关节角，而是先转换为整条骨链的归一化弯曲量 `curl`：

```text
chainCapacity = Σ(couplingᵢ × jointFlexionLimitᵢ)
effectiveFlexion = max(0, flexion - sensorMinDegrees)
curl = clamp(effectiveFlexion / chainCapacity, 0, 1)
```

这里使用的是骨链实际可分配的总屈伸容量，而不是把传感器角度简单映射到单个关节。

### 5. 单个指尖观测如何驱动整条手指

指尖 IMU 测到的是整条手指累计后的末端旋转，无法唯一拆分为多个关节角。`PoseSolver::applyFingerPose()` 因此采用配置驱动的受约束耦合：

```text
jointFlexionᵢ = curl × couplingᵢ × jointFlexionLimitᵢ
```

- 屈伸按照每个关节的 `coupling` 分配到整条骨链；
- 张合角只加到该手指的第一个基部关节；
- 每个关节的锁定轴始终归零；
- 每个开放轴最终截断到 `minDegrees..maxDegrees`；
- 每一帧都从绑定姿态重新求解，不在上一帧角度上积分，因此不会产生数值累积漂移；
- 未选中的四根手指保持绑定姿态。

例如食指由以下骨链驱动：

```text
Bone.001 -> Bone.011 -> Bone.012 -> Bone.013
```

其中末端控制骨 `Bone.013` 的 `coupling` 为 `0`，用于保持模型既有层级，不额外分配屈伸角。五根手指的骨链、轴、限位和耦合系数都集中定义在 `config/hand_rig.json`。

### 6. 骨骼旋转如何变成指尖位移

`PoseSolver::solve()` 为每根骨骼保留 FBX 的绑定局部变换 `bindLocal`，只在其上追加受约束的局部旋转：

```text
localMatrix = bindLocal × rotation(appliedEulerDegrees)
globalMatrix = parentGlobalMatrix × localMatrix
```

这一步是标准的前向运动学。父关节旋转后，所有子骨骼的全局矩阵都会随层级传播，因此末端骨骼的空间位置自然变化。也就是说，指尖“移动”来自整条父子骨链的旋转传播，而不是直接修改指尖坐标。

同时，代码不修改骨骼的绑定平移和缩放，所以骨长不会因输入而改变。

### 7. 骨骼姿态如何驱动网格

`HandRenderWidget::drawMeshes()` 为每个网格、每根骨骼构造蒙皮矩阵：

```text
globalInverse
× animatedBoneGlobal
× meshBoneOffset
× meshBindTransform
```

每个顶点最多保存 4 个骨骼索引和对应权重。矩阵数组作为 `uBones[128]` 上传到 GLSL 顶点着色器，顶点位置和法线按权重混合。于是骨骼末端移动时，受对应骨骼影响的手指表面和指尖网格会一起变形。

这里为每个网格分别生成骨骼调色板，是因为 FBX 中不同蒙皮网格可能拥有不同的 `bone offset` 和节点绑定变换；共用一套矩阵会导致指甲分离、比例错误或远端网格飞出。

## 真实 IMU 使用流程

1. 将 WIT IMU 固定在目标手指的末端指节，确保运动过程中不会滑动。
2. 启动程序，在右侧模式列表选择“真实单 IMU”。
3. 点击“扫描 WT 设备”，选择设备后点击“连接”。
4. 等待状态显示“设备已连接”，确认角度和帧计数持续更新。
5. 在手指下拉框中选择实际佩戴的手指。
6. 保持手掌参考方向稳定，让目标手指自然伸直。
7. 点击“临时校准”。
8. 勾选“启用驱动”，活动目标手指并观察模型。
9. 如果传感器发生滑动、零位变化或动作方向错误，停止驱动后重新校准。
10. 使用完毕后点击“断开”。

状态门禁由 `SingleImuFingerController` 保证：

```text
Disconnected
  -> ConnectedUnbound
  -> BoundUncalibrated
  -> Ready
  -> Driving
```

未连接、未绑定或未校准时不能启用驱动；换手指、断开设备、重置姿态都会停止驱动并清除校准。时间戳不递增、四元数非有限或接近零模的帧不会覆盖上一有效姿态。

## 构建与运行

### 环境

- Windows 10/11
- Visual Studio 2022 / MSVC x64
- CMake 3.24 或更高版本
- Qt 6.8，包含 Core、Gui、Widgets、Bluetooth、OpenGL、OpenGLWidgets、Test
- 支持 OpenGL 3.3 Core 的桌面图形环境
- 首次配置时可访问 GitHub，以便 CMake 下载 Assimp 5.4.3

### Debug 构建

在仓库根目录执行：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug -j8
./build/bin/Debug/hand_rig_demo.exe
```

如果 CMake 无法找到 Qt：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH="C:/Qt/6.8.3/msvc2022_64"
```

### Release 构建

```powershell
cmake --build build --config Release -j8 --target hand_rig_demo
./build/bin/Release/hand_rig_demo.exe
```

程序默认加载：

- 模型：`models/3D带骨架手模.fbx`
- 骨架配置：`config/hand_rig.json`

### 启动参数

```text
hand_rig_demo.exe [options]

--model <path>  指定 FBX 模型
--smoke-test    完成首帧和自动交互检查后退出
-h, --help      显示帮助
```

示例：

```powershell
./build/bin/Debug/hand_rig_demo.exe --model "models/3D带骨架手模.fbx"
./build/bin/Debug/hand_rig_demo.exe --smoke-test
```

Smoke test 成功时会更新：

- `outputs/render_regression_bind.png`
- `outputs/render_regression_moved.png`

## 三种输入模式

### 手动关节

从骨骼树或 3D 视图选择可编辑骨骼，通过右侧数值控件或旋转操纵器修改开放轴。所有输入仍会经过锁轴和角度限位。

### 六路 IMU 模拟

用掌心和五指共六个逻辑姿态槽位测试相对掌心映射、五指耦合、60 Hz 自动播放和无效样本策略。此模式不需要真实硬件。

### 真实单 IMU

连接一枚 WIT BLE IMU，将其绑定到一根手指，以临时零姿态作为固定参考驱动该手指。该模式不会伪造掌心 IMU，也不会把单设备数据送入要求掌心有效的六路映射器。

## 相机与界面操作

| 操作 | 行为 |
|---|---|
| 左键拖动空白区域 | 轨道旋转 |
| 中键拖动 | 平移 |
| `Shift` + 左键拖动 | 平移 |
| 鼠标滚轮 | 缩放 |
| `F` | 适配模型视角 |
| 工具栏“适配视角” | 适配模型视角 |
| 工具栏“重置姿态” | 恢复绑定姿态并清除 IMU 校准 |

## 测试

执行全部自动测试：

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

主要覆盖：

- `model_importer`：真实 FBX 导入、骨架层级、蒙皮权重和逐网格绑定变换；
- `handdemo_motion`：配置校验、锁轴、限位、骨长保持、整链耦合和单 IMU 状态机；
- `wit_protocol`：20 字节运动帧与寄存器帧、分包、粘包、噪声和量程换算；
- `--smoke-test`：OpenGL 首帧、自动关节操作、矩阵有限性和骨长检查。

建议在修改导入、运动映射或渲染后执行：

```powershell
cmake --build build --config Debug -j8
ctest --test-dir build -C Debug --output-on-failure
./build/bin/Debug/hand_rig_demo.exe --smoke-test
```

Smoke test 需要能够创建窗口和 OpenGL 3.3 Core 上下文，不适用于纯无头环境。

## 目录结构

```text
.
├── CMakeLists.txt
├── config/
│   └── hand_rig.json             # 手指骨链、轴、限位、耦合与安装修正
├── models/
│   └── 3D带骨架手模.fbx
├── src/
│   ├── app/                       # 程序入口与模型/求解器装配
│   ├── core/                      # 模型、网格、骨骼和蒙皮数据结构
│   ├── import/                    # Assimp FBX 导入
│   ├── imu/                       # WIT BLE 管理与协议解析
│   ├── motion/                    # 姿态映射、单 IMU 控制器与骨骼求解
│   ├── render/                    # OpenGL 蒙皮、骨架、拾取与相机
│   └── ui/                        # 三种模式和实时状态界面
├── tests/                         # 导入、运动和协议测试
├── document/                      # 单 IMU 设计与验收文档
├── 开发文档/                      # 各工作包实现记录
├── figures/                       # 架构和交互流程图
└── outputs/                       # Smoke 截图与诊断产物
```

## 当前限制

- IMU 只提供末端姿态，不提供指尖三维位置；项目没有实现位置追踪或位置 IK。
- 单 IMU 无法唯一恢复各指关节真实角度，当前结果来自配置化整链耦合。
- 临时校准使用固定世界参考；校准后如果手掌整体转动，模型会把这部分变化也解释为手指运动。
- 当前没有掌心 IMU，因此不能动态消除手掌整体姿态。
- 当前没有低通、互补、卡尔曼等姿态滤波；输入主要依赖设备自身姿态输出、角度限位和张合安全缩放。
- 当前没有自动重连和运行时三轴安装向导。
- IMU 相对指尖滑动后必须重新校准。
- 只支持一个真实设备同时驱动一根手指；六路模式目前是软件模拟。
- FBX 外部纹理不在仓库中，渲染使用可诊断的纯色材质。
- 当前主要验证平台为 Windows，Linux 和 macOS 未纳入验收。
- GLSL 骨骼数组上限为 128，当前模型包含 20 根骨骼。

## 延伸文档

- [单蓝牙 IMU 指尖联动设计](document/单蓝牙IMU指尖联动设计.md)
- [单蓝牙 IMU 指尖联动验收文档](document/单蓝牙IMU指尖联动验收文档.md)
- [工作包 E：单 IMU 核心与指尖映射](开发文档/工作包E_单IMU核心与指尖映射.md)
- [工作包 F：真实 IMU 界面与集成](开发文档/工作包F_真实IMU界面与集成.md)
- [完整设计文档](设计文档.md)
- [验收文档](验收文档.md)
- [模型来源说明](models/SOURCE.md)
