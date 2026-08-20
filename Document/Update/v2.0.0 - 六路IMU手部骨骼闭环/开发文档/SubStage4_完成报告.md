# SubStage 4 完成报告：手部观测与骨骼求解

## 1. 实施范围

- 工程根：`Document/Research/手套IMU解算MVP/`
- 新增观测模块：`src/hand/hand_observation_solver.*`、`orientation_decomposition.*`、`mount_calibration.*`。
- 新增骨骼模块：`src/skeleton/kinematic_skeleton.*`、`skin_palette_mapper.*`。
- 新增配置：`assets/hand_rig_generic_left.json`。
- 新增构建入口：`cmake/substage4.cmake`。
- 新增测试：`tests/test_hand_observation.cpp`、`test_hand_rig_config.cpp`、`test_skeleton_solver.cpp`、`test_skeleton_palette.cpp`。
- 未修改根 `CMakeLists.txt`、`src/core/`、`src/fusion/`、`src/model/`、`src/render/`、旧全局 `hand_skeleton_*` 实现或 GLB 资产。

## 2. 观测公式与冻结证据

实现固定采用 Hamilton 四元数顺序：

```text
qRelative = inverse(qPalmWorld) * qFingerWorld
qCorrected = qMount * qRelative * inverse(qMount)
```

`test_hand_observation` 使用 X/Y/Z 三轴公共手掌旋转验证相对姿态抵消；使用非交换合成旋转冻结安装修正共轭顺序；使用不同拇指轴验证逐指配置隔离；另覆盖手腕失效冻结、NaN/Inf/零四元数拒绝。测试通过。

## 3. hand_rig_generic_left.json 结构

顶层字段：

- `schemaVersion=1`、`skeletonId=generic-hand-left-v1`、`handSide=left`、`rootName=wrist`。
- `joints`：25 个标准关节，每项包含 `name`、`parentName`、`fingerIndex`、三类局部轴、三轴限位、锁轴和三轴耦合。
- `mountOrientations`：五个传感器独立 `[w,x,y,z]` 安装四元数。
- `missingFrames`：held 150 ms、回中 350 ms、Recovered 200 ms、置信度衰减率和恢复最大角速度。

解析器拒绝 JSON 根错误、schema/标识缺失、骨名空/重复/模型缺失、父链不连续、25 关节不完整、零轴、非有限向量/四元数、零四元数、min>max、负耦合和非法缺帧参数。

## 4. 25 关节虚拟层级与 palette 映射

| 虚拟关节 | 虚拟父节点 | GLB palette 映射 |
|---|---|---|
| wrist | 无 | 按唯一名称映射（目标 palette index 24） |
| thumb-metacarpal | wrist | 按唯一名称映射 |
| thumb-phalanx-proximal | thumb-metacarpal | 按唯一名称映射 |
| thumb-phalanx-distal | thumb-phalanx-proximal | 按唯一名称映射 |
| thumb-tip | thumb-phalanx-distal | 按唯一名称映射 |
| index-finger-metacarpal | wrist | 按唯一名称映射 |
| index-finger-phalanx-proximal | index-finger-metacarpal | 按唯一名称映射 |
| index-finger-phalanx-intermediate | index-finger-phalanx-proximal | 按唯一名称映射 |
| index-finger-phalanx-distal | index-finger-phalanx-intermediate | 按唯一名称映射 |
| index-finger-tip | index-finger-phalanx-distal | 按唯一名称映射 |
| middle-finger-metacarpal | wrist | 按唯一名称映射 |
| middle-finger-phalanx-proximal | middle-finger-metacarpal | 按唯一名称映射 |
| middle-finger-phalanx-intermediate | middle-finger-phalanx-proximal | 按唯一名称映射 |
| middle-finger-phalanx-distal | middle-finger-phalanx-intermediate | 按唯一名称映射 |
| middle-finger-tip | middle-finger-phalanx-distal | 按唯一名称映射 |
| ring-finger-metacarpal | wrist | 按唯一名称映射 |
| ring-finger-phalanx-proximal | ring-finger-metacarpal | 按唯一名称映射 |
| ring-finger-phalanx-intermediate | ring-finger-phalanx-proximal | 按唯一名称映射 |
| ring-finger-phalanx-distal | ring-finger-phalanx-intermediate | 按唯一名称映射 |
| ring-finger-tip | ring-finger-phalanx-distal | 按唯一名称映射 |
| pinky-finger-metacarpal | wrist | 按唯一名称映射 |
| pinky-finger-phalanx-proximal | pinky-finger-metacarpal | 按唯一名称映射 |
| pinky-finger-phalanx-intermediate | pinky-finger-phalanx-proximal | 按唯一名称映射 |
| pinky-finger-phalanx-distal | pinky-finger-phalanx-intermediate | 按唯一名称映射 |
| pinky-finger-tip | pinky-finger-phalanx-distal | 按唯一名称映射 |

GLB `children` 不参与解剖父链。`test_skeleton_palette` 验证目标 GLB 25/25 唯一名称映射和 wrist palette index 24。

## 5. FK、约束与缺帧结果

- 每帧重新从模型 `bindLocal` 计算 `local = bindLocal * configuredRotation`，不累计上一帧矩阵。
- `global = parentGlobal * local`；tip 三轴锁定；各关节先耦合再按三轴限位 clamp。
- `skin = rootTransform * virtualGlobal * inverseBind`，通过唯一名称回填 palette。
- 绑定 translation/scale 不变，因此测试中的最大骨长误差为 `0`，满足 `<=1e-4`。
- 同输入、同时间戳重复求解矩阵完全一致。
- 单指失效：`Held`，置信度衰减；150 ms 后在 350 ms 内回中。
- 单指恢复：按 360 deg/s 最大步长插值，200 ms 内标记 `Recovered`。
- 手腕失效：返回上一整手帧并追加 `wrist_invalid` Error 诊断。
- 所有可动多关节输出均为 `Estimated/Held/Recovered`，未标记 `Measured`。

## 6. 构建与测试证据

配置命令：

```powershell
cmake -S . -B build-s4 -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=D:/Devtools/Qt/6.8.3/msvc2022_64
```

结果：配置和生成成功。

Release 构建：

```powershell
cmake --build build-s4 --config Release
```

结果：`handstudio_hand`、`handstudio_skeleton`、四个新增测试和全仓目标构建成功。

CTest：

```powershell
$env:PATH = "D:\Devtools\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
ctest --test-dir build-s4 -C Release --output-on-failure
```

真实输出摘要：`100% tests passed, 0 tests failed out of 17`，总耗时 4.77 秒。既有 `test_calibration`、`test_fusion`、`test_glb_model`、`test_render_offscreen` 均通过；新增四项测试均通过。

## 7. 遗留风险

- 当前配置的安装四元数为单位四元数，真实手套安装方向需由 SubStage 6 硬件校准流程写入版本化配置后才能给出精度结论。
- 当前摆扭分解以配置轴的四元数向量投影提取有符号分量，已覆盖合成轴向测试；真实复杂复合动作仍需硬件数据验证。
- 当前缺帧状态按帧时间戳工作；上游必须保证同 sequence 六路输入和单调时间戳。
- 本 SubStage 不执行 UI/真实硬件启动测试；系统级启动与真实动作验证属于 SubStage 6 集成范围。
