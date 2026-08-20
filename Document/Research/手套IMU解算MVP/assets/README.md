# assets

## generic-hand-left.glb

- 来源：`Document/Develop/v2.0/手套姿态解算软件/app/assets/generic-hand-left.glb`
  （V2.0.0 目标手部模型，由 Python/PySide6 参考实现验证）。
- 格式：glTF 2.0（GLB 容器），单场景 / 单网格 / 单 primitive / 单 skin，TRIANGLES。
- 统计：30 节点、1360 顶点、6942 索引、25 个 WebXR 关节、25×4 顶点权重。
- 关节：`Armature` 下 25 个关节全部平铺（无 children）；`skin.joints` 顺序为
  小指→无名指→中指→食指→拇指→`wrist`（wrist 最后，palette 索引 24）。
- 顶点属性：`POSITION`、`NORMAL`、`TEXCOORD_0`、`JOINTS_0`（u8×4）、`WEIGHTS_0`（f32×4）。
- 由 `src/model/model_importer.cpp`（自包含 glTF2/GLB 解析器）加载，契约校验见
  `tests/test_glb_model.cpp`。

> 说明：任务书首选 Assimp 5.4.3 导入，但开发会话沙箱阻断 TLS 出网且本地 Assimp
> 缓存为空，无法获取 Assimp 源码。依据任务书第 6 节「允许替换解析器」，
> SubStage 5 采用自包含解析器作为替换路径，行为与 Python 参考 `loader.py` 一致。
