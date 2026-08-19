# SubStage 任务书：协议核心

## SubStage: WIT 协议与数据模型
- 所属 Stage：Stage 1
- 依赖前置：无依赖
- 并行状态：独立可执行
- 所属阶段：阶段三 - 开发

## 背景与目标

实现与 BLE 和 UI 解耦的 WIT 20 字节流解析器，为真实通知和单元测试提供唯一协议实现。

## 当前状态

目标目录尚无代码。协议依据为 `../wit-example-ble5/wit-example-ble5/WitSDK/src/main/java/com/wit/witsdk/Device/DeviceModel.java:202`，完整设计见 `设计文档.md` 第 7、8、9.1 节。

## 输入输出

- 输入：任意长度 `QByteArray`，可能是空数据、噪声、半帧、完整帧或多帧。
- 输出：最新 `ImuData` 快照和每个支持帧一次 `dataUpdated(const ImuData &)` 信号。

## 必须创建

- `src/imu_data.h`
- `src/wit_protocol_parser.h`
- `src/wit_protocol_parser.cpp`
- `tests/test_wit_protocol.cpp`

## 接口契约

严格实现 `设计文档.md` 第 7、8、9.1 节的 `ImuData`、`WitProtocolParser`、换算、版本、电量及流缓存规则。`ImuData` 需使用 `Q_DECLARE_METATYPE(ImuData)`，以支持 `QSignalSpy`。

## 错误处理

- 不抛异常。
- 无效输入不崩溃。
- 不完整帧保留等待。
- 噪声丢弃后继续同步。
- 未知 `0x71` 寄存器消费但不发 `dataUpdated`。

## 单元测试

使用 QtTest 数据驱动测试，至少覆盖：

1. 全零姿态帧。
2. 正负值和小端字节序。
3. 分包与粘包。
4. 噪声及末尾单字节 `55`。
5. 磁场、温度和全部电量阈值的等于/略大边界。
6. 有效与无效版本。
7. 未知寄存器。
8. `reset()`。

浮点比较使用明确容差，不比较格式化字符串。

## 验收标准

- `test_wit_protocol` 编译并全部通过。
- 分包输入在收到第 20 字节前不发信号，完成后恰好发一次。
- 两帧粘包恰好发两次信号，帧计数为 2。
- 公式与设计文档逐项一致。

## 禁止事项

- 不依赖 Qt Bluetooth 或 Widgets。
- 不提前格式化或四舍五入协议值。
- 不增加未知校验和。
- 不修改其他 SubStage 文件。
