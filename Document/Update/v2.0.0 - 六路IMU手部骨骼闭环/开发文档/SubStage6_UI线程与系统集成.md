# SubStage 6：UI、线程与系统集成

- 所属 Stage：SubStage 6
- 依赖前置：等待 SubStage 2、3、4、5 全部完成并通过各自测试
- 并行状态：需等待依赖，不可提前集成
- 所属阶段：阶段三·开发

## 1. 背景与目标

将输入、录制、融合、观测、骨骼、GLB 渲染和诊断接入一个纯 C++/Qt Widgets 产品 `HandSkeletonStudio.exe`，完成真实硬件和回放闭环、线程生命周期、UI 可观察性与启动测试。

## 2. 当前代码状态

- 旧 C++ UI 位于产品工程 `src/main_window.*`、`src/sensor_panel.*`，主要展示六路融合并含 SlimeVR UI。
- Python UI 的行为参考包括 3D 视图、串口、录制、读数和图表，但不得保留 Python 依赖。
- archived `src/ui/main_window.*` 和 `src/render/hand_render_widget.*` 可提供 Qt/OpenGL 组件参考。

## 3. 修改范围

- 新增/重构 `src/ui/main_window.*`、串口/回放、录制、六路状态、手指观测和诊断面板。
- 新增 worker QObject/QThread 编排器 `src/app/runtime_controller.*`。
- 接入 `HandRenderWidget`、`SessionRecorder` 和完整算法链。
- 移除产品中的 SlimeVR 菜单、设置和运行时连接。
- 新增 UI 离屏测试、线程退出测试、应用启动参数和启动脚本。

## 4. 线程与队列契约

- GUI 线程只操作 QWidget/OpenGL context。
- 单一算法 worker 线程执行数据源、解析、分组、校准、融合、观测和骨骼；录制可用独立 writer thread。
- 跨线程信号为 queued connection，传递不可变值对象或共享只读对象。
- 原始写队列、骨骼帧队列均有上限；UI 落后时丢旧保新并计数。
- 退出顺序：停止数据源→断开生产信号→停止录制并 flush→请求 worker 退出→等待完成→销毁 UI/GL 资源。
- 禁止销毁仍运行的 QThread。

## 5. UI 功能

- 数据源：串口/回放选择、端口、连接、断开、重连、回放速度、暂停和逐组。
- 校准：原始参数、启动零偏、中立/安装校准分离；相机重置独立。
- 状态：六路帧率、有效性、融合模式、静止、磁健康、校准和置信度。
- 手指：flexion/abduction/twist、Estimated/Held/Recovered。
- 模型：蒙皮、骨架覆盖、相机控制、显示开关和 FPS。
- 录制：开始、暂停、继续、停止、时长、帧数、字节数和目录。
- 诊断：CRC、丢组、pending、队列丢弃、延迟和最近错误。

## 6. 启动参数

至少支持：

- `--source serial|replay|demo`
- `--port COMx`
- `--replay <session-dir>`
- `--model <glb>`
- `--config <json>`
- `--auto-exit-ms <n>` 用于启动测试
- `--screenshot <png>` 用于验收证据

默认模型和配置从发布目录解析，不使用 Python 打包路径。

## 7. 单元与集成测试

- 离屏创建主窗口、目标 GLB 加载和关闭。
- 数据源切换不会残留线程或重复信号。
- UI 落后时队列有界、丢旧保新。
- 录制暂停/恢复按钮状态和关闭窗口自动 stop。
- 手腕失效、单指 held、磁干扰等诊断正确显示。
- 相机重置不改变算法配置。
- 退出后所有 QThread 均停止。
- 回放固定数据可自动启动、生成截图并正常退出。

## 8. 启动测试

必须实际运行 `HandSkeletonStudio.exe`：

1. demo 或固定回放启动，加载 GLB、显示骨架和蒙皮、截图、正常退出。
2. 真实六路串口启动，确认六路持续更新，执行中立校准和逐指动作。
3. 录制至少一段真实数据，停止后从同一会话回放。
4. 断开/重连串口，验证线程和状态恢复。
5. 运行 30 分钟并记录内存、队列、pending、延迟和 FPS。

证据写入 SubStage 完成报告：命令、时间、操作、日志片段、截图路径和统计。

## 9. 验收标准

- 产品构建、运行和测试无 Python。
- 真实六路原始 IMU 驱动目标 GLB 手模。
- UI 30–60 FPS，算法目标频率不被 UI 阻塞。
- 从完整组到骨骼帧处理延迟目标<10 ms。
- 30 分钟无崩溃和无界增长。
- 自动化测试与启动测试均通过并有证据。

## 10. 禁止事项

- 不新增 SlimeVR 网络 UI。
- 不在 GUI 线程执行串口读取、融合或同步磁盘写。
- 不以编译通过替代启动测试。
- 不静默吞掉模型、线程或录制错误。

## 11. 完成报告要求

报告修改文件、构建/测试、启动测试步骤、真实硬件端口、截图/日志、性能统计、线程退出证据和任何未达指标。