# SubStage 5 完成报告：GLB 导入与 OpenGL 渲染

- 所属 Stage：SubStage 5（阶段三·开发）
- 工程根：`Document/Research/手套IMU解算MVP`
- 构建目录：`build-s5`（独立，CMake 4.3.2 / VS 2022 / Qt 6.8.3）
- 状态：核心交付完成；两个 SubStage5 测试均通过。

## 1. Assimp 决策门结论（关键）

任务书首选 Assimp 5.4.3，但本会话执行导入测试前的「获取」阶段已受阻，结论如下：

| 尝试 | 结果 | 证据 |
|------|------|------|
| PowerShell `Invoke-WebRequest`（GitHub v5.4.3.tar.gz） | 失败 | `The SSL connection could not be established` |
| `curl.exe -L`（schannel） | 失败 | `SEC_E_NO_CREDENTIALS (0x8009030e)` |
| `git ls-remote`（assimp） | 失败 | `schannel: AcquireCredentialsHandle failed: SEC_E_NO_CREDENTIALS` |
| CMake `file(DOWNLOAD)` | 失败 | `status: 35;"SSL connect error"`（含 `CMAKE_TLS_VERIFY=0` 仍失败） |
| 本地缓存 `build/_deps/assimp-src` / `assimp-build` | 空 | 两个目录均为 0 个文件（仅 `assimp-subbuild` 有 populate 状态，无源码） |
| 全盘检索 Assimp 源码/静态库/头文件 | 无 | 仅找到 PySide6/PyQt6 插件内的 `assimp.dll`（无头文件，不可链接） |

**结论：** 沙箱在凭证层阻断 TLS 出网（非证书校验问题），Assimp 5.4.3 无法获取；任务书所述本地缓存实际为空。依据任务书第 6 节「允许替换解析器，但不得改变 `RiggedModel` 和渲染接口」，SubStage 5 采用**自包含 glTF2/GLB 解析器**作为替换导入路径，行为与 Python 参考 `app/gltf/loader.py` 完全一致。`cmake/substage5.cmake` 仍保留 `HAND_SKELETON_FETCH_ASSIMP` 选项（默认 OFF）供联网环境后续引入 Assimp。

## 2. 目标 GLB 实测证据（直接解析 GLB，等价于 Assimp 需提供的契约）

对 `assets/generic-hand-left.glb`（94572 字节，glTF 2.0 / GLB）直接解析 JSON chunk + BIN chunk，得到：

- **节点：30 个**。节点 29 = `Armature`（场景根，无变换即单位矩阵）；节点 25 = `l_handMeshNode`（mesh=0、skin=0、单位变换）；节点 26/27/28 = `xr_standard_trigger_pressed_{max,min,value}`（WebXR 语义，无网格）。
- **关节：25 个**，即节点 0..24，全部是 `Armature` 的平铺 children、自身无 children（证明资产 `children` 不能作为解剖父链）。
- **palette（skin.joints）顺序 = [0..24]**：`pinky-finger-tip` → `pinky-finger-…` → … → `thumb-metacarpal`/`thumb-…` → `wrist`（**wrist 最后，palette 索引 24**）。与 Python `skin_joint_names[-1] == 'wrist'` 一致。
- **accessors**：POSITION=VEC3/f32×1360；NORMAL=VEC3/f32×1360；TEXCOORD_0=VEC2/f32×1360；JOINTS_0=VEC4/u8×1360；WEIGHTS_0=VEC4/f32×1360；indices=SCALAR/u16×6942；inverseBindMatrices=MAT4/f32×25。全部 bufferView 位于 buffer 0、紧凑排列（无 byteStride）。
- **材质**：1 个（baseColorFactor 由导入器读取为 `MaterialData`）。
- **矩阵**：inverseBind 全为仿射（末行 ≈ [0,0,0,1]，m(3,3)=1），bindWorld/inverseBind 全有限。

C++ 导入器 `ModelImporter` 将这些契约固化为运行时校验（25 关节唯一齐全、palette 顺序、每顶点 ≤4 权重且索引 ∈[0,24]、inverseBind 齐全、矩阵/权重非有限拒绝）。

## 3. 模型统计

- 顶点：**1360**；索引：**6942**（`test_glb_model::meshStatistics` 断言通过）。
- 骨骼：**25**（palette 顺序，`boneIndexByName` 映射完整）。
- 权重：每顶点 4 权重，归一化后 ∑=1（容差 1e-4），索引 ∈[0,24]。

## 4. bind pose CPU 蒙皮不变性（硬门禁）

固定显示根变换 `R = Rz(π) ⊗ Ry(-π/2)`、缩放 `7.05/maxDim`、居中 `+0.35 y`，wrist 单位四元数（即 root = fixedTransform）：

- `skinned = Σ w_j · (root · bindWorld_j · inverseBind_j) · v`
- 与 `expected = root · v` 比较，**最大误差 = 7.468e-07**（阈值 1e-4），逐顶点全部通过。

测试 `bindPoseSkinnedInvariance` 输出：`bind-pose max skinning error = 7.468e-07 (threshold 1e-4)`。

## 5. 单元测试结果（CTest）

构建目录 `build-s5`，`Release`，`ctest -C Release -R "test_glb_model|test_render_offscreen"`：

```
1/2 Test #12: test_glb_model ........... Passed
2/2 Test #13: test_render_offscreen .... Passed
100% tests passed, 0 tests failed out of 2
```

- `test_glb_model`（9 子项全过）：标准名/骨段计数、文件加载与 25 关节契约、1360/6942 统计、权重范围/归一、bind 不变性、远节旋转只影响子树且骨长不变、非法模型拒绝。
- `test_render_offscreen`（5 子项全过）：离屏创建 widget + setModel + 提交一帧非空帧缓冲 + `glGetError()==GL_NO_ERROR`、非法模型结构化诊断、`setSkeletonFrame` 消费无错误。

## 6. OpenGL 离屏测试说明（重要偏离）

Qt 6.8.3 的 `offscreen` 平台插件**不支持** `createPlatformOpenGLContext`（报 `QOpenGLWidget is not supported on this platform`），无法用 `QT_QPA_PLATFORM=offscreen` 创建 GL 上下文；而 `QT_OPENGL=software`（`opengl32sw.dll`）在本机经 WGL 回退仅给出 3.0 上下文（低于 shader 所需的 3.3 core）。

因此 GL 测试使用 **`QT_QPA_PLATFORM=windows` + `QT_OPENGL=desktop`**（本机桌面 GL ≥ 3.3），软件 GL 路径已在 `cmake/substage5.cmake` 注释中说明。这是对任务书「QT_QPA_PLATFORM=offscreen」字面要求的**环境适配偏离**，已在 `cmake/substage5.cmake` 留痕。

- 离屏渲染截图：`开发文档/SubStage5_离屏渲染截图.png`（640×480，13878 字节，含蒙皮手模 + 骨架覆盖层）。

## 7. 文件清单

新增：

- `assets/generic-hand-left.glb`（由 `Document/Develop/v2.0/手套姿态解算软件/app/assets/generic-hand-left.glb` 复制）
- `assets/README.md`（来源与契约说明）
- `src/model/model_data.h`（`RiggedModel`/`Vertex`/`MeshData`/`BoneData`/`MaterialData`、`Result`/`ModelLoadError`、矩阵工具、`computeDisplayRootTransform`、`computeSkinMatrices`、`skinVertex`）
- `src/model/standard_joints.h`（25 标准关节名 + 19 条语义骨段）
- `src/model/model_importer.h` / `model_importer.cpp`（自包含 glTF2/GLB 解析器 + 契约校验）
- `src/render/hand_render_widget.h` / `hand_render_widget.cpp`（OpenGL 3.3/GLSL330 四权重 GPU 蒙皮、轨道相机、骨架覆盖层、拾取基础、半透明/双面/深度写、法线逆转置）
- `cmake/substage5.cmake`（自包含；`handstudio_model`、`handstudio_render`、两个测试、可选 Assimp FetchContent）
- `tests/test_glb_model.cpp`
- `tests/test_render_offscreen.cpp`

未修改 `src/core/`、根 `CMakeLists.txt`、`src/` 根遗留文件（遵守并行协调边界）。

## 8. 渲染接口与契约

```cpp
class HandRenderWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public slots:
    void setModel(std::shared_ptr<const RiggedModel>);
    void setSkeletonFrame(const HandSkeletonFrame&);
    void resetCamera();
};
```

- `setSkeletonFrame` 只按骨名匹配使用 `HandBoneFrame::skinMatrix`（GPU 蒙皮）与 `globalMatrix` 的平移（覆盖层关节位置），不读取融合器。
- 覆盖层显示 25 关节 + 19 条语义骨段（来自 `standard_joints.h`，**不从节点 parent 画线**）。
- 契约假设：`skinMatrix = globalMatrix × inverseBind`（与 Python `hand_pose.py` 一致，`globalMatrix` 已含显示根变换）。若 SubStage 4 对 `skinMatrix`/`globalMatrix` 语义有不同定义，需在 SubStage 6 集成时对齐（见 §9）。

## 9. 遗留风险

1. **Assimp 未纳入**：因沙箱 TLS 阻断 + 缓存为空，采用替换解析器（已获任务书 §6 允许）。联网环境可置 `HAND_SKELETON_FETCH_ASSIMP=ON` 重新验证 Assimp 路径。
2. **离屏 GL 平台**：`QT_QPA_PLATFORM=offscreen` 无法建 GL 上下文，GL 测试改用 `windows` + 桌面 GL；在无 GPU/仅基础显示驱动的机器上可能低于 3.3。
3. **跨 SubStage 契约歧义点（需 SubStage 6 对齐）**：渲染器假定 `HandBoneFrame::skinMatrix` 已含显示根变换、`globalMatrix` 为关节世界矩阵（含根变换）。若 SubStage 4 的 FK 输出把根变换单独放在 `HandSkeletonFrame::rootTransform` 而不并入 per-bone 矩阵，渲染器需相应调整（当前 bind 姿态回退路径由渲染器自行用 `computeDisplayRootTransform` 计算，自洽）。
4. **全量构建**：`build-s5` 全量 `cmake --build --config Release` 现已成功（SubStage 3 先前未完成的 `calibration_pipeline.h`/`fusion_bank.h` 默认构造错误已由并行任务修复）；SubStage 5 各目标独立构建并通过。
