# SubStage 2：输入、协议、录制与回放

- 所属 Stage：SubStage 2
- 依赖前置：等待 SubStage 1；在 `src/core/` 获取统一帧契约和 schema 版本
- 并行状态：依赖满足后，可与 SubStage 3、5 并行
- 所属阶段：阶段三·开发

## 1. 背景与目标

建立串口与回放共享的数据入口，复用现有 25 字节六路原始帧解析和同 sequence 分组，实现有界缓存、全链路统计、原始数据录制与确定性回放。真实输入和回放必须从同一 `bytesReady` 接口进入解析器。

## 2. 当前代码状态

- `src/frame_stream_parser.h:12` 已提供 `appendBytes(bytes, monotonicNs)`。
- `src/sequence_grouper.h:12` 已提供 maxPending，`:20`/`:21` 输出完整组和丢弃的部分组。
- `src/serial_data_source.h` 和 `src/demo_data_source.h` 为现有输入源。
- `src/imu_types.h:77` 与 `:87` 已有解析和分组统计，但缺队列水位、丢组原因与录制统计。
- 需求协议为 25 字节：`AA 55 | address | sequence | 0x12 | 18-byte axes | CRC16-Modbus LE`。

## 3. 修改范围

- 整理 `src/input/`、`src/protocol/`；可通过兼容头逐步迁移旧路径。
- 新增 `IDataSource`、`SerialDataSource`、`ReplayDataSource`。
- 新增 `src/recording/session_recorder.*`、`recording_schema.*`、`replay_controller.*`。
- 新增 `tools/hardware_baseline_capture.cpp`。
- 新增协议、录制、回放和硬件基线测试。

## 4. 接口契约

```cpp
class IDataSource : public QObject {
  Q_OBJECT
signals:
  void bytesReady(const QByteArray&, qint64 monotonicNs);
  void stateChanged(SourceState, const Diagnostic&);
public slots:
  virtual void start() = 0;
  virtual void stop() = 0;
};
```

- `FrameStreamParser` 输出 `RawImuFrame`。
- `SequenceGrouper` 只把 `complete && presentMask==0x3f` 发送到默认下游。
- 重复节点：首帧保留，后续丢弃并计数。
- pending 默认上限 8，淘汰最旧组。
- `SessionRecorder` 支持 idle/recording/paused/stopping，暂停期不写入。
- 录制目录和文件格式按设计文档第 10 节。
- `ReplayDataSource` 读取 `raw.bin`，支持原速、暂停、逐完整组和无限速。

## 5. 硬件基线采集

使用真实六路设备执行 `hardware_baseline_capture`，产出：

- `testdata/hardware/<dataset-id>/raw.bin`
- `testdata/hardware/<dataset-id>/metadata.json`
- 至少包含串口参数、设备/固件、量程、轴向、采样率、六路安装位置、采集动作与配置版本。

若量程、轴向或安装方向无法从硬件资料/实测确定，停止依赖这些字段的后续任务并报告 MainAgent；禁止填入猜测值。

## 6. 单元测试

- 任意分包、粘包、前导噪声、错误长度、错误 CRC 后重同步。
- 未知地址不进入分组。
- sequence 交错、重复和 255→0 回绕。
- pending 始终不超过配置上限。
- 录制状态机、暂停、二次 start 错误、磁盘失败诊断。
- `raw.bin` 字节与输入完全一致。
- 同一 `raw.bin` 两次回放得到相同组序列和统计。
- 有界写队列溢出策略与计数。

## 7. 验收标准

- 协议和录制回放单测全部通过。
- 真实硬件至少采集一份可重复解析的六路数据集；若硬件连接被客观阻塞，任务不得宣告完整完成，必须附阻塞证据。
- 正常数据 `presentMask` 恒为 `0x3f`，CRC 错误率和完整组率可统计。
- 运行时所有缓存有明确上限。

## 8. 禁止事项

- 不实现姿态融合或骨骼算法。
- 不把不同 sequence 拼成一组。
- 不以 demo 数据替代真实硬件基线结论。
- 不在回放层跳过协议解析直接构造融合对象。

## 9. 完成报告要求

列出修改文件、测试、硬件命令、设备端口、数据集路径、解析统计和任何未知硬件参数。