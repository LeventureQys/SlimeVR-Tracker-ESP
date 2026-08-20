# web 端场景参数对照表（验收 4.2 观感比对依据）

来源：`web/js/viewer.js`（36–74 行）与 `web/js/hand-model.js`（29、122–132、219–220、230、239 行）。桌面端必须逐项实现（设计文档 5.6 节），验收报告按本表逐项记录结论。

## 相机与交互（viewer.js 39–57、218–223 行）

| 参数 | web 端值 | 桌面端实现位置 |
| --- | --- | --- |
| fov | 34°（PerspectiveCamera） | app/camera.py |
| near / far | 0.1 / 100 | app/camera.py |
| 默认位置 | (-4.2, 0.7, 12.2) | app/camera.py |
| 默认目标 | (0, 0.3, 0) | app/camera.py |
| 距离钳制 | min 7.2 / max 18 | app/camera.py |
| 阻尼 | 0.07（OrbitControls damping） | app/camera.py |
| 交互 | 左旋转 / 滚轮缩放 / 右平移 | app/hand_view.py |
| pixelRatio | min(devicePixelRatio, 2) | app/hand_view.py |
| 重置视角 | 恢复默认位置+目标 | reset_view() |

## 灯光（viewer.js 58–67 行）

| 灯光 | 颜色 | 强度/距离 | 位置/方向 |
| --- | --- | --- | --- |
| 半球光 | sky 0xc8e1ff / ground 0x17202b | 2.0 | — |
| 主方向光 | 0xfff2dd | 4.4 | (-4.5, 3.5, 7.5) |
| 轮廓光 | 0x79aaff | 3.0 | (5.5, -1.5, 4.5) |
| 点补光 | 0x49d9d0 | 1.7，距离 12 | (-3, -3, 3) |

## 雾 / 色调 / 渲染（viewer.js 37、44–48 行）

| 项 | web 端值 |
| --- | --- |
| 雾 | FogExp2，颜色 0x080d15，密度 0.035 |
| 色调映射 | ACESFilmic，曝光 1.12 |
| 色彩空间 | sRGB 输出 |
| 抗锯齿 | antialias true |

## 网格（viewer.js 69–74 行）

| 项 | web 端值 |
| --- | --- |
| GridHelper | 10×10、20 分割 |
| 颜色 | 0x29435b / 0x172537 |
| 变换 | 绕 X 转 90°，平放于 z=-1.2 |
| 透明度 | 0.34（混合） |

## 材质（hand-model.js 122–132、219–220 行）

| 材质 | 颜色 | 参数 |
| --- | --- | --- |
| 皮肤（MeshPhysicalMaterial） | 0xd9bca4 | roughness 0.58、metalness 0、opacity 0.43、transmission 0.04、clearcoat 0.08、depthWrite off、DoubleSide |
| 骨骼覆盖层圆柱 | 0xf0dfbd | roughness 0.62、clearcoat 0.12 |
| 关节球 | 0xcdb28a | roughness 0.72 |

## 覆盖层几何（hand-model.js 230、239 行）

| 部件 | 参数 |
| --- | --- |
| 骨段圆柱 | 半径上 0.075 / 下 0.06、高 1、径向 14 段 |
| 关节球 | 腕 0.13、其余 0.095；16×12 段 |
| 覆盖层结构 | 19 骨段（拇指链 3 + 长指链各 4）+ 25 关节球 |

## 模型变换（hand-model.js 153–169、29 行）

| 项 | web 端值 |
| --- | --- |
| 固定旋转 | Rz(π) ⊗ Ry(-π/2) |
| 显示缩放 | 最大维度 = 7.05 场景单位 |
| 居中 | AABB 中心 → 原点，再 +0.35 y |
| 满握拳角度 | thumb 85 / index 155 / middle 160 / ring 120 / little 145 |

## UI 配色（style.css 8–22 行）

| 变量 | 值 |
| --- | --- |
| bg / panel | #070b12 / #0c121c → #0a1018 |
| line | rgba(159,183,210,0.15)，strong 0.28 |
| text / muted | #edf3f8 / #8292a5 |
| bend / sway | #49d9d0 / #ffbd66 |
| accent / danger | #72a7ff / #ff6c7b |
| 轴图例 | X #ff7685 / Y #67dda0 / Z #73aaff |
