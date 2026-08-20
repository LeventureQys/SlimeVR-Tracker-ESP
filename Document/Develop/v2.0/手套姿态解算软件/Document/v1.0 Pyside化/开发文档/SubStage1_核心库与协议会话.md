# SubStage 1 任务书：工程骨架 + 核心库与协议会话

- **所属 Stage**：阶段三·开发 / 工作包 A
- **依赖前置**：无。**独立可执行**。
- **并行状态**：可并行（与 SubStage 2、3 并行；2/3 的集成测试等待本包文件落地）
- **契约来源**：`Document/v1.0 C++化/设计文档.md` 第 6 节（接口契约）、第 5 节（算法移植规格）、第 11 节（禁止事项）。开工前先通读该文档第 1、2、5、6、11 节。

## 0. 背景与目标

本项目是 WIT IMU 传感器手套上位机。当前是 Python HTTP 服务 + Three.js 网页；本版本把 UI 换成 PySide6 桌面应用（3D 用 QOpenGLWidget 自绘）。本工作包只做**纯 Python 核心库与串口会话**（不写任何 GUI/渲染代码）：

1. 工程骨架（`app/` 包、`tests/`、依赖文件）；
2. glTF2 解析器：解析 `web/assets/generic-hand-left.glb`；
3. 四元数工具与手部姿态映射（移植 web 端 JS 算法）；
4. 串口会话 `tools/serial_live.py`（从 `tools/serve_app.py` 提取，去 HTTP）；
5. 资产复制到 `app/assets/`；
6. 全部单元测试（pytest）。

## 1. 环境

- 工作目录：`D:\workshop\Processing\wit-imu-sensor-glove-prototype`
- 运行环境：`.venv\Scripts\python.exe`（Python 3.14.4；已装 pyserial==3.5、PySide6==6.10.3、shiboken6）
- 测试命令：`.venv\Scripts\python.exe -m pytest tests/ -q`（如 pytest 未装：`.venv\Scripts\python.exe -m pip install pytest`）
- 构建检查：`.venv\Scripts\python.exe -m compileall -q app tools tests`

## 2. 现状要点（参考，勿改）

- `tools/processed_pipeline.py`（1–160 行）：`ProcessedPoseAssembler`、`modbus_crc`、`FINGER_ORDER` 等，**零改动，直接 import**（需把 `tools/` 加入 sys.path 或作为包引用）。
- `tools/serve_app.py` 62–227 行：`SerialLiveSession`（串口线程 + 队列 + 标定窗口），本包把它提取改造为 `tools/serial_live.py`。
- 算法移植源：`web/js/hand-model.js`（全文）、`web/js/viewer.js`（22 行显示符号翻转）。
- 资产：`web/assets/generic-hand-left.glb`（SHA-256 `BC67783144944EA1CDA54D9247885825EA5FB9D4651469FE7D00BE517A5C2B87`），复制到 `app/assets/generic-hand-left.glb` 并校验哈希。

## 3. 交付文件清单

```
app/__init__.py
app/assets/__init__.py              # 可空；附 assets/README.md（来源、版本 1.0.15、MIT、SHA-256）
app/assets/generic-hand-left.glb    # 从 web/assets 复制 + SHA-256 校验
app/gltf/__init__.py
app/gltf/loader.py                  # GltfLoader + GltfAsset 数据类（契约 6.1）
app/quaternion.py                   # 基变换（设计文档 5.1）
app/hand_pose.py                    # HandPoseModel/HandPoseResult（契约 6.2，算法 5.3/5.4/5.5）
tools/serial_live.py                # SerialLiveSession（契约 6.3）
tests/conftest.py                   # sys.path：项目根 + tools/；pytest fixtures（GLB 路径、合成帧工具）
tests/test_gltf_loader.py
tests/test_quaternion.py
tests/test_hand_pose.py
tests/test_serial_live.py
tests/test_crc_regression.py
requirements.txt                    # 更新：pyserial==3.5 / PySide6==6.10.3 / pytest>=8
```

## 4. 实现要点

### 4.1 gltf/loader.py

- GLB 容器：12 字节头（magic `glTF`、version 2、total length），chunk 循环（chunkLength/chunkType）；JSON chunk 类型 `0x4E4F534A`，BIN chunk `0x004E4942`。
- 解析 JSON：`scenes[0].nodes` → scene_roots；`nodes[]`（name/children/translation/rotation/scale/matrix）；`meshes[0].primitives[0]`（attributes→accessor、indices、mode）；`skins[0]`（joints→node 索引、inverseBindMatrices→accessor）；`materials[0].pbrMetallicRoughness.baseColorFactor`。
- accessor 解析：`bufferViews[]` + `buffers[]` → 从 BIN 切出 `data` 字节（含 byteOffset），保留 `component_type/count/type/byte_stride`。**MAT4 需展平成 16 元组（行主序）**；VEC3/VEC4 提供便捷解码函数（`decode_accessor(acc) -> list[tuple]`，供测试与 hand_pose 使用）。
- 输出 `GltfAsset`（契约 6.1）。节点 `matrix` 与 T/R/S 二选一，二者并存时报 ValueError（glTF 规范禁止并存）。
- 校验：必须存在 POSITION/NORMAL/indices；`skins` 至少 1；`skin.joints` 长度 25；必需关节名（设计文档 5.3 的 25 个）全部在 nodes 中。

### 4.2 quaternion.py

- 使用 `PySide6.QtGui.QQuaternion/QVector3D`（纯值类型，无需 QApplication）。
- `ALGORITHM_TO_HAND = QQuaternion.fromAxisAndAngle(QVector3D(0,0,1), -90.0)`。
- `algorithm_quaternion_to_hand(wxyz: Sequence[float]) -> QQuaternion`：`q = QQuaternion(x, y, z, w).normalized(); return (ALGORITHM_TO_HAND * q * ALGORITHM_TO_HAND.conjugated()).normalized()`。
- 另提供 `mat4_from_trs` / `quat_to_mat4` 等 16 元组（行主序）辅助函数（渲染与测试共用），实现自写（QMatrix4x4 也可，但输出必须行主序 tuple）。

### 4.3 hand_pose.py（关键，逐语句移植）

- 常量与结构完全按设计文档 5.3 节；`REQUIRED_JOINT_NAMES` 顺序 = `['wrist'] + 每指按 FINGER_ORDER 追加 metacarpal/proximal(/intermediate)/distal/tip`（共 25）。
- rest 姿态：用 `GltfNode` 的 translation/rotation/scale（matrix 节点则从矩阵分解或直接用矩阵；本项目关节节点用 TRS，如遇 matrix 直接读取并转换为 TRS 处理，函数 `node_local_matrix()` 统一返回 16 元组）。
- 父子链：`jointWorld_j = M_root × Π(祖先局部 TRS) × jointLocal`（含 Armature 祖先）；所有节点 world 矩阵从 scene_roots 向下递推。
- 根变换 `M_root`：按设计文档 5.2 精确计算（bbox 用蒙皮网格 POSITION 数据，bind 姿态、mesh 节点局部 TRS 参与）。
- 蒙皮矩阵：`skinMatrix_j = jointWorld_j × invBind_j`（invBind 用 GLB 原始数据，行主序 4×4 乘法）。
- `apply_pose()`：先恢复 rest → 逐指 `_pose_finger`（设计文档 5.3 递推伪码，注意 `accumulated` 的乘序与 JS 一致：`firstBend ⊗ swayRotation`、`nextBend ⊗ accumulated`）→ 根四元数 `algorithm_quaternion_to_hand(wrist)`。
- `overlay_positions` = 各关节 world 矩阵平移列（scene 空间，含根变换）。
- `overlay_bones`：19 条，链边顺序同 JS `#buildBoneOverlay` 循环（thumb 3 + 长指各 4）。
- 数值全部 float；无 numpy（仅 PySide6 值类型 + 标准库）。

### 4.4 tools/serial_live.py

- 从 `tools/serve_app.py` 62–227 行提取：`SerialLiveSession`、`SerialStatus`、`build_recalibrate_command`、`DEFAULT_BAUD=921600`、`HAND_HZ=200.0`、`CALIBRATE_STALE_S=0.4`。
- 修改点：删 HTTP 相关；`status_dict()` 保留原 payload 结构（phase/message/sample_count/output_frame_count/port/baud/hand_hz，calibrating 时 phase/message 覆写，同 `serve_app.py` 77–93 行）；新增 `drain_frames()`（`get_nowait` 循环清空队列返回列表）；新增 `running` 属性；`request_calibrate()` 未连接抛 `RuntimeError('串口未连接')`。
- `start_demo()` 留桩（真实实现在 SubStage 3 的 `app/demo_source.py`）：导入 `app.demo_source` 的函数 `demo_frame_bytes(now_s, sequence)`（先 `import app.demo_source`，导入失败则 raise RuntimeError('模拟数据源不可用')），启动同名 daemon 线程每 1/200 s 产出一批 6 节点字节送入 `self._pose_assembler.push()`，其余（状态/发布/标定丢弃）与真实串口完全一致；demo 模式 `_serial` 为 None，`request_calibrate()` 抛 `RuntimeError('模拟数据源不支持标定')`。
- 测试用钩子：`CALIBRATE_STALE_S` 为模块常量但测试可 monkeypatch（构造后再 patch 实例 `_calibrate_until` 亦可，任务书允许测试直接操作私有字段）。
- **不 import processed_pipeline 之外的任何项目模块**（避免循环依赖）；`sys.path` 上 tools 由调用方保证（main.py 或测试 conftest）。

### 4.5 tests/

- conftest：`sys.path.insert(0, 项目根)` 与 `tools/`；fixture `asset`（GltfLoader 加载复制后的 `app/assets/generic-hand-left.glb`，模块级缓存）；fixture `make_frame(...)` 合成合法 AA55 帧字节（用 `modbus_crc`）。
- 断言要点（设计文档 9 节）：解析计数与必需关节；基变换轴映射（对 +X 轴 90° 旋转 q，验证 world 轴映射 X→-Y 等）；握拳完成度与关节角（bendDeg=85 拇指 → completion=1 → [-38,-52,-62]°；bendDeg=155 食指 → completion=1）；sway ±30 钳制；**bind 不变性**：rest 姿态下逐顶点验证 `Σ w·skinMatrix·v == M_root·T_meshNode·v`（误差 <1e-4，失败则按设计文档 5.4 备选式实现并重测）；重复 applyPose 确定性；串口会话：合成 6 节点流出 1 帧且字段正确、坏 CRC 计数、标定字节 `AA 55 C0 01 00 00+CRC`、0.4s 窗口丢弃、stop 幂等；CRC16 已知向量（Modbus 标准例：`01 03 00 00 00 01` → 0x0A84，字节序 LE `84 0A`；再自算一组 31 字节全帧做回归）。

### 4.6 requirements.txt

```
pyserial==3.5
PySide6==6.10.3
pytest>=8
```

## 5. 验收标准（本包完成即判定）

1. `compileall -q app tools tests` 退出码 0；
2. `pytest tests/ -q` 全绿（含上面全部断言）；
3. `GltfAsset` 满足 6.1 契约字段（SubStage 2 直接按契约消费，不允许本包改契约）；
4. `tools/processed_pipeline.py` 未改动（`git status` 无该文件变更）；
5. 完成报告：列出新建/修改文件、测试执行结果原文（pytest 输出）、契约符合性自查表。

## 6. 禁止事项

- 见设计文档 11 节；另：本包不得创建任何 QWidget/QOpenGL 代码；不得修改 `web/`、`tools/serve_app.py`、`archived/`；不得引入 numpy 等新依赖。

## 7. 完成报告

在最终回复中输出：文件清单、pytest 完整输出、任何与任务书的偏差及理由（偏差须符合设计文档，否则视为失败）。
