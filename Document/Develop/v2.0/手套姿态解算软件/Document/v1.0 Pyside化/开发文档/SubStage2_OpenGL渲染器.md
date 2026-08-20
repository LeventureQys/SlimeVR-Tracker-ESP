# SubStage 2 任务书：OpenGL 渲染器

- **所属 Stage**：阶段三·开发 / 工作包 B
- **依赖前置**：契约依赖 SubStage 1 的 `app/gltf/loader.py`（GltfAsset，契约 6.1）与 `app/hand_pose.py`（HandPoseResult，契约 6.2）。**等待依赖（在 `app/gltf/loader.py`、`app/hand_pose.py` 获取）**——纯函数模块可先行开发，渲染器与集成测试须等 SubStage 1 文件落地。
- **并行状态**：可与 SubStage 3 并行。
- **契约来源**：`Document/v1.0 C++化/设计文档.md` 5.2/5.4/5.5/5.6 节（场景参数与蒙皮公式）、6.1/6.2/6.5 节（接口）、11 节（禁止事项）。开工前先通读第 1、2、5、6、11 节。

## 0. 背景与目标

实现桌面应用的 3D 视口：用 `QOpenGLWidget` + 自写 OpenGL（GLSL 1.50 core）渲染真实左手蒙皮模型（`app/assets/generic-hand-left.glb`，由 SubStage 1 解析），复刻 web 端 Three.js 场景（`web/js/viewer.js` 36–74、218–223 行与 `web/js/hand-model.js` 218–244 行）：蒙皮网格（半透明软组织）+ 骨骼覆盖层（19 骨段圆柱 + 25 关节球）+ 网格 + 4 灯 + 半球光 + 指数雾 + ACES 色调映射 + 轨道相机（OrbitControls 同参数）。

**本包不写任何 UI 面板/主窗口/串口逻辑。**

## 1. 环境

- 工作目录：`D:\workshop\Processing\wit-imu-sensor-glove-prototype`
- 运行环境：`.venv\Scripts\python.exe`（Python 3.14.4；PySide6==6.10.3）
- 测试：`.venv\Scripts\python.exe -m pytest tests/ -q`；构建检查：`compileall -q app`
- 禁止使用 Qt3D/QtQuick3D/QML（选型已否决，PySide6 6.10 下 glTF 导入器不支持本资产蒙皮）。

## 2. 现状要点

- SubStage 1 产出：`GltfAsset`（含 `mesh.attributes['POSITION'|'NORMAL'|'TEXCOORD_0'|'JOINTS_0'|'WEIGHTS_0']` 字节访问器 + `indices` + `skin_joint_names` + `skin_inverse_bind_matrices`）与 `HandPoseModel`（`apply_pose` → `HandPoseResult{skin_matrices(25×16 行主序), overlay_positions(25 个名字→xyz), overlay_bones(19 条边), root_transform}`）。
- 网格顶点数约数千级（BIN 86KB），200Hz 数据 + 60fps 渲染无性能压力。

## 3. 交付文件清单

```
app/camera.py               # OrbitCamera 纯函数模块（球坐标/阻尼/钳制），可 headless 单测
app/overlay_geometry.py     # 覆盖层几何纯函数：端点→圆柱 TRS、关节球位置，可 headless 单测
app/hand_view.py            # HandViewWidget(QOpenGLWidget)（契约 6.5）+ 着色器源码（模块内常量）
tests/test_camera.py
tests/test_overlay_geometry.py
```

## 4. 实现要点

### 4.1 app/camera.py（纯函数，无 GL 依赖）

- 球坐标状态：`theta/phi/radius/target`；`position() -> (x,y,z)` = target + sphericalToVec3。
- 常量：fov 34、near 0.1、far 100、默认位置 (-4.2, 0.7, 12.2)、目标 (0, 0.3, 0)、min 距离 7.2、max 18、阻尼 0.07、pixelRatio = min(devicePixelRatio, 2)。
- 操作：`rotate(dx, dy)`（theta += 2π·dx/width；phi += 2π·dy/height，phi 钳制 [0.01, π-0.01]）、`zoom(delta)`（半径乘 exp 系数或 wheel delta 比例）、`pan(dx, dy)`（沿相机右/上向量平移 target，距离比例）、`step(dt)`（阻尼指数趋近：`cur += (desired − cur)·(1 − exp(−k·dt))`，k 与 0.07 阻尼因子等效）、`reset()`。
- 输出行主序 view/projection 矩阵纯函数（`view_matrix()`、`projection_matrix(aspect)`）供渲染器与测试使用。

### 4.2 app/overlay_geometry.py（纯函数）

- `bone_transform(start, end) -> (midpoint, quat_xyzw, length)`：位置=(s+e)/2；旋转 = +Y→(e−s) 单位向量的最短弧（`QQuaternion.rotationTo(QVector3D(0,1,0), dir)`）；缩放 (1, length, 1)（圆柱半径常量在渲染器：顶 0.075/底 0.06、高 1、14 段）。
- `joint_marker_positions(result: HandPoseResult) -> dict[name, (x,y,z)]`（直接透传 overlay_positions；半径常量：腕 0.13 其余 0.095，16×12 段）。
- 单位圆柱/单位球顶点生成函数（返回 pos/normal/索引三组 list，供渲染器建 VBO；球用经纬细分，顶部收敛处理）。

### 4.3 app/hand_view.py

- 类契约严格按设计文档 6.5（方法名/信号名/语义不得改）。
- `__init__(asset, parent=None)`：不依赖已加载纹理；编译着色器、生成 VAO/VBO（网格、单位圆柱、单位球、网格线）、构建相机与灯光 uniform 结构；模型姿态未设置前按 `HandPoseModel(asset).reset_pose()` 渲染 bind 姿态。
- 蒙皮管线：
  - 顶点属性布局：pos(3f) normal(3f) uv(2f) joints(4×u8/u16 展开为 4×float) weights(4f)；索引 u16/u32 用 `GL_UNSIGNED_INT/SHORT` 对应；
  - 顶点着色器：`uniform mat4 uSkin[25];` `mat4 skin = w0*uSkin[j0]+w1*uSkin[j1]+w2*uSkin[j2]+w3*uSkin[j3];`（矩阵以 `glUniformMatrix4fv(loc, 25, GL_TRUE, data)` 上传行主序数据）；
  - `skinMatrix_j = jointWorld_j × invBind_j` 已由 HandPoseResult 给出；模型矩阵为单位（顶点已位于 scene 空间）；
  - 法线：`mat3(skin)` 旋转 + normalize（权重归一化由 skin 矩阵线性组合保证）。
- 片元着色器（网格/覆盖层共用同一光照路径，参数不同）：
  - 半球光：`ambient = mix(ground, sky, n.y*0.5+0.5) * 2.0`（sky 0xc8e1ff / ground 0x17202b）；
  - 主方向光 0xfff2dd × 4.4（方向 (-4.5,3.5,7.5) 归一化）、轮廓光 0x79aaff × 3.0（(5.5,-1.5,4.5)）、点补光 0x49d9d0 × 1.7（位置 (-3,-3,3)，衰减 `1/(1+0.09d+0.032d²)` 近似 three.js 距离 12 衰减）；
  - Blinn-Phong：diffuse(N·L) + specular(pow(max(N·H,0), shininess))；皮肤 shininess 由 roughness 0.58 反推（≈32）；骨骼 0.62、关节球 0.72 同理；
  - 皮肤：baseColor 0xd9bca4、alpha 0.43、混合开、深度写入关、双面；骨骼 0xf0dfbd、关节球 0xcdb28a，alpha 1、深度写入开；
  - 雾：`f = 1.0 − exp(−(0.035·dist)²)`（FogExp2 密度 0.035），`color = mix(color, vec3(0x080d15), f)`，所有材质生效；
  - ACES（Narkowicz 近似）× 曝光 1.12 → sRGB 输出（pow(c, 1/2.2)）。
- 网格（GridHelper(10, 20) 平放 z=-1.2，绕 X 90°）：20 等分线两色 0x29435b/0x172537，opacity 0.34 混合，**不参与蒙皮**；线宽 1。
- 覆盖层渲染：逐骨段 draw（MVP + 模型矩阵），逐关节球 draw；随 `set_pose` 每帧更新。
- 相机交互：mousePress/Move/Release（左旋转、右平移、中键或滚轮缩放）、wheelEvent；`reset_view()`；阻尼每帧 `step(dt)`（paintGL 内计时）。
- FPS：paintGL 帧计数，内部 QTimer(700ms) 发 `fpsChanged(float)`。
- `set_pose` 需线程安全（QTimer 主线程调用即可，但内部用 `QMutex`/`QAtomic` 保护最新指针，paintGL 只取最新）。
- 抗锯齿：`QSurfaceFormat.setSamples(4)`（构造时设默认格式）；`setUpdateBehavior(QOpenGLWidget.NoPartialUpdate)`。
- 全部 GL 资源在 `initializeGL` 创建，`cleanupGL` 释放（着色器/VAO/VBO 列表统一管理）。

## 5. 验收标准（本包完成即判定）

1. `compileall -q app` 退出码 0；
2. `pytest tests/test_camera.py tests/test_overlay_geometry.py -q` 全绿（断言至少含：默认相机位/目标、min/max 距离钳制、phi 钳制、阻尼收敛单调性、reset 恢复、bone_transform 中点/长度/正交性（圆柱轴平行方向向量）、投影矩阵投影点正确）；
3. `HandViewWidget` 与契约 6.5 一致（方法/信号齐全，SubStage 4 按契约装配）；
4. 着色器源码内嵌于 `hand_view.py` 常量（不得依赖外部文件/运行时网络）；
5. 完成报告：文件清单、测试输出原文、相机/光照/雾/色调与 web 端参数的逐项对照表（5.6 节）、已知观感差异（允许轻微光照差异）说明。

## 6. 禁止事项

- 见设计文档 11 节；另：不得依赖 Qt3D/QtQuick3D/QML；不得修改 `app/gltf/`、`app/hand_pose.py`（只消费契约）；不得写任何串口/HTTP/面板逻辑；GLSL 版本固定 150 core（Windows 兼容，禁用 `#version 330` 之外的扩展）。

## 7. 完成报告

在最终回复中输出：文件清单、pytest 完整输出、与任务书偏差说明。
