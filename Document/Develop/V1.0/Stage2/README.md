# V1.0 阶段 2 推进索引

## 1. 当前目标

将 Windows 六 IMU Qt 工具接入 SlimeVR Server，作为一个左手或右手设备的六个姿态传感器，并完成协议、映射、坐标和 30 分钟稳定性 PoC。

## 2. 版本目录

| 版本 | 目录 | 当前状态 | 完成凭证 |
| --- | --- | --- | --- |
| `S2.1` | `S2.1_UDP协议与会话基座/` | ✅ 完成 | `完成报告.md` |
| `S2.2` | `S2.2_六传感器注册与姿态发送/` | ✅ 完成 | `完成报告.md` |
| `S2.3` | `S2.3_左右手映射与坐标转换/` | ✅ 完成（真实动作验证待硬件） | `完成报告.md` |
| `S2.4` | `S2.4_集成稳定性与PoC验收/` | ✅ 完成（真实 Server/长稳待硬件） | `完成报告.md` |

每个版本目录固定包含：

- `需求.md`：做什么和验收结果；
- `开发任务书.md`：Agent 修改范围、接口、步骤和测试命令；
- `问题清单.md`：未知项、阻断项、结论和依据；
- `完成报告.md`：修改文件、测试结果、决策和遗留。

## 3. 软件实现现状

- 全部代码位于 `Document/Research/手套IMU解算MVP/`；
- 网络模块：`slimevr_protocol`、`slimevr_udp_client`、`slimevr_sensor_mapping`、`slimevr_coordinate_transform`、`slimevr_pose_adapter`、`slimevr_pose_sender`、`slimevr_settings(_store)`、`slimevr_mount_dialog`；
- 自动化测试：`ctest` 共 11 个目标，全部通过；
- 无界面验证探针：`slimevr_bridge_probe.exe`。

## 4. 真实验收待办（需要用户硬件与 Server）

1. 启动真实 SlimeVR Server（v20.1.0 基线）；
2. 按 `S2.4/用户操作说明.md` 连接真实六 IMU 手套并应用设置；
3. 执行 `S2.3/动作验证记录.md` 左右手动作表；
4. 执行 `S2.4/稳定性测试记录.md` 30 分钟长稳；
5. 关闭 `Q-S2.3-04/05/06`、`Q-S2.4-01/02`。

## 5. 顶层资料

- `../需求说明.md`：V1.0 总体需求；
- `阶段2_分版本说明.md`：阶段 2 总体拆分、统一映射和完成标准。
