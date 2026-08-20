# SubStage 6 完成报告：UI、线程与系统集成

- 所属 Stage：SubStage 6
- 所属阶段：阶段三·开发
- 工程根：`Document/Research/手套IMU解算MVP`
- 构建目录：`build-s6`
- 执行结果：代码、Release 构建、18/18 CTest、demo/replay 启动截图完成；真实串口数据因 COM12 沙箱访问限制未采集；30 分钟耐久测试未执行。

## 1. 结论摘要

| 验收项 | 结果 |
| --- | --- |
| 新 Qt Widgets 主窗口、数据源/校准/六路状态/手指/诊断/录制面板 | 完成 |
| 独立算法 worker QThread，GUI 仅操作 QWidget/OpenGL | 完成 |
| serial/replay/demo 统一 `IDataSource::bytesReady` 链路 | 完成 |
| 产品链接 handstudio_core/io/model/render，不链接 six_imu_core/network | 完成 |
| raw.bin + diagnostics + fused/observation/skeleton JSONL | 完成 |
| globalMatrix/skinMatrix 语义冻结与 bind pose 测试 | 完成 |
| Release 全量构建 | 完成 |
| 全量 CTest | 18/18 通过（既有 17 + 新增 1） |
| demo 启动、截图、退出码 0 | 完成 |
| replay 固定会话启动、截图、退出码 0 | 完成 |
| serial COM12 | 程序可启动并自动退出；硬件访问沿用 SubStage2 明确拒绝访问证据，本环境无真实数据 |
| 30 分钟耐久 | 未执行，列为遗留风险 |

## 2. 文件清单

### 2.1 新增

- `src/app/demo_data_source.h/.cpp`：生成合法 25 字节六路 raw 帧，经统一输入契约进入产品链。
- `src/app/runtime_controller.h/.cpp`：模型/rig 初始化、数据源切换、worker QThread、协议/校准/融合/观测/骨骼编排、录制与退出顺序。
- `src/ui/main_window.h/.cpp`：数据源、回放、校准、六路状态、五指观测、诊断、录制控制和 `HandRenderWidget` 集成。
- `assets/default_runtime.json`：发布目录默认运行时配置。
- `cmake/substage6.cmake`：产品目标接线、Widgets/OpenGL 依赖、手工 moc、资产复制和测试注册。
- `tests/test_app_integration.cpp`：离屏主窗口、相机重置算法隔离、demo 完整链和线程退出测试。
- `testdata/substage6-demo-session/`：由产品 demo 源实际录制的固定回放会话。
- `Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/开发文档/SubStage6_demo.png`。
- `Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/开发文档/SubStage6_replay.png`。

### 2.2 修改

- `src/app/main.cpp`：完整 QApplication 入口和命令行参数；默认资产从 EXE 发布目录解析；自动截图/退出；附加 `--record-dir` 仅用于自动化生成固定回放会话。
- `tests/test_recording.cpp`：新增三类派生流写出与有界丢弃统计测试。

## 3. 跨模块修改

MainAgent 已明确授权以下最小跨模块修改：

1. `src/recording/session_recorder.h/.cpp`
   - 新增 `appendFusedPoses`、`appendObservation`、`appendSkeletonFrame`。
   - 新增三个独立有界队列和 `fused_poses.jsonl`、`observations.jsonl`、`skeleton_frames.jsonl`。
   - raw.bin 保持最高优先级：raw 队列满时直接写入并计 overflow bytes；派生队列满时丢低优先级项并计 `derivedQueueDroppedItems`。
   - 保持既有 API 与状态机兼容，`test_recording`、`test_replay` 均通过。
2. `src/skeleton/kinematic_skeleton.cpp`
   - `rootTransform = computeDisplayRootTransform(model)`，作为记录值。
   - `globalMatrix` 统一包含显示根变换。
   - 目标 GLB 的 joint 节点是平铺结构，虚拟解剖父链不能直接重建 GLB bindWorld；因此绑定姿态以每骨 `bindWorld` 为基准，再叠加虚拟链累计旋转增量，保证 bind pose 精确不变。
3. `src/skeleton/skin_palette_mapper.cpp`
   - 冻结为 `skinMatrix = globalMatrix * inverseBind`，不重复乘 `rootTransform`。
4. `tests/test_skeleton_solver.cpp`
   - 新增非单位显示根下 global/skin 空间一致性断言；绑定姿态逐骨矩阵对齐 GLB bindWorld。

未修改 `src/render/hand_render_widget.*`；其既有契约与冻结语义一致。

## 4. 线程与队列设计

- GUI 线程：`MainWindow`、`HandRenderWidget`、表格与诊断文本。
- worker 线程：`IDataSource`、`FrameStreamParser`、`SequenceGrouper`、`CalibrationPipeline`、`FusionBank`、`HandObservationSolver`、`KinematicSkeleton`、`SessionRecorder`。
- 跨线程：Qt queued signal 传值对象 `FusedImuPose[6]`、`HandObservationFrame`、`HandSkeletonFrame` 和只读 `shared_ptr<const RiggedModel>`。
- 模型所有权：solver 持值；renderer 持 `shared_ptr<const RiggedModel>`；二者来自同一导入结果。
- 数据源切换：停止并断开旧源，销毁旧源，reset parser/grouper/calibration/fusion/observation，再创建新源。
- 退出顺序：断开并停止 source → 删除 source → recorder stop/flush → worker 发 stopped → thread quit/wait → controller 清理状态。`test_app_integration` 断言关闭后 `isWorkerRunning()==false`。
- 录制队列：raw 和三类派生流均有界；raw 溢出直接落盘，派生流溢出丢弃并计数。

## 5. skinMatrix 语义对齐与测试证据

冻结契约：

```text
globalMatrix = displayRoot × bindWorld × accumulatedDelta
skinMatrix   = globalMatrix × inverseBind
rootTransform = displayRoot（记录值，不由 renderer 二次参与）
```

- wrist 单位四元数、所有指关节零旋转时，`globalMatrix = displayRoot × bindWorld`。
- `test_skeleton_solver` 逐骨验证 global 关节位置与 GLB bindWorld 在同一显示空间，误差阈值 `1e-4`。
- `test_glb_model::bindPoseSkinnedInvariance` 继续逐顶点验证 bind pose CPU 蒙皮不变性，历史实测最大误差 `7.468e-07`，低于 `1e-4`。
- `HandRenderWidget` 使用 `skinMatrix` 画蒙皮、使用 `globalMatrix` 平移画骨架覆盖层；二者现处于同一显示空间，不再分离。

## 6. 构建与 CTest

环境：Visual Studio 17 2022 / MSVC 19.44；Qt 6.8.3 msvc2022_64；Windows SDK 10.0.26100；无 Python 调用。

配置：

```powershell
cmake -S . -B build-s6 -G "Visual Studio 17 2022" -A x64 `
  -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=D:/Devtools/Qt/6.8.3/msvc2022_64
```

Release 全量构建：

```powershell
cmake --build build-s6 --config Release
```

结果：产品、handstudio_core/io/hand/skeleton/model/render/app、工具、遗留 `six_imu_core` 与全部测试目标均构建完成。2026-08-20 最终复验中 `cmake --build build-s6 --config Release` 退出码 0，输出明确包含 `six_imu_core.vcxproj -> .../Release/six_imu_core.lib`。此前一次验收重链时 EXE 被先前串口测试进程短暂锁定产生 LNK1104；清理句柄并等待 3 秒后同一产品目标重建成功，不是源码或链接图错误。

CTest：

```powershell
$env:PATH = "D:\Devtools\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build-s6 -C Release --output-on-failure
```

真实结果：

```text
1/18  test_core_contracts ........ Passed
2/18  test_runtime_config ........ Passed
3/18  test_protocol .............. Passed
4/18  test_sequence_grouper ...... Passed
5/18  test_madgwick_filter ....... Passed
6/18  test_settings .............. Passed
7/18  test_protocol_v2 ........... Passed
8/18  test_recording ............. Passed
9/18  test_replay ................ Passed
10/18 test_calibration ........... Passed
11/18 test_fusion ................ Passed
12/18 test_glb_model ............. Passed
13/18 test_render_offscreen ...... Passed
14/18 test_hand_observation ...... Passed
15/18 test_hand_rig_config ....... Passed
16/18 test_skeleton_solver ....... Passed
17/18 test_skeleton_palette ...... Passed
18/18 test_app_integration ....... Passed
100% tests passed, 0 tests failed out of 18
Total Test time: 1.41 sec（2026-08-20 最终复验）
```

## 7. 启动测试证据

执行日期：2026-08-20（当前测试环境系统时间）。运行前 Qt 6.8.3 bin 置于 PATH 首位，OpenGL 使用 `QT_QPA_PLATFORM=windows;QT_OPENGL=desktop`。

### 7.1 demo

```powershell
HandSkeletonStudio.exe --source demo `
  --screenshot <开发文档>/SubStage6_demo.png --auto-exit-ms 5000
```

结果：退出码 0；初次截图 `1400×900`、`68298` 字节。2026-08-20 最终复验再次退出码 0，并生成 `SubStage6_demo_final.png`（`68267` 字节）。人工检查可见：GLB 蒙皮手模、黄色骨架覆盖、数据源/校准/六路/手指/录制/诊断面板。截图路径：

- `Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/开发文档/SubStage6_demo.png`
- `Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/开发文档/SubStage6_demo_final.png`

### 7.2 固定回放

固定会话来源：使用产品自身 `--source demo --record-dir ... --auto-exit-ms 3000` 实际走 `DemoDataSource → parser → grouper → algorithms → SessionRecorder` 生成，不是硬件采集。

会话路径：`Document/Research/手套IMU解算MVP/testdata/substage6-demo-session/`。

文件统计：

```text
raw.bin                  83250 bytes
diagnostics.jsonl            0 bytes
fused_poses.jsonl       674151 bytes
observations.jsonl      560625 bytes
skeleton_frames.jsonl  8689652 bytes
metadata.json              632 bytes
```

回放命令：

```powershell
HandSkeletonStudio.exe --source replay `
  --replay testdata/substage6-demo-session `
  --screenshot <开发文档>/SubStage6_replay.png --auto-exit-ms 5000
```

结果：退出码 0；初次截图 `1400×900`、`72764` 字节。2026-08-20 最终复验再次退出码 0，并生成 `SubStage6_replay_final.png`（`73016` 字节）；GLB 蒙皮与骨架显示正常。截图路径：

- `Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/开发文档/SubStage6_replay.png`
- `Document/Update/v2.0.0 - 六路IMU手部骨骼闭环/开发文档/SubStage6_replay_final.png`

### 7.3 serial COM12

命令：

```powershell
HandSkeletonStudio.exe --source serial --port COM12 --auto-exit-ms 10000
```

结果：程序正常启动并在约 `10490 ms` 后退出，退出码 0；没有销毁运行中的 QThread。

硬件状态沿用 SubStage2 同一环境、同一真实 FTDI 设备的明确证据：

```text
COM12 | USB Serial Port | FTDI | A50285BIA
BLOCKED: 无法打开串口 COM12: 拒绝访问。
Access to the path 'COM12' is denied.
```

本次 GUI 启动中错误显示在诊断面板。尝试通过 PowerShell 管道捕获 GUI stderr 会使进程管道等待并超时，按沙箱命令边界未继续换方式规避；因此报告引用同环境已有的明确拒绝访问日志，未声称采集到真实数据。2026-08-20 最终复验还运行 `hardware_baseline_capture --port COM12 --duration 1`：成功枚举 `COM12 | USB Serial Port | FTDI | A50285BIA`，随后停在“尝试打开 COM12 @ 921600 8N1 ...”并超过 15 秒超时；工具进程随后退出。该结果再次证明设备存在但沙箱设备打开不可完成。

## 8. 性能与线程退出证据

- demo/replay 自动启动期间 UI 可显示持续帧，未观察崩溃或 OpenGL 错误。
- 完整组到骨骼帧 latency 已在 RuntimeController 统计并显示，但本次报告未形成可靠分位数采样，不能给出 `<10 ms` 的量化结论。
- `test_app_integration::demoRunsAndShutdownStopsWorker` 等待收到骨骼帧，随后 `controller.stop()` 并断言 worker thread 停止。
- `test_app_integration::mainWindowCreatesAndCameraResetIsAlgorithmIndependent` 关闭窗口后断言线程停止；相机重置不改变 controller 模型路径/算法状态。
- 未执行 30 分钟耐久测试，因此无内存增长、长时队列水位和 FPS 分位数证据。

## 9. 遗留风险与未达指标

1. **真实硬件阻塞**：COM12 在当前 DSH 环境拒绝访问；未验证六路真实更新、中立校准、逐指动作、断连重连和真实录制回放，不能给出真实姿态精度结论。
2. **30 分钟耐久未执行**：任务书中的长时内存、队列、pending、延迟和 FPS 统计仍需 MainAgent 在可访问硬件与桌面环境补验收。
3. **录制 writer 实现**：派生流已使用独立有界队列和丢弃统计，但实际 flush 仍在算法 worker 线程同步执行，并非独立 writer QThread；raw 优先级和有界性已满足，极慢磁盘下算法周期仍可能受单次写调用影响。
4. **运行时配置应用范围**：`--config` 已解析路径并纳入启动契约，但当前控制器主要使用 rig 中的 mount 配置；默认 runtime JSON 的量程/安装参数尚未完整映射到 CalibrationPipeline。真实硬件验收前必须补齐该映射。
5. **回放速度 UI**：界面已提供速度控件、暂停和逐组；当前速度控件尚未接 `ReplayDataSource::setBytesPerSecond`，暂停与逐组已接通。
6. **真实中立/安装校准 UI**：入口已分离展示，启动零偏已接通；原始参数和中立/安装校准按钮尚无完整持久化对话框。

以上未达项不影响本次合成数据闭环、UI/线程、录制格式、矩阵契约与自动启动测试结论，但会影响 V2.0.0 最终真实硬件验收，需 MainAgent 纳入阶段四问题清单。
