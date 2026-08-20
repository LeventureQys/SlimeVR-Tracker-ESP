# SubStage 4：界面与集成任务书

- 所属 Stage：Stage 4 / UI 与完整链路集成
- 依赖前置：等待 SubStage 1、2、3 完成
- 并行状态：需等待；从 `src/` 获取协议、融合、设置、串口和演示类
- 所属阶段：阶段三·开发

## 1. 背景与目标

实现姿态设置 Dialog、六个 SensorPanel、主窗口、主程序及完整信号槽链路。真实串口与演示源互斥，数据必须经过 parser→grouper→solver，再以 33 ms 节流刷新 UI。

## 2. 修改范围

- `src/settings_dialog.h/.cpp`
- `src/sensor_panel.h/.cpp`
- `src/main_window.h/.cpp`
- `src/main.cpp`

不得修改已通过测试的核心算法，除非接口集成确有错误并先报告 MainAgent。

## 3. UI 契约

严格实现 `设计文档.md` 第 13、14、15 节全部 objectName、布局、按钮状态和生命周期。

六面板固定顺序：手腕、拇指、食指、中指、无名指、小指；2×3 布局放入滚动区。每路显示 raw 九轴、相对四元数、Roll/Pitch/Yaw、SEQ、更新时间、FusionMode、有效、全零、超时和标定状态。

格式：raw 整数；四元数 4 位小数；欧拉角 2 位；统计速率 1 位。超时阈值 500 ms，只改变状态，不删除最后值。

## 4. 设置 Dialog

- 构造时接收 `SolverSettings` 草稿；
- 不直接读写 `QSettings` 或融合器；
- Restore 只改草稿控件；
- Cancel 不产生配置；
- OK 校验后 accept；
- MainWindow 先调用 Store 保存，成功后 applySettings；失败保持旧配置；
- 保存成功提示重置融合与零位，串口保持打开。

## 5. 数据链路

连接：

```text
SerialDataSource::bytesReady ----+
DemoDataSource::bytesReady ------+-> FrameStreamParser::appendBytes
frameParsed -> SequenceGrouper::addFrame
completeGroupReady -> SixImuSolver::processCompleteGroup
snapshotReady -> MainWindow 保存 pendingSnapshot
33ms timer -> SensorPanel/统计显示
```

切换真实/演示前调用统一 resetPipeline。演示状态必须包含“非真实设备数据”。

## 6. 零位标定

按钮仅在最新六路姿态均有效时启用。点击调用 `calibrateZero()`；失败显示具体原因且不改变旧零位。成功后下一 UI 刷新显示六路 relative 接近单位四元数。Clear 清除零位。

## 7. `main.cpp`

- 设置 organization `SlimeVRResearch` 和 application `SixImuSolverQt`；
- 解析 `--demo`；
- 解析正整数 `--quit-after-ms`；
- 非法退出时间输出错误并返回非零；
- 创建显示 MainWindow；
- demo 参数启用演示；
- 单次 timer 正常退出。

## 8. 错误处理

- 串口错误显示状态，不自动演示；
- 协议错误只显示统计；
- 设置写失败使用单次错误框；
- 高频数据不得产生弹窗；
- 关闭窗口停止 demo、关闭串口、停止刷新 timer；
- 所有操作幂等。

## 9. 验收标准

- 六个面板与全部 objectName 可由 QtTest 查找；
- 演示模式 3 秒内持续更新六路；
- UI 无逐帧重绘；
- 配置取消/保存语义正确；
- 零位正确；
- 不包含曲线、三维或关节角；
- 完成后报告集成问题与手工观察结果。

## 10. 禁止事项

- 禁止在槽函数复制 Madgwick/CRC/四元数公式；
- 禁止 UI 直接操作 parser 私有缓存；
- 禁止逐帧刷新控件；
- 禁止将演示标为真实数据；
- 禁止绕过阶段接口扩展范围；
- 遇接口冲突停止并报告 MainAgent。
