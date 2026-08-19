# WIT IMU Qt 实时数据示例

这是原 WIT BLE 5.0 Android 示例的 Qt 6 桌面版本。程序使用 Qt Bluetooth 扫描并连接名称以 `WT` 开头的兼容传感器，解析真实 BLE 通知，并通过标准 Qt Widgets 实时显示数值。

## 功能

- 扫描、筛选和连接单个 WIT BLE 设备
- 订阅姿态通知并处理 BLE 分包、粘包和前导噪声
- 显示三轴加速度、角速度、角度和磁场
- 周期读取并显示电量、温度和固件版本
- 显示连接状态、最近更新时间和真实解析帧计数
- 提供明确标识的演示模式用于无硬件 UI 验证

本版本不包含三维姿态、曲线、动画、数据记录、恢复出厂、回传率设置、带宽设置或校准操作。

## 依赖

- CMake 3.20 或更高版本
- 支持 C++17 的编译器
- Qt 6：`Core`、`Widgets`、`Bluetooth`、`Test`
- Windows 实机连接需要可用的 Bluetooth Low Energy 适配器和驱动

## 构建

在本目录执行：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug -j8
```

如果 CMake 无法找到 Qt，可指定：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="D:/Devtools/Qt/6.8.3/msvc2022_64"
```

## 运行

```powershell
.\build\Debug\IMUExampleQt.exe
```

自动启动演示模式并在 3 秒后退出：

```powershell
.\build\Debug\IMUExampleQt.exe --demo --quit-after-ms 3000
```

演示模式的数据不是来自真实设备，窗口状态会持续显示“演示模式 · 非真实设备数据”。

## 测试

```powershell
ctest --test-dir build -C Debug --output-on-failure
```

测试覆盖协议换算、正负值、BLE 分包与粘包、噪声同步、电量边界、版本解析、数值格式和演示模式 UI 数据链路。

连接真实设备进行自动诊断：

```powershell
.\build\Debug\ble_hardware_diagnostic.exe WT901BLE67
```

诊断程序会在 30 秒内扫描并连接指定名称设备，输出连接状态、真实姿态帧数量、轮询寄存器响应情况和最后一组数值。

## BLE 协议

| 类型 | UUID |
| --- | --- |
| Service | `0000ffe5-0000-1000-8000-00805f9a34fb` |
| Notify | `0000ffe4-0000-1000-8000-00805f9a34fb` |
| Write | `0000ffe9-0000-1000-8000-00805f9a34fb` |

程序解析固定 20 字节的 `55 61` 姿态帧和 `55 71` 寄存器返回帧。磁场、电量、温度和版本通过写特征每 500 ms 轮询一项。

## 字段单位

| 字段 | 单位 |
| --- | --- |
| 加速度 | g |
| 角速度 | °/s |
| 角度 | ° |
| 磁场 | 协议值，原示例未明确物理单位 |
| 电量 | %，由设备电压分段估算 |
| 温度 | ℃ |

## 真实设备使用

1. 打开 Windows 蓝牙和 WIT 设备。
2. 确保设备没有连接到其他手机或电脑。
3. 点击“扫描”，选择列表中的设备并点击“连接”。
4. 状态显示“设备已连接”后观察实时数据。
5. 完成后点击“断开”。

真实硬件兼容性取决于设备名称、上述 UUID、`55 61`/`55 71` 帧协议和 Windows BLE 驱动。演示模式及单元测试不能替代真实硬件验收。

## 已知限制

- 同时只能连接一个设备。
- 未实现断线自动重连。
- 不保存原始帧或历史数据。
- 没有兼容 WIT 设备时只能验证协议单元测试和演示模式。
