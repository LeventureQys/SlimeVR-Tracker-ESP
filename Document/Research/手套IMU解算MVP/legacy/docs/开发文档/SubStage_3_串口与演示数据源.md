# SubStage 3：串口与演示数据源任务书

- 所属 Stage：Stage 3 / 运行时数据源
- 依赖前置：使用 `ImuFrame` 协议格式；任务书已给出完整字节契约
- 并行状态：独立可执行，可与 SubStage 1、2 并行；集成时等待 SubStage 1 的 CRC 函数
- 所属阶段：阶段三·开发

## 1. 背景与目标

实现真实 `QSerialPort` 异步数据源与生成合法六 IMU 协议字节的演示数据源。两者只输出字节和单调时间，不解析协议、不融合、不操作 UI。

## 2. 修改范围

- `src/serial_data_source.h/.cpp`
- `src/demo_data_source.h/.cpp`

可在对应测试文件中添加数据源小型测试，但不得修改其他模块。

## 3. 串口接口

严格实现 `设计文档.md` 第 12.1 节：`SourceState`、`SerialPortDescriptor`、`SerialDataSource`。

配置固定：921600、8 数据位、无校验、1 停止位、无流控。`availablePorts()` 提供名称、描述、厂商、序列号。

状态流程：Closed → Opening → Open；关闭为 Closing → Closed；失败进入 Error。重复打开/关闭安全。打开新端口前释放旧端口。

`readyRead` 只调用 `readAll()` 并发出：

```cpp
void bytesReady(const QByteArray &, qint64 monotonicNs);
```

时间戳由已启动的 `QElapsedTimer::nsecsElapsed()` 产生。禁止 wait/sleep。

## 4. 演示源接口

严格实现 `设计文档.md` 第 12.2 节。5 ms 一组，地址 0x50..0x55，共六个 25 字节帧，同一个 `SEQ`。字段布局：

```text
AA 55 ADDR SEQ 12
AXH AXL AYH AYL AZH AZL
GXH GXL GYH GYL GZH GZL
MXH MXL MYH MYL MZH MZL
CRCL CRCH
```

九轴为大端 qint16；CRC 为前 23 字节 Modbus CRC16，低字节先。演示轨迹必须确定性、有限，包含静止重力和缓慢旋转，不使用随机数。

演示源应以确定性周期制造分包/粘包：例如每 10 组一次把 150 字节拆为 37/113 两块，其余一次发出。不得制造错误 CRC。

## 5. 生命周期

- `start()` 重置 SEQ、相位和时钟后启动；
- 重复 start 不创建多个定时器；
- `stop()` 停止；重复 stop 安全；
- 实例析构前停止；
- 真实串口和演示互斥由 MainWindow 集成层控制，数据源本身不互相依赖。

## 6. 可测试标准

- 串口空端口名产生 Error，不崩溃；
- 无串口也可枚举并返回空列表；
- 演示源运行至少两组，输出可由正式 parser 解码为六地址完整组；
- CRC 全部正确；
- SEQ 递增并可回绕；
- start/stop 幂等；
- 无阻塞调用。

## 7. 验收标准

- 文件独立编译并链接 Qt6::SerialPort；
- 演示字节走正式协议链路；
- 不依赖 Widgets；
- 完成后报告修改文件和验证结果。

## 8. 禁止事项

- 禁止演示源直接发送姿态对象；
- 禁止串口层解析帧；
- 禁止硬编码 UI 文本或控件；
- 禁止在 GUI 线程阻塞等待；
- 禁止后台启动替代服务器或外部进程；
- 遇不确定立即报告 MainAgent。
