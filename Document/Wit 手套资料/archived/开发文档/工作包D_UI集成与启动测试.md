# 工作包 D - UI 集成与启动测试

- 依赖前置：工作包 A、工作包 B、工作包 C
- 并行状态：需等待
- 执行时机：渲染与交互模块可用后

## 目标

装配独立 Demo 主窗口，提供骨骼树、属性与约束面板、手动/六路模拟模式、状态反馈，并完成真实启动测试。

## 实现范围

1. 主窗口装配中央渲染视图、骨骼树、属性面板、工具栏和状态栏。
2. 实现树与 3D 选择双向同步。
3. 属性面板显示并编辑允许轴角度、上下限和截断状态。
4. 六路模拟面板提供掌心与五指控制、播放、暂停、重置、无效样本注入。
5. 模式切换时明确姿态来源，避免手动与模拟输入同时写状态。
6. 支持命令行 `--model <path>`；默认加载研究目录模型副本。
7. 支持 `--smoke-test`：窗口创建、模型加载、首帧渲染后自动退出并返回明确状态码。
8. 所有错误显示中文摘要和可复制技术详情。

## 生命周期

`Application` 持有导入模型、运动控制器和主窗口。OpenGL 资源仅在上下文有效时创建/销毁。模拟源使用 UI 定时器轮询，不创建无必要线程。窗口关闭后停止定时器，再销毁渲染资源。

## 启动测试路径

1. 默认启动并确认手模显示。
2. 完成轨道旋转、平移、缩放、重置。
3. 在树和视图中各选一次骨骼。
4. 拖动普通手指与拇指关节至限位。
5. 切换六路模拟，分别改变五个手指姿态并观察独立响应。
6. 注入无效掌心和无效单指样本，确认保持策略与状态提示。
7. 重置姿态。
8. 使用不存在模型路径启动，确认窗口存活且错误可诊断。
9. 执行 `--smoke-test` 并记录退出码和日志。

## 验收标准

- 所有核心路径无崩溃、无 NaN、无与本功能相关的 Qt Critical/OpenGL 错误。
- UI 标签明确说明“六路姿态耦合近似，非真实关节角、非医学用途”。
- 启动测试证据写入阶段三完成报告，包含命令、环境、操作结果和日志路径。
- 不修改研究目录外文件。

## 实现记录 - 2026-07-29

- 新增 `src/app/main.cpp`，完成 `--model <path>`、默认模型与配置路径、FBX 导入、`handrig::BoneData` 到 `motion::SkeletonBinding` 转换、配置校验、`PoseSolver`/`ImuPoseMapper` 装配和 `--smoke-test` 状态码。
- 新增 `src/ui/main_window.h/.cpp`，实现中央 `HandRenderWidget`、骨骼树、树与 3D 选择同步、关节 XYZ 编辑与限位状态、手动/六路模拟互斥模式、五指 curl、六路无效样本注入、播放、重置、视角适配和状态栏 FPS/模型/输入诊断。
- `HandRenderWidget` 新增 `frameRendered()`，仅在完成有效模型绘制后发出，用作首帧 smoke 成功条件。
- `--smoke-test` 在首帧后自动执行可编辑骨骼选择、超限关节截断、六路模式切换、五指不同姿态、单指无效样本和姿态重置；第二个有效渲染帧完成后才以 0 退出。
- 根 `CMakeLists.txt` 统一加入 `src/motion`、`src/render`、应用目标与 motion tests；所有改动和构建输出均位于研究项目目录。

## 验证记录 - 2026-07-29

环境：Windows 10 SDK 10.0.26100.0、MSVC 2022 17.14、Qt 6.8.3、CMake 4.3.2。

| 验证项 | 命令 | 结果 |
|---|---|---|
| 配置 | `cmake -S . -B build` | 通过；仅有 Assimp 对 CMake CMP0175 的开发者警告 |
| Debug 全量构建 | `cmake --build build --config Debug -j8` | 通过；应用、导入测试、motion tests 均生成 |
| Release 应用构建 | `cmake --build build --config Release -j8 --target hand_rig_demo` | 通过 |
| 单元/导入测试 | `ctest --test-dir build -C Debug --output-on-failure` | 2/2 通过；`model_importer` 14.61 秒，`handdemo_motion` 0.07 秒 |
| 错误模型 smoke | `build/bin/Release/hand_rig_demo.exe --smoke-test --model models/missing.fbx` | 按约定退出，退出码 2 |
| Debug 正常 smoke | `build/bin/Debug/hand_rig_demo.exe --smoke-test` | 通过；真实模型导入、窗口初始化和首帧 OpenGL 绘制完成，退出码 0 |
| Release 正常 smoke | `build/bin/Release/hand_rig_demo.exe --smoke-test` | 通过；退出码 0 |
| 错误模型 smoke | `build/bin/Debug/hand_rig_demo.exe --smoke-test --model models/missing.fbx` | 通过；按约定退出码 2 |

首帧阻塞已关闭。根因是 `buildInterface()` 在渲染器收到模型前调用 `setMode(0)`，该调用产生姿态并触发 `setPoseResult()` 的“无模型”错误；非 smoke 状态下错误处理打开模态对话框，使窗口构造无法返回。修复为模型、姿态和骨骼交互全部初始化后再调用 `setMode(0)`。同时相机包围盒改为最多约 20 万个绑定姿态顶点采样，避免大模型在 Debug 下产生不必要的启动延迟。

## 模型变换回归修复 - 2026-07-29

- 用户反馈默认姿态存在分离指甲，调整任一候选链远节会导致手指网格飞出。
- 根因是 FBX 的多个蒙皮网格具有各自节点变换和各自骨骼 offset；旧实现只保存首次出现的全局骨骼 offset，并在 GPU 蒙皮时丢弃网格节点变换。
- 修复后每个 `MeshData` 保存 `bindTransform` 与 `boneOffsets`，GPU 逐网格上传独立调色板。蒙皮顶点公式统一为 `globalInverse * animatedBoneGlobal * meshBoneOffset * meshBindTransform`；网格绑定变换在最右侧恢复 FBX 节点比例，骨骼形变在其左侧作用于统一模型空间。
- `model_importer` 新增真实模型逐网格绑定不变量；motion 测试新增带预旋转绑定姿态的骨长回归；smoke 会旋转五条候选链各自最远的可编辑关节，并验证矩阵有限和骨长不变。
- 验证：Debug 全量构建通过，CTest 2/2 通过，Debug/Release 远节交互 smoke 均退出码 0。
