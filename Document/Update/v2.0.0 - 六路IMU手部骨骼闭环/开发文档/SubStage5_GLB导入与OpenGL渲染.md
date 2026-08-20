# SubStage 5：GLB 导入与 OpenGL 渲染

- 所属 Stage：SubStage 5
- 依赖前置：等待 SubStage 1；在 `src/core/hand_skeleton_frame.h` 获取骨骼帧契约
- 并行状态：依赖满足后，可与 SubStage 2、3 并行
- 所属阶段：阶段三·开发

## 1. 背景与目标

将现有 Python/PySide6 目标 GLB 的加载、25 关节验证、GPU 蒙皮、骨架覆盖和相机行为移植为纯 C++/Qt OpenGL。Python 代码仅作为行为参考，产品不得启动或调用 Python。

## 2. 当前代码状态

- 目标资产：`Document/Develop/v2.0/手套姿态解算软件/app/assets/generic-hand-left.glb`。
- Python 加载器 `app/gltf/loader.py:90` 限定单网格/单 primitive/单 skin；`:189` 要求 25 关节。
- Python 姿态模型 `app/hand_pose.py:43` 定义 25 个标准关节；`:252` 生成 skin matrices。
- Python 测试 `tests/test_gltf_loader.py:22` 和 `tests/test_hand_pose.py:28` 冻结模型与 bind pose 行为。
- archived C++ `src/core/model_data.h:16` 定义可复用 `RiggedModel`；`src/render/hand_render_widget.cpp` 已有 OpenGL 蒙皮框架。

## 3. 修改范围

- 将目标 GLB 复制或 CMake 安装到产品 `assets/`，保留来源说明。
- 新增/重构 `src/model/` 导入适配器，优先使用 Assimp 5.4.3。
- 移植 archived `src/render/` 到产品工程并改为消费 `HandSkeletonFrame`。
- 新增标准关节语义校验、固定显示根变换、相机和覆盖层。
- 新增目标 GLB 的 QtTest 与离屏 OpenGL 测试。

## 4. 模型契约

- glTF 2.0、单网格、单 primitive、单 skin、TRIANGLES。
- POSITION、NORMAL、JOINTS_0、WEIGHTS_0、indices 必须存在。
- 25 个 skin joint；名称与 Python `REQUIRED_JOINT_NAMES` 一致。
- 目标资产 25 个 joint 在 `Armature` 下平铺；导入器必须保持 GLB joint palette 原顺序，禁止把资产 children 误判为解剖父链。
- `RiggedModel` 提供 name→palette index 映射；虚拟解剖父链由 SubStage 4 的模型配置建立。
- 每顶点最多四权重，索引在 `[0,24]`。
- 保留节点局部 TRS/矩阵和 inverse bind。
- bind pose 下 CPU 验证的蒙皮顶点必须还原根变换后的原始顶点，误差≤`1e-4`。

## 5. 渲染接口

```cpp
class HandRenderWidget : public QOpenGLWidget {
public slots:
  void setModel(std::shared_ptr<const RiggedModel>);
  void setSkeletonFrame(const HandSkeletonFrame&);
  void resetCamera();
};
```

- `setSkeletonFrame` 只使用骨名/索引一致的 skin/global 矩阵，不读取融合器。
- 骨架覆盖层显示 25 关节和 19 条骨段。
- 相机重置仅修改视图矩阵。
- 模型、shader 或骨骼数量错误时显示结构化诊断，不崩溃。

## 6. Assimp 决策门

先用目标 GLB 执行导入测试。若 Assimp 能正确提供 25 关节、权重、父链和 inverse bind，则采用 Assimp。若不能，停止实现并向 MainAgent 报告证据；允许替换解析器，但不得改变 `RiggedModel` 和渲染接口。

## 7. 单元测试

- 目标 GLB 文件存在且可加载。
- 25 标准关节全部存在，父链无环。
- 顶点、索引、权重和 inverse bind 有效。
- bind pose 蒙皮不变性。
- 任一远节旋转只影响其子树，骨长不变。
- 非法模型返回错误，不访问越界。
- 离屏创建 OpenGL widget、加载 shader、提交一帧无 GL error。

## 8. 验收标准

- `HandSkeletonStudio` 构建不依赖 Python。
- 目标 GLB 在 C++ 中加载并显示。
- bind pose 测试误差满足 `1e-4`。
- 覆盖层与蒙皮处于同一空间。
- 相关 QtTest 和离屏渲染测试通过。

## 9. 禁止事项

- 不保留 Python 子进程或生成中间 JSON 的运行路径。
- 不硬编码匿名旧 FBX 骨名。
- 不在 renderer 计算手指角或修复安装坐标。
- 不把 FBX 动画导出加入本任务。

## 10. 完成报告要求

报告 Assimp 实测证据、模型统计、bind pose 最大误差、OpenGL 测试结果、截图路径和修改文件。