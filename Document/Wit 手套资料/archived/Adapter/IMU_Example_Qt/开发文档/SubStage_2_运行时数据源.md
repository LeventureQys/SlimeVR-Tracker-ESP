# SubStage 任务书：运行时数据源

## SubStage: BLE 会话与演示源
- 所属 Stage：Stage 2
- 依赖前置：SubStage 1 的 `src/imu_data.h`
- 并行状态：BLE 管理器与演示源可并行
- 所属阶段：阶段三 - 开发

## 背景与目标

实现真实 Qt Bluetooth Central 会话和无硬件演示数据源，两者向上层提供互斥的数据来源。

## 当前状态

Android 参考连接实现位于 `../wit-example-ble5/wit-example-ble5/WitSDK/src/main/java/com/wit/witsdk/Device/DeviceModel.java:65`，扫描实现位于 `../wit-example-ble5/wit-example-ble5/WitSDK/src/main/java/com/wit/witsdk/Bluetooth/WitBluetoothManager.java:173`。

## 必须创建

- `src/wit_ble_manager.h`
- `src/wit_ble_manager.cpp`
- `src/demo_data_source.h`
- `src/demo_data_source.cpp`

## 输入输出

- BLE 输入：`QBluetoothDeviceInfo`、Qt Bluetooth 状态信号和 Notify 特征字节。
- BLE 输出：筛选后的设备、状态、错误和原始通知字节。
- 演示输入：`start()` / `stop()`。
- 演示输出：每 100 ms 一份 `ImuData`。

## 接口契约

严格实现 `设计文档.md` 第 9.2、9.3 节。UUID、CCCD、四条轮询命令、500 ms 间隔和状态枚举不得变更。

设备去重键：优先使用 `deviceUuid().toString()`；若为空则使用 `address().toString()`；两者都为空时使用设备名和扫描信息可用标识组合。列表显示地址为空时显示设备 UUID。

对象所有权：

- `WitBleManager` 独占 discovery agent、controller、service 和 polling timer。
- 使用 QObject 父子关系或 `std::unique_ptr`，不得保留悬空 Qt 指针。
- 创建新 controller 前释放旧 service/controller。
- 析构必须调用等价于 `stopScan()` 和 `disconnectDevice()` 的清理。

## 错误处理

- 所有 Qt Bluetooth 错误转为稳定中文消息并发 `errorOccurred`。
- 目标服务、Notify、Write 或 CCCD 缺失时进入 `Error` 并断开。
- 非目标特征通知忽略。
- 未连接、写特征无效或队列为空时 polling tick 不写入。
- 断开后清空命令索引和定时器。

## 验收标准

- 编译时只依赖 Qt `Core` 和 `Bluetooth`。
- 扫描只发布 `WT` 前缀设备且同一扫描周期不重复发布。
- 成功订阅后才报告 `Connected` 并启动轮询。
- 轮询命令顺序和间隔符合设计。
- 演示数据每次更新帧计数，固件版本明确包含 `Demo`。
- 演示源多次 `start()` / `stop()` 幂等且不产生重复定时器。

## 禁止事项

- 不直接解析协议或更新 UI。
- 不创建阻塞线程，不调用 `sleep`。
- 不自动重连，不同时连接多个设备。
- 不发送恢复出厂、保存配置或校准命令。
