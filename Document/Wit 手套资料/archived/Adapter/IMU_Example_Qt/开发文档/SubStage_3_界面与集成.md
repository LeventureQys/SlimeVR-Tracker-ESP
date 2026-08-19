# SubStage 任务书：界面与集成

## SubStage: Widgets 展示、构建与启动
- 所属 Stage：Stage 3
- 依赖前置：SubStage 1、SubStage 2
- 并行状态：需等待
- 所属阶段：阶段三 - 开发

## 背景与目标

使用标准 Qt Widgets 把扫描、连接、协议解析和演示数据源组合成可运行桌面程序，只展示实时数值，不进行图形渲染。

## 当前状态

界面字段来源于 Android `DeviceActivity.java:148`，Qt 界面和对象名契约见 `设计文档.md` 第 9.4、10 节。

## 必须创建

- `src/main_window.h`
- `src/main_window.cpp`
- `src/main.cpp`
- `tests/test_main_window.cpp`
- `CMakeLists.txt`
- `README.md`

## 输入输出

- 输入：`WitBleManager` 设备和状态信号、`WitProtocolParser::dataUpdated`、`DemoDataSource::dataUpdated`、用户按钮事件。
- 输出：标准 Widgets 中的设备列表、连接状态和最新 `ImuData` 字段。

## 集成契约

连接关系：

```text
WitBleManager::notificationReceived -> WitProtocolParser::appendBytes
WitProtocolParser::dataUpdated      -> MainWindow pending snapshot
DemoDataSource::dataUpdated         -> MainWindow pending snapshot
```

- 真实扫描前停止演示源。
- 演示开启前停止扫描、断开 BLE、重置解析器。
- 连接新设备和断开时重置解析器与表格快照。
- UI 每 100 ms 最多刷新一次，不逐 BLE 帧操作 15 行单元格。
- 表格、按钮及标签必须使用设计指定的 `objectName`。

## CMake 契约

- `cmake_minimum_required(VERSION 3.20)`。
- `project(IMUExampleQt VERSION 0.1.0 LANGUAGES CXX)`。
- C++17、`CMAKE_AUTOMOC ON`。
- `find_package(Qt6 REQUIRED COMPONENTS Core Widgets Bluetooth Test)`。
- 将协议代码建为可被应用和测试复用的 `wit_protocol` 静态库。
- 应用链接 `Qt6::Core`、`Qt6::Widgets`、`Qt6::Bluetooth`。
- 使用 `include(CTest)` 和 `BUILD_TESTING` 控制测试。
- 注册 `test_wit_protocol`、`test_main_window` 到 CTest。
- Windows 应用设置 `WIN32_EXECUTABLE TRUE`；测试保持控制台子系统。

## README 契约

README 需说明：功能边界、依赖、Windows 构建命令、运行命令、测试命令、BLE UUID、字段单位、演示模式、真实硬件要求和已知限制。不得声称未执行的硬件测试已经通过。

## UI 测试

- 使用 QtTest。
- 可设置 `QT_QPA_PLATFORM=offscreen` 执行。
- 验证所有指定 `objectName` 可找到。
- 验证数据表 15 行、3 列、只读。
- 提供测试可调用的数据注入槽或最小公开方法 `displayData(const ImuData &data)`；该方法只负责保存 pending 快照，实际刷新仍由统一方法完成。
- 验证数值精度、单位、帧计数和更新时间。
- 验证演示模式开关的按钮状态和状态文字。

## 错误处理

- 未选设备连接时显示提示，不弹阻塞式对话框。
- 蓝牙错误更新状态标签，程序保持可继续扫描。
- 关闭窗口期间不访问已销毁 BLE 对象。

## 验收标准

- 独立工程可配置、构建和启动。
- 两个测试目标全部通过。
- 演示模式运行 2 秒时帧计数和至少一个数值发生变化。
- UI 无三维、图表、动画和设备配置控件。
- README 命令可直接复制执行。

## 禁止事项

- 不修改仓库根工程和 Android 参考项目。
- 不使用 QML、Qt Charts、OpenGL 或定制绘制。
- 不在 UI 中伪装演示数据为真实数据。
- 不增加范围外配置按钮。
