# SubStage 3 任务书：UI 面板与交互

- **所属 Stage**：阶段三·开发 / 工作包 C
- **依赖前置**：契约依赖 SubStage 1 的 `tools/serial_live.py`（SerialLiveSession，契约 6.3）与 frame dict 结构（设计文档第 4 节）；运行依赖 `tools/processed_pipeline.py`（零改动）。**等待依赖（在 `tools/serial_live.py` 获取）**——纯函数与面板 UI 可先行开发，demo 源集成测试须等 SubStage 1 文件落地。
- **并行状态**：可与 SubStage 2 并行。
- **契约来源**：`Document/v1.0 C++化/设计文档.md` 6.3/6.4/6.6 节、7 节（UI 规格）、9 节（测试）、11 节（禁止事项）。开工前先通读第 1、2、4、6、7、9、11 节。

## 0. 背景与目标

实现主窗口右侧 390px 数据面板三件套（串口面板、读数面板、曲线面板）+ 模拟数据源。复刻基准：`web/index.html` 48–121 行结构、`web/style.css` 全文配色/尺寸、`web/js/viewer.js` 21–22 行（符号翻转）、91–208 行（读数与曲线）、240–398 行（串口交互）。

**本包不写 3D 视口（SubStage 2）、不写主窗口装配（SubStage 4）。**

## 1. 环境

- 工作目录：`D:\workshop\Processing\wit-imu-sensor-glove-prototype`
- 运行环境：`.venv\Scripts\python.exe`（Python 3.14.4；PySide6==6.10.3）
- 测试：`.venv\Scripts\python.exe -m pytest tests/ -q`；构建检查：`compileall -q app`
- 面板纯函数测试不得创建 QApplication（用 `pytest.importorskip` 隔离或纯函数模块单独 import）；需要控件的测试用 `QT_QPA_PLATFORM=offscreen` + `QApplication` fixture（offscreen 下 Qt Widgets 可用，仅 OpenGL 不可用——本包不涉及 GL）。

## 2. 现状要点

- frame dict 契约：设计文档第 4 节（`wrist_quaternion_wxyz`、`fingers[name].bend_deg/sway_deg/...`、`time_s`、`sequence`）。
- `viewer.js` 关键行为：`LATERAL_DISPLAY_SIGN = {thumb:1, index:-1, middle:1, ring:-1, little:1}`；`displaySwayDeg(name, deg) = deg × sign`；sway 条 ±30° → ±50% 宽度、负值向左；bend 条 0~90° → 0~100%；曲线 y 范围 [-30,90]、刻度 [-30,0,30,60,90]、bend 色 #49d9d0、sway 色 #ffbd66、当前帧竖线、live 窗口 400 帧；点击手指行切换选中并全量重绘曲线；状态徽章映射 `{idle:未连接, connecting:连接中, calibrating:标定中, live:处理实时, error:错误}`；状态行格式 `{message} · 样本 {n} · 输出帧 {m} · {port}@{baud}`；summary = 相对时间/采样间隔/最大弯曲/最大侧摆/帧计数 `i+1 / len`。

## 3. 交付文件清单

```
app/panel_utils.py       # 纯函数：display_sway_deg、phase_badge_text、status_line_text、frame_summary、chart 采样钳制（可 headless 单测）
app/demo_source.py       # demo_frame_bytes(now_s, sequence) + DemoSource 常量（契约 6.4）
app/serial_panel.py      # SerialPanel（契约 6.6）
app/readout_panel.py     # ReadoutPanel + FingerRow 自定义行控件（契约 6.6）
app/chart_panel.py       # ChartPanel（QtCharts，契约 6.6）
app/styles.py            # QSS 字符串与调色板常量（设计文档 7 节，供 SS4 全局应用）
tests/test_panel_utils.py
tests/test_demo_source.py
```

## 4. 实现要点

### 4.1 app/panel_utils.py（纯函数，全部可单测）

- `display_sway_deg(finger: str, sway_deg: float) -> float`（符号表见上）。
- `phase_badge_text(phase) -> str`；`status_line_text(status: dict) -> str`（含 port@baud 后缀规则：`port` 为 None 时不拼）；`frame_summary(frame, index, frames) -> dict`（relative_time、interval_ms（index=0 → None）、max_bend、max_sweay（按显示符号翻转后取 abs 最大）、counter 文案）。
- `clamp_chart_value(v, lo=-30.0, hi=90.0) -> float`。

### 4.2 app/demo_source.py

- 常量：`DEMO_HZ = 200.0`；手指相位 `DEMO_PHASES = {'thumb':0.0, 'index':0.7, 'middle':1.4, 'ring':2.1, 'little':2.8}`。
- `demo_frame_bytes(now_s: float, sequence: int) -> bytes`：按设计文档 6.4 公式计算腕四元数与五指 bend/sway，`struct.pack('<B B B H 6f H', 0xAA, 0x55, node, sequence, *floats, crc)` × 6 节点拼接（腕节点 0x50：wxyz+time_s+0.0；手指节点 0x51–0x55：相对四元数（绕 +X 转 bend_rad）+ bend_deg + sway_deg；CRC 用 `from processed_pipeline import modbus_crc`）。
- 时间基：模块函数参数传入（会话线程用 `time.perf_counter()`），保证确定性可测。

### 4.3 app/serial_panel.py（契约 6.6）

- 控件：端口 QComboBox（首项"模拟数据"data='demo'，其余 data=device，显示 `{device} · {description}`）、波特率 QComboBox（115200/460800/921600/3000000，默认 921600）、按钮行（刷新端口 / 连接串口 primary / 断开 / 重新标定）、状态 QLabel。
- `set_ports()` 保留原选中（同 `viewer.js` 280–310 行 replaceChildren 语义）；`set_connected(connected, demo)`：连接态禁用端口/波特率下拉与连接钮、启用断开钮；demo 态禁用重新标定钮并 tooltip"模拟数据源不支持标定"；非 demo 且 phase ∈ {calibrating, error} 时禁用标定钮（`viewer.js` 263 行）。
- 信号语义：连接点击 → `connectRequested(port, baud)`；`current_selection()` 返回当前下拉值。
- 样式：见 `app/styles.py`（本包负责写入该文件全量 QSS）。

### 4.4 app/readout_panel.py（契约 6.6）

- 节头"当前帧"+ 帧计数 `output` 标签；2×2 网格：时间/采样间隔/最大弯曲/最大侧摆（`frame_summary` 填充）。
- 五指角度节：5 个 `FingerRow`（QWidget 子类，checkable 语义自定义）：名称 QLabel + bend 条（自定义 paintEvent 或 QProgressBar 定制：0~90°→0~100%）+ bend 数值 + sway 条（±30°→±50%，负向左，中心对齐）+ sway 数值（带符号 `+x.x`）。点击行 → 选中样式（边框 #49d9d0 38% 透明）+ `fingerSelected(name)`。`selected_finger()` 默认 'index'。
- `apply_frame(frame, index, frames)`：按 `renderFrame`（`viewer.js` 117–150 行）语义更新全部行与 summary（sway 显示值用 `display_sway_deg`）；`reset()` 清零。
- 数值格式化：`bendDeg.toFixed(1)`；sway 带符号 `+`/`-`。

### 4.5 app/chart_panel.py（契约 6.6）

- QChartView 内 QChart：标题"X指曲线"（`set_finger` 更新）、图例 bend/sway；QLineSeries ×2（bend #49d9d0 / sway #ffbd66，QPen width 2.5，点集用 `QPointF(index, value)`）；QValueAxis X 0~399 隐藏刻度、Y -30~90（tickCount=5 → -30/0/30/60/90，labelFormat '%d'）；背景 #091019、边框 rgba(159,183,210,0.15)；竖线标记 = 第三 QLineSeries（2 点，(index,-30)/(index,90)，白 70% 透明，宽 1）。
- 数据窗口：内部 list 上限 400（`push_frame` 超限丢头、`replace()` 重挂）；值经 `clamp_chart_value`；`mark_current(index)` 更新竖线；`clear()` 清空。
- `set_frames(frames)` 全量重绘（切指时由装配层调用）。

### 4.6 app/styles.py

- 输出 `GLOBAL_QSS: str` 与 `PALETTE: dict`：完整复刻 `web/style.css` 颜色表（设计文档 7 节）：主窗口/面板背景、按钮（normal/hover/disabled/checked）、下拉框、标签、primary 按钮（青底深字 #071012）、行选中、节分隔线、滚动条、等宽字体（Cascadia Mono, Consolas, monospace 回退）、圆角与内边距（px 与 css 一致）。

## 5. 验收标准（本包完成即判定）

1. `compileall -q app` 退出码 0；
2. `pytest tests/test_panel_utils.py tests/test_demo_source.py -q` 全绿：
   - panel_utils：符号表（index/ring 翻转）、phase 文案、状态行拼装（含/不含 port）、frame_summary 数值（构造 2 帧验证相对时间/间隔/最大弯曲）、clamp 边界；
   - demo_source：`demo_frame_bytes` 长度 31×6、同步头、CRC 校验通过、经 `ProcessedPoseAssembler.push()` 解析后 6 节点聚合为 1 帧、帧字段与脚本公式在给定 t 处逐值一致（bend/sway/四元数误差 < 1e-6）、sequence 递增；
3. offscreen 冒烟：QApplication + 三面板实例化、`set_ports`/`set_status`/`set_connected`/`apply_frame`/`push_frame` 无异常（作为测试或验证脚本，输出到报告）；
4. 契约 6.6 方法签名逐一核对（SubStage 4 按契约装配）；
5. 完成报告：文件清单、测试输出原文、与 web 端 UI 逐项对照表（控件/文案/颜色/数值格式）。

## 6. 禁止事项

- 见设计文档 11 节；另：不得依赖 `app/hand_view.py`（本包与渲染解耦）；不得创建主窗口/应用入口；不得修改 `tools/serial_live.py`（只消费契约）；QtCharts 之外的第三方绘图库禁用。

## 7. 完成报告

在最终回复中输出：文件清单、pytest 完整输出、偏差说明。
