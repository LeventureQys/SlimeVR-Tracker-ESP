# SubStage 4 任务书：集成与启动

- **所属 Stage**：阶段三·开发 / 工作包 D
- **依赖前置**：**等待依赖（在 `app/gltf/loader.py`、`app/hand_pose.py`、`tools/serial_live.py`、`app/hand_view.py`、`app/serial_panel.py`、`app/readout_panel.py`、`app/chart_panel.py`、`app/styles.py`、`app/demo_source.py` 获取全部）**。SubStage 1/2/3 全部完成后开始。
- **并行状态**：需等待（集成点）。
- **契约来源**：`Document/v1.0 C++化/设计文档.md` 第 4 节（线程模型/数据流）、6.1–6.6 节、7 节（UI 装配）、9 节（启动测试）、11 节（禁止事项）。开工前先通读第 1、2、4、6、7、9、11 节。

## 0. 背景与目标

把 SubStage 1/2/3 的部件装配为完整桌面应用并落地启动链路：

1. `app/main_window.py`：主窗口（左 3D 视口 + 右 390px 面板），接线串口会话 ↔ 面板 ↔ 3D 手；
2. `app/main.py`：应用入口（QLockFile 单实例、资产加载、主题、命令行证据参数）；
3. `启动上位机.bat` 改写与 `README.md` 更新；
4. 集成自测：真实窗口启动测试 + 截图证据通道（`--screenshot`），走通"模拟数据源 → 3D 手动 → 读数/曲线更新"核心路径。

## 1. 环境

- 工作目录：`D:\workshop\Processing\wit-imu-sensor-glove-prototype`
- 运行环境：`.venv\Scripts\python.exe`（Python 3.14.4；PySide6==6.10.3）
- 启动：`.venv\Scripts\python.exe -m app.main`
- 测试：`.venv\Scripts\python.exe -m pytest tests/ -q`（全仓）；构建检查：`compileall -q app`

## 2. 现状要点

- web 端装配语义（`web/js/viewer.js`）：启动 `refreshSerialPorts({autoConnect:true})`（仅一个真实端口且无"模拟数据"占用时自动连接，304 行）；`buildFingerRows()` 后 `resizeRenderer()`；帧应用 `renderFrame`（117–150 行）；live 窗口 `LIVE_CHART_MAX=400`；状态文案映射 248–271 行；FPS 700ms 更新（412–417 行）。
- 资产：`app/assets/generic-hand-left.glb`（SubStage 1 已复制并校验 SHA-256）。
- frame dict 契约：设计文档第 4 节。

## 3. 交付文件清单

```
app/main_window.py     # MainWindow
app/main.py            # 入口 + CLI 参数
app/frame_window.py    # FrameWindow 纯数据类：push 上限 400、index 语义、最新帧（可 headless 单测）
tests/test_frame_window.py
启动上位机.bat         # 改写
README.md              # 更新（现状表/目录结构/快速开始/架构图；web 标记为"旧版（验收后归档）"）
```

## 4. 实现要点

### 4.1 app/frame_window.py

- `FrameWindow(max_len=400)`：`push(frame) -> None`（超限丢头）、`frames() -> list`、`latest() -> dict|None`、`index_of_latest() -> int`、`clear()`。窗口语义与 `viewer.js` 235–237、273–278 行一致（帧计数 = `index+1 / len`）。

### 4.2 app/main_window.py

- `MainWindow(QMainWindow)`：
  - 构造：`SerialLiveSession()`；`GltfLoader` 加载 `app/assets/generic-hand-left.glb`（`_bundle_dir()` 式路径解析：`Path(__file__).resolve().parents[1]`）；`HandPoseModel(asset)`；`HandViewWidget(asset)`；三面板；`FrameWindow`。
  - 布局：中央 QWidget 水平布局：左侧视口容器（`HandViewWidget` 全铺 + 透明叠加层：标题区/状态徽章/FPS、工具按钮行（重置视角/软组织/网格，后两者 QCheckButton 默认 checked）、轴图例 X/Y/Z；叠加层 QLabel 设 `WA_TransparentForMouseEvents`，按钮不设）+ 右侧固定 390px 面板（QScrollArea，垂直容纳四节 + 页脚"上位机只呈现主控输出的姿态结果"）。
  - 应用 `app/styles.py` 的 GLOBAL_QSS（在 main.py 或 MainWindow 构造时 `setStyleSheet`）。
  - 接线：
    - `SerialPanel.connectRequested → 连接（port=='demo' ? session.start_demo() : session.start(port, baud)，异常 → set_status error 文案）`；断开 → `session.stop()` + 面板 reset + 场景状态"等待数据"；标定 → `session.request_calibrate()`（异常提示）；刷新 → `session.list_ports()` → `set_ports`（异常显示"刷新端口失败：..."）；
    - `QTimer(33ms)`：`for f in session.drain_frames(): frame_window.push(f); chart.push_frame(f)`，若批非空：`last = frame_window.latest()` → `angles = {name: {'bendDeg': float(f['bend_deg']), 'swayDeg': display_sway_deg(name, f['sway_deg'])}}` → `view.set_pose(pose_model.apply_pose(last['wrist_quaternion_wxyz'], angles))` → `readout.apply_frame(last, frame_window.index_of_latest(), frame_window.frames())` → `chart.mark_current(...)`；
    - `QTimer(250ms)`：`session.status_dict()` → `serial_panel.set_status`、状态徽章（`phase_badge_text`）、状态点亮（live/calibrating）、场景状态文案（live→"主控姿态控制"、calibrating→"串口标定中"、其余不覆盖模型加载文案）；连接成功前场景状态"等待数据"，模型就绪后"真实手模型就绪"；
    - `ReadoutPanel.fingerSelected → chart.set_finger(name) + chart.set_frames(frame_window.frames()) + mark_current`（复刻 `viewer.js` 104–111 行）；
    - `view.fpsChanged → FPS 标签 "NN FPS"`；
    - 视口按钮：重置视角 → `view.reset_view()`；软组织 → `view.set_skin_visible(checked)`；网格 → `view.set_grid_visible(checked)`。
  - 关闭事件：`session.stop()`。
  - 启动顺序：`refresh_ports(auto_connect=True)`（auto_connect 条件：真实端口恰 1 个且未选中"模拟数据"）。

### 4.3 app/main.py

- `sys.path` 注入 `tools/`（import `serial_live`、`processed_pipeline`）。
- `QApplication` + `QLockFile(QDir.tempPath() + '/灵巧手上位机.lock')`：锁定失败 → 弹 QMessageBox"上位机已在运行" + `sys.exit(0)`。
- 默认 `QSurfaceFormat`：samples 4（渲染器需要，此处兜底）。
- CLI：`--demo`（启动后自动连接模拟数据源）、`--screenshot PATH`（连接成功后延时 2s 抓 `QScreen.grabWindow` 存 PNG，供验收证据）、`--quit-after MS`（延时退出，配合截图自动化）、`--no-demo` 无操作占位（默认无自动连接，仅 auto_connect 逻辑）。
- 资产加载失败 → 错误对话框 + 退出码 1（文案含异常信息，复刻 web "手模型加载失败"语义）。
- `if __name__ == '__main__': raise SystemExit(main())`。

### 4.4 启动上位机.bat（改写）

- 保持现有 venv 引导/依赖安装/失败提示结构（`启动上位机.bat` 1–49 行），仅改：依赖提示文本（pyserial + PySide6）、运行命令改为 `.venv\Scripts\python.exe -m app.main`、提示语去掉浏览器 URL（"桌面窗口即上位机界面；关闭本窗口即停止"）、Python 版本提示改为 3.9+。

### 4.5 README.md（更新）

- 现状表：桌面应用行替换 web 行（新增 PySide6 桌面应用可用；`tools/serve_app.py` 与 `web/` 标注"旧版对照，验收后归档"）；目录结构补 `app/` 与 `tools/serial_live.py`；快速开始改桌面启动；架构图改为 4 节线程模型；串口协议/HTTP API 章节：协议保留，HTTP API 标注旧版；依赖表加 PySide6==6.10.3。

## 5. 验收标准（本包完成即判定）

1. `compileall -q app` 退出码 0；`pytest tests/ -q` 全仓全绿（含 test_frame_window：上限丢头、index 语义、latest）；
2. **启动测试（强制，本机真实窗口）**：`python -m app.main --demo --screenshot 证据.png --quit-after 8000` 正常启动无异常退出；截图显示：深色主题主窗口、左 3D 手（bind 或运动姿态）+ 网格 + 骨骼覆盖层、右四节面板、状态徽章"处理实时"、读数非零、曲线有两条轨迹、FPS 显示数值；单实例验证（第二次启动弹"已在运行"）；stderr 无 traceback；
3. 完成后把截图与启动日志附入完成报告；
4. 完成报告：文件清单、pytest 输出原文、启动测试操作记录与截图说明、与任务书偏差。

## 6. 禁止事项

- 见设计文档 11 节；另：不得修改 SubStage 1/2/3 已交付模块（只消费契约，发现问题报给总指挥重规划）；不得改 AA55 协议；不得引入新依赖；不得改 `web/` 与 `tools/serve_app.py`。

## 7. 完成报告

在最终回复中输出：文件清单、pytest 完整输出、启动测试记录（含截图路径与现象描述）、偏差说明。
