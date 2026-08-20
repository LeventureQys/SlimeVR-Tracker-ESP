# legacy/ — 旧 SlimeVR MVP 参考归档

本目录是 V2.0.0 之前的 SlimeVR 时代 MVP（六路原始 IMU 解算 + SlimeVR UDP 输出）的完整归档，仅作参考。

- docs/：旧 MVP 需求、设计、开发任务书、验收与 README。
- src/：旧协议解析、六路分组、Madgwick、旧 UI、SlimeVR 网络协议与发送。
- tests/：旧 MVP 测试（其中 test_protocol / test_sequence_grouper / test_madgwick_filter / test_settings 仍注册在当前 CTest 中作回归）。
- tools/：旧串口硬件探针与 SlimeVR 桥接探针。

当前主线（HandSkeletonStudio）不依赖本目录中的 SlimeVR 网络代码；仅复用其 Madgwick 纯数学核心（编译进 handstudio_core 供 MadgwickFusionFilter 包装）与协议适配层作为遗留回归目标。SlimeVR 对本项目仅是算法参考（VQF/静止检测/零偏思路）。

