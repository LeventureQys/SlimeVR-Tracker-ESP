# SubStage 1：协议与分组核心任务书

- 所属 Stage：Stage 1 / 协议数据核心
- 依赖前置：无
- 并行状态：独立可执行，可与 SubStage 2、3 并行
- 所属阶段：阶段三·开发

## 1. 背景与目标

在 `Document/Research/手套IMU解算MVP/` 建立六 IMU 二进制协议的纯核心模块，完成 CRC16、连续流切帧、九轴大端解码、六地址识别、同 `SEQ` 分组、错误统计与单元测试。该任务不得实现串口、姿态融合或 UI。

## 2. 当前状态

目标源码尚不存在。协议依据：

- `Document/Wit 手套资料/串口输出解析协议文档.md:33`：25 字节；
- 同文件 `:45`：字段偏移；
- 同文件 `:68`：地址；
- 同文件 `:81`：大端 int16；
- 同文件 `:100`：同 SEQ 分组；
- 同文件 `:127`：CRC16；
- `Document/Wit 手套资料/parse_six_imu.py:51`：流恢复；
- 同文件 `:106`：有界 pending。

## 3. 修改范围

只创建或修改：

- `src/imu_types.h`
- `src/protocol_constants.h`
- `src/crc16.h/.cpp`
- `src/frame_stream_parser.h/.cpp`
- `src/sequence_grouper.h/.cpp`
- `tests/test_protocol.cpp`
- `tests/test_sequence_grouper.cpp`

不得修改其他 SubStage 文件、参考程序或仓库根工程。

## 4. 完整接口契约

严格实现 `设计文档.md` 第 7、8、9 节的数据类型和接口。公开类型包括 `SensorId`、`RawAxes`、`ImuFrame`、`ImuSampleGroup`、`ParserStatistics`、`GroupStatistics`。

协议常量：帧头 `AA 55`、帧长 25、LEN `0x12`、前 23 字节 Modbus CRC16、CRC 低字节在前、九轴大端有符号。

解析器：

```cpp
void appendBytes(const QByteArray &, qint64 monotonicNs);
void reset();
ParserStatistics statistics() const;
```

信号：

```cpp
void frameParsed(const ImuFrame &);
void statisticsChanged(const ParserStatistics &);
```

分组器：

```cpp
void addFrame(const ImuFrame &);
void reset();
GroupStatistics statistics() const;
```

信号：

```cpp
void completeGroupReady(const ImuSampleGroup &);
void partialGroupDropped(const ImuSampleGroup &);
void statisticsChanged(const GroupStatistics &);
```

## 5. 行为要求

- 解析任意分包、粘包、噪声和残帧；
- 未找到帧头时保留末尾单字节 `AA`；
- 错误 LEN/CRC 只丢弃候选头首字节并继续同步；
- 未知地址计数并发出帧，但不进入分组；
- 全零帧有效并设置 `allZero=true`；
- pending 最大 8，按首次到达顺序淘汰；
- 不按 SEQ 数值排序，支持 FF→00；
- 重复地址用最新帧覆盖并计数；
- 所有容器有界，所有操作不抛异常。

## 6. 单元测试

`test_protocol` 覆盖：文档示例、CRC 已知值、正负边界、大端、逐字节分包、多帧粘包、噪声、半帧头、错误 LEN 恢复、错误 CRC 恢复、未知地址、全零、reset。

`test_sequence_grouper` 覆盖：六路任意顺序、交错 SEQ、重复覆盖、超 8 淘汰、缺失 mask、FF→00、未知地址、reset。

## 7. 验收标准

- 文件可被独立静态库编译；
- 两个 QtTest 可执行目标通过；
- 与 Python 参考行为一致；
- 无串口、Madgwick、UI 依赖；
- 完成后报告修改文件、测试命令和结果。

## 8. 禁止事项

- 禁止跳过 CRC；
- 禁止盲按 25 字节切分；
- 禁止清空整个缓存恢复单个错误；
- 禁止动态创建第七传感器；
- 禁止开始其他 SubStage；
- 遇到协议歧义立即停止并报告 MainAgent。
