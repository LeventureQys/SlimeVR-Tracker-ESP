# SlimeVR-Tracker-ESP（Leventure 分支）— 当前主线：HandSkeletonStudio

> 本仓库当前的主要开发目标是 **HandSkeletonStudio（六路 IMU 手部骨骼闭环桌面软件）**。
> 原 SlimeVR 追踪器 ESP 固件保留为硬件平台与算法参考；旧 MVP 与 SlimeVR 网络链路不再进入产品主线。

## 当前项目

**HandSkeletonStudio**（`Document/Research/手套IMU解算MVP/`）：纯 C++20 / Qt 6 Windows 桌面应用。

```text
六路手套 IMU 原始九轴数据（AA 55 帧，921600/8N1）
  → 协议解析与六路同步 → 校准（量程/轴/零偏/磁）
  → 姿态融合（Madgwick / VQF） → 掌指相对姿态与安装修正（含调零）
  → 虚拟层级骨骼解算 → HandSkeletonFrame → 25 关节 GLB 手模 GPU 蒙皮显示
  → 全程可录制 / 回放（raw.bin + jsonl，确定性验证）
```

- 零 Python 依赖；产品不链接 SlimeVR 网络与遗留库。
- 单元测试 18/18 通过；demo / 回放 / 真实串口（COM12）三种数据源实测可用（≈198 组/s、0 CRC 错误）。
- 入口：`Document/Research/手套IMU解算MVP/README.md`；发布目录 `dist/HandSkeletonStudio/`（构建后生成，不入库）。
- 版本文档：`Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/`（需求、设计、开发任务书、验收报告、用户测试指南）。

## 仓库结构

```text
├─ boards/ include/ lib/ src/ test/ scripts/ ci/   ESP 固件（参考硬件平台）
├─ Document/                                      软件开发与研究内容
│  ├─ Research/手套IMU解算MVP/                     ★ 当前主线项目
│  │  ├─ src/ tests/ tools/ assets/ cmake/         当前主线代码与测试
│  │  ├─ docs/                                     项目文档（架构、状态、测试指南）
│  │  ├─ legacy/                                   旧 SlimeVR MVP 参考归档
│  │  └─ dist/ build-*/                            发布与构建产物（不入库）
│  ├─ Develop/v2.0/手套姿态解算软件/               旧 PySide6 手部显示软件（参考）
│  ├─ Wit 手套资料/archived/                       FBX/Assimp 骨骼研究原型（参考）
│  └─ Update/v2.0.0 - 六路IMU手部骨骼闭环/         版本流程文档
└─ platformio.ini 等                               固件构建配置（未改动）
```

## 固件说明（原 README 保留内容）

本仓库 fork 自 [SlimeVR/SlimeVR-Tracker-ESP](https://github.com/SlimeVR/SlimeVR-Tracker-ESP)。ESP8266/ESP32 追踪器固件及其 IMU 支持列表、传感器校准步骤见上游文档。当前项目将固件视为手套 IMU 数据采集的硬件平台与算法参考（VQF、静止检测、零偏估计），不依赖 SlimeVR Server 网络协议。
