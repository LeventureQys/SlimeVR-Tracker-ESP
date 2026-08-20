# SubStage 4：手部观测与骨骼求解

- 所属 Stage：SubStage 4
- 依赖前置：等待 SubStage 1 与 SubStage 3；模型集成测试等待 SubStage 5 提供目标 GLB joint palette
- 并行状态：依赖满足后独立开发；最终需与 SubStage 5 集成
- 所属阶段：阶段三·开发

## 1. 背景与目标

将六路世界姿态转换为掌指相对观测，执行安装修正、屈伸/张合/扭转分解，再使用目标 GLB 的虚拟解剖层级、限位和耦合生成完整 `HandSkeletonFrame`。输出必须明确多关节结果属于估计值。

## 2. 当前代码状态

- archived `src/motion/imu_pose.h:14` 有 `ImuPoseMapper`，实现已采用掌指相对姿态和传感器修正。
- archived `src/motion/pose_solver.h:7` 有绑定姿态、关节、整指 curl/abduction 求解接口。
- archived `config/hand_rig.json:25` 有五指链、轴、限位和耦合，但骨名针对旧 FBX。
- 产品 `src/hand_skeleton_pipeline.h:12` 已有管线雏形。
- 产品 `src/hand_skeleton_types.h:36` 的帧字段不足，统一版本由 SubStage 1 提供。
- 目标 GLB 的 25 个 skin joint 在 `Armature` 下平铺，不能直接作为解剖父链。

## 3. 修改范围

- 新增 `src/hand/hand_observation_solver.*`、`orientation_decomposition.*`、`mount_calibration.*`。
- 重构/移植 archived `src/motion/` 到 `src/skeleton/`。
- 新增 `assets/hand_rig_generic_left.json`。
- 新增 `kinematic_skeleton.*` 与 `skin_palette_mapper.*`，分离虚拟运动学层级和 GLB palette。
- 新增缺帧保持、回中、恢复限速和置信度逻辑。
- 新增观测、配置、FK、约束、缺帧和目标模型映射测试。

## 4. 观测接口与公式

输入为同 sequence 的六个 `FusedImuPose`，输出 `HandObservationFrame`。

```text
qRelative = inverse(qPalmWorld) * qFingerWorld
qCorrected = qMount * qRelative * inverse(qMount)
```

- 公式顺序不可交换。
- 每指独立安装四元数和分解轴。
- 使用 swing/twist 或等价四元数分解，不通过世界欧拉角相减。
- 拇指拥有独立局部轴、限位和耦合参数。
- 手腕无效时整帧观测无效并冻结整手。

## 5. 虚拟运动学层级

`hand_rig_generic_left.json` 必须按标准关节名定义：

- root/wrist。
- 拇指：metacarpal→proximal→distal→tip。
- 四根长指：metacarpal→proximal→intermediate→distal→tip。
- 每节点 `parentName`、绑定局部变换来源、flexion/abduction/twist 轴、角度限位、锁轴和耦合。

GLB joint palette 只决定 skin matrix 数组顺序；虚拟层级决定 FK。通过唯一骨名把虚拟骨骼全局矩阵映射回 palette 并计算 skin matrix。空名、重名、缺失关节或映射不完整必须失败。

## 6. 骨骼求解

- 每帧从绑定局部矩阵重新开始。
- 绑定 translation/scale 不变，只追加配置允许的 local rotation。
- 张合默认只作用于基部。
- MCP/PIP/DIP 或等价链角度按配置耦合，`coupledApproximation=true`。
- 输出顺序稳定，且与 `skeletonId` 对应。
- 所有矩阵、四元数和角度发布前检查有限性。

## 7. 缺帧策略

- 单指失效：上一姿态 `Held`，置信度随时间下降；超过超时后平滑回中立位。
- 单指恢复：限速插值，来源短时为 `Recovered`。
- 手腕失效：冻结全手相对解算并发出高严重度诊断。
- 一指失效不改变其他有效指。

## 8. 单元测试

- 三轴公共手掌旋转被相对姿态抵消。
- 安装修正乘法顺序和逐指隔离。
- flexion/abduction/twist 正负方向。
- 拇指与长指使用不同参数。
- bind pose 下骨骼不变。
- 每关节不越限，锁轴为零。
- 骨长变化≤`1e-4`。
- 重复同帧求解确定一致。
- 单指 held、衰减、回中、recovered；其他指不受影响。
- 手腕失效冻结整手。
- 虚拟解剖链与 GLB palette 名称映射完整。
- NaN/Inf/零四元数不进入输出。

## 9. 验收标准

- 输出完整 `HandSkeletonFrame`，25 个骨骼字段齐全。
- 所有多关节输出标记 `Estimated` 或 `Held/Recovered`，不得标记为直接测量。
- 骨长、限位、有限性和确定性测试通过。
- 目标 GLB palette 映射 25/25 完整。
- 手掌整体旋转不会显著改变静止手指观测。

## 10. 禁止事项

- 不依赖 GLB 节点 children 建立解剖父链。
- 不直接修改指尖位置。
- 不在显示层做安装修正。
- 不累计上一帧骨骼矩阵。
- 不把估计关节称为测量关节。

## 11. 完成报告要求

报告配置路径、25 关节映射表、测试结果、最大骨长误差、限位结果、缺帧状态转换和已知动作轴假设。