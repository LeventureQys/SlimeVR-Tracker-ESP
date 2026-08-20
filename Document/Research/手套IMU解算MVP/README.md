# HandSkeletonStudio（六路 IMU 手部骨骼闭环）

## 项目定位

本目录（`Document/Research/手套IMU解算MVP`）是 **V2.0.0 当前主线项目**：一个纯 C++20 / Qt 6 的 Windows 桌面应用，接收手套上六颗 IMU（手腕 + 五指）的原始九轴数据，完成协议解析、校准、姿态融合、掌指相对姿态、骨骼解算，并实时驱动 25 关节 GLB 手模的 GPU 蒙皮显示。支持录制与回放，可用于动作复核与算法对比。

旧 SlimeVR 时代的 MVP（含 SlimeVR UDP 输出）已整体归档到 `legacy/` 作为**参考项目**；SlimeVR 对本项目的意义仅是**算法参考**（VQF 融合、静止检测、零偏思路），不接入其网络链路。

## 快速开始

### 直接运行（发布目录，无需构建）

```text
dist\HandSkeletonStudio\
├─ 启动-主界面.bat          打开软件后在界面里选数据源
├─ 启动-真实手套COM12.bat   直接以 COM12 串口启动
└─ 启动-演示模式.bat        合成数据演示
```

### 构建与测试

```powershell
cmake -S . -B build-final -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH="D:\Devtools\Qt\6.8.3\msvc2022_64"
cmake --build build-final --config Release
$env:PATH = "D:\Devtools\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build-final -C Release --output-on-failure
```

当前状态：**18/18 单元测试通过**；demo/回放/真实串口三种数据源实测可用（COM12 真实六路设备 ≈198 组/s、0 CRC 错误）。

## 目录结构

```text
├─ CMakeLists.txt            产品构建入口（当前主线 + 遗留参考目标）
├─ README.md                 本文件
├─ docs/                     当前项目文档（架构、测试指南、状态）
├─ assets/                   目标 GLB、骨骼配置、默认运行配置
├─ src/                      当前主线源码
│  ├─ core/                  统一数据契约与元类型注册
│  ├─ config/                运行时配置加载校验
│  ├─ protocol/              25 字节帧解析与六路同步（handstudio::）
│  ├─ input/                 串口 / 回放统一数据源
│  ├─ recording/             录制、元数据、回放控制（有界队列）
│  ├─ calibration/           量程/轴/零偏/磁校准
│  ├─ fusion/                IFusionFilter、Madgwick、VQF、磁健康、保护
│  ├─ hand/                  掌指相对姿态、安装修正、方向分解、调零
│  ├─ skeleton/              虚拟层级 FK、限位耦合、palette 映射
│  ├─ model/                 GLB 导入与模型契约
│  ├─ render/                OpenGL 蒙皮与骨架覆盖层
│  ├─ ui/                    主窗口与面板
│  └─ app/                   main、运行时编排、demo 源
├─ tests/                    当前主线 QtTest（18 项注册）
├─ tools/                    hardware_baseline_capture（真实硬件基线采集）
├─ cmake/                    各模块自包含 CMake（substage2~6.cmake）
├─ testdata/                 录制会话与真实硬件数据集
├─ dist/                     可直接运行的发布目录
├─ build-final/              验收用干净构建目录
└─ legacy/                   【参考】旧 SlimeVR MVP 归档
   ├─ docs/                  旧 MVP 需求/设计/开发/验收文档
   ├─ src/                   旧协议、融合、SlimeVR 网络等源码
   ├─ tests/                 旧 MVP 测试
   └─ tools/                 旧硬件探针
```

## 参考项目与资产

| 位置 | 内容 | 用途 |
| --- | --- | --- |
| `legacy/` | 旧 SlimeVR MVP（协议/融合/SlimeVR UDP/旧 UI） | 参考实现；遗留测试仍编译验证 |
| `Document/Wit 手套资料/archived/` | FBX 骨骼/Assimp/OpenGL 研究原型 | 算法与矩阵处理参考 |
| `Document/Develop/v2.0/手套姿态解算软件/` | 旧 PySide6 手部显示软件 | GLB 行为契约参考（Python，不参与构建） |
| 仓库根 `lib/vqf/` | SlimeVR-ESP 固件 VQF（MIT） | 算法来源；已 vendor 到 `src/fusion/vqf/` |

## 版本文档

V2.0.0 的需求、问题清单、设计、开发任务书、验收文档与验收报告位于：

```text
Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/
```

当前验收状态：**限条件通过**——软件闭环与真实数据闭环已实测通过；受控静置漂移、逐指动作矩阵、30 分钟耐久与硬件量程/轴向确认仍需补验（详见 `docs/开发与验收状态.md`）。
