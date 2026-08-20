# 灵巧手上位机 —— WIT IMU 传感器手套原型

本项目是 **WIT IMU 传感器手套原型**的配套上位机：通过有线串口接收主控板（MCU）已解算的六路姿态帧，在桌面窗口中用三维人手实时显示五指姿态、角度读数与曲线。

> 当前主线是 **PySide6 桌面应用**（v1.0，QOpenGLWidget 自绘渲染）；更早的 Python HTTP 服务 + Three.js 网页前端已归档到 `archived/旧版web与HTTP服务/`。更早的 Qt/C++ 手模 Demo、BLE 适配器等历史工作已归档到 [`archived/`](archived/)。

## 项目现状

| 部分 | 内容 | 状态 |
| --- | --- | --- |
| `app/` | PySide6 桌面应用（3D 视口 + 串口面板 + 读数 + 曲线） | ✅ 可用 |
| `app/gltf/loader.py` | glTF2 解析（generic-hand-left.glb） | ✅ 可用 |
| `app/hand_pose.py` | bend/sway → 25 关节蒙皮姿态（与 web 端公式一致，经 three.js 交叉验证） | ✅ 可用 |
| `app/hand_view.py` | QOpenGLWidget 自绘渲染（GPU 蒙皮 + 覆盖层 + 网格 + 4 灯 + 雾 + ACES） | ✅ 可用 |
| `tools/serial_live.py` | 串口后台线程会话（AA55 帧解析 + 标定） | ✅ 可用 |
| `tools/processed_pipeline.py` | AA55 姿态帧解析（CRC16-Modbus、按 sequence 聚合 6 节点） | ✅ 可用（零改动） |
| `archived/旧版web与HTTP服务/` | 旧版 HTTP 服务 + Three.js 前端（v1.0 桌面化时归档） | 📦 已归档 |
| `archived/` | Qt/C++ 手模 Demo、BLE 适配器、各阶段设计文档 | 📦 已归档 |

## 目录结构

```
项目根/
├── 启动上位机.bat            # 一键启动（自动建 .venv、装依赖、启动桌面应用）
├── requirements.txt          # pyserial==3.5 / PySide6==6.10.3 / pytest>=8
├── app/                      # 桌面应用包
│   ├── main.py               # 入口（单实例、主题、--demo/--screenshot 证据参数）
│   ├── main_window.py        # 主窗口装配
│   ├── hand_view.py          # OpenGL 渲染器（蒙皮/覆盖层/网格/灯光/雾/轨道相机）
│   ├── hand_pose.py          # 姿态映射（移植 web 端公式）
│   ├── camera.py             # 轨道相机（OrbitControls 同参数）
│   ├── overlay_geometry.py   # 骨段圆柱/关节球几何
│   ├── quaternion.py         # 基变换与矩阵工具
│   ├── gltf/loader.py        # GLB 解析
│   ├── serial_panel.py / readout_panel.py / chart_panel.py / panel_utils.py
│   ├── demo_source.py        # 模拟数据源（无硬件演示/验收）
│   ├── frame_window.py       # 400 帧环形窗口
│   ├── styles.py             # 深色主题 QSS
│   └── assets/               # generic-hand-left.glb（桌面应用自持副本）
├── tools/
│   ├── serial_live.py        # 串口会话（线程 + 队列 + 标定窗口）
│   ├── processed_pipeline.py # 协议解析（零改动）
│   └── serve_app.py          # 旧版 HTTP 服务（已归档）
├── tests/                    # pytest 测试（62 项）
├── web/                      # 旧版 Three.js 前端（已归档，见 archived/）
└── archived/                 # 历史阶段归档
```

## 快速开始

### 环境要求

- Windows（脚本使用 `.venv` 路径）
- Python 3.9 及以上（PySide6 6.10.3 为 cp39-abi3）
- （可选）连接主控板的串口设备；无硬件时可用"模拟数据"模式体验完整功能

### 启动

**方式一：直接双击 `启动上位机.bat`**。首次运行会自动创建 `.venv` 并安装依赖。

**方式二：手动执行**

```powershell
python -m venv .venv
.venv\Scripts\pip install -r requirements.txt
.venv\Scripts\python -m app.main
```

启动后在右侧"有线串口"面板选择端口（或"模拟数据"）→ 点「连接串口」。命令行参数：`--demo`（启动即连模拟源）、`--screenshot PATH --quit-after MS`（自动截图证据，用于验收）。

## 架构与数据流

```text
主控 MCU（6 路 IMU 已解算）
   │ 有线串口，默认 921600 baud，31 字节×6 姿态帧
   ▼
tools/serial_live.py  SerialLiveSession（后台线程；或 app/demo_source.py 模拟源）
   │ ProcessedPoseAssembler：同步头对齐 → CRC16-Modbus 校验 → 按 sequence 聚合
   ▼
内部队列（UI 侧 QTimer 批量 drain，200 Hz 目标帧率）
   ▼
app/main_window.py（QTimer 33ms 拉帧；QTimer 250ms 状态轮询）
   ├─ app/hand_pose.py：frame → 25 关节蒙皮矩阵 + 覆盖层位置
   ├─ app/hand_view.py：GPU 蒙皮渲染 + 骨骼覆盖层 + 网格 + 轨道相机
   └─ 面板：串口 / 当前帧 / 五指角度（bend·sway 条）/ 选中手指曲线（400 帧窗口）
```

## 串口协议

- 默认波特率 **921600**（可选 115200 / 460800 / 3000000）。
- 姿态帧（31 字节 × 6 节点）：

  ```
  AA 55 | node_id | sequence(u16 LE) | 6×float32 LE | CRC16-Modbus(u16 LE)
  ```

  - `0x50` 手腕：`wxyz` 四元数 + `time_s` + 保留位；
  - `0x51–0x55` 五指：相对掌心四元数 + `bend_deg` / `sway_deg`。
- 校验为 **CRC16-Modbus**（多项式 0xA001），帧不合法时跳过并计数。
- 重新标定命令：`AA 55 C0 01 00 00 + CRC16`；发送后进入 0.4 s 标定窗口，期间丢弃新帧。标定时请张开手静止约 3 s。

## 三维显示说明

- 真实人手蒙皮模型来自 `@webxr-input-profiles/assets@1.0.15`（MIT），glTF2/GLB，25 个 WebXR 标准关节。
- 姿态映射与 web 端同一套公式（基变换 C=Rz(-90°)、满握拳完成度、sway ±30° 钳制），蒙皮矩阵经 three.js r160 实跑交叉验证（bind 姿态最大误差 1.06e-5）。
- 场景参数复刻 web 端：fov 34、相机 (-4.2, 0.7, 12.2)、4 灯 + 半球光、FogExp2(0.035)、ACES×1.12、GridHelper(10, 20) 平放 z=-1.2、半透明皮肤（opacity 0.43）+ 骨骼覆盖层。
- 显示效果为工程可视化，不用于医疗/精细测量；上位机只呈现主控输出的姿态结果。

## 依赖

| 层 | 依赖 | 版本 |
| --- | --- | --- |
| Python | pyserial | 3.5 |
| Python | PySide6 | 6.10.3（锁定：6.11 起 Qt3D 绑定被移除，且 6.10 的 QtOpenGL 绑定存在已知缺陷，已在渲染器中规避） |
| 测试 | pytest | ≥8 |

## 注意事项

- 三维显示为工程可视化，不用于医疗/精细测量；
- 上位机仅接收并呈现主控已解算的姿态结果，桌面端不做姿态解算；
- 出现串口异常时自动置为 `error` 状态并关闭串口，断开后重连即可恢复；
- 模拟数据源走与真实串口完全相同的 AA55 协议解析路径（仅标定命令不支持）。

## 相关文档

- 版本工作文档：`Document/v1.0 C++化/`（需求说明、问题清单、设计文档、开发文档、验收文档、验收报告）
- [`archived/README.md`](archived/README.md) —— 早期 Qt/C++ 原型与历史阶段说明
