# 真实人手模型资产

`generic-hand-left.glb` 是带蒙皮和 25 个 WebXR 标准手关节的真实左手表面模型，桌面应用自持副本。

| 项目 | 内容 |
| --- | --- |
| 来源 | `@webxr-input-profiles/assets` |
| 固定版本 | `1.0.15` |
| 原始路径 | `dist/profiles/generic-hand/left.glb` |
| 许可证 | MIT |
| SHA-256 | `BC67783144944EA1CDA54D9247885825EA5FB9D4651469FE7D00BE517A5C2B87` |
| 本副本来源 | `web/assets/generic-hand-left.glb`（旧版 web 前端资产） |

模型结构：glTF2 / GLB，单场景（根节点 `Armature`），1360 顶点单蒙皮网格（`l_handMeshNode`，
局部 TRS 为单位），25 个关节节点直接挂于 Armature（pinky→ring→middle→index→thumb→wrist，
节点索引 0–24），JOINTS_0 为 u8，索引 u16，无 Draco、无动画。
