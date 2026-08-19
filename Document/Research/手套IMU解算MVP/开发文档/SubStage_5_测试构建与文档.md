# SubStage 5：测试、构建与文档任务书

- 所属 Stage：Stage 5 / 集成验证准备
- 依赖前置：等待 SubStage 1 至 4 的源码与测试
- 并行状态：需等待核心目标可用
- 所属阶段：阶段三·开发

## 1. 背景与目标

完成独立 CMake、剩余 UI/设置测试、README、CTest 注册、启动测试入口，并执行阶段三要求的构建、单元测试和演示启动测试。

## 2. 修改范围

- `CMakeLists.txt`
- `README.md`
- `tests/test_main_window.cpp`
- 必要时补充现有 `tests/*.cpp` 的 CTest 注册，不改其业务断言含义

不得修改需求、设计或验收标准；发现设计无法实现时报告 MainAgent。

## 3. CMake 契约

- CMake 3.20；C++17；AUTOMOC；MSVC `/utf-8`；
- `find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets SerialPort Test)`；
- `six_imu_core`：协议、设置、融合；
- `six_imu_runtime`：串口、demo、dialog、panel、window；
- `SixImuSolverQt`；
- 五个测试目标：protocol、sequence_grouper、madgwick_filter、settings、main_window；
- `include(CTest)` 和 `add_test`；
- UI 测试设置 `QT_QPA_PLATFORM=offscreen`。

## 4. UI 自动化测试

覆盖：

- 主控件 objectName；
- 六 SensorPanel；
- 注入快照显示 raw、四元数、欧拉角和状态；
- 演示开启时串口控件禁用，关闭后恢复；
- Dialog Cancel 不改配置；
- Restore 后 Cancel 不保存；
- Restore 后 OK 返回默认；
- 非法 min/max 不 accept；
- 标定按钮有效性；
- 应用设置后融合/零位重置提示。

设置测试必须使用临时 INI，不污染生产注册表。

## 5. README

至少包含：功能、范围外、依赖、Qt 路径、配置构建、测试、运行、演示启动、真实串口步骤、默认换算、参数 Dialog、QSettings 标识、零位标定、FusionMode、已知限制和真实硬件验收要求。

## 6. 执行命令

以本机 Qt 路径为准：

```powershell
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="<Qt6>"
cmake --build build --config Debug -j8
ctest --test-dir build -C Debug --output-on-failure
.\build\Debug\SixImuSolverQt.exe --demo --quit-after-ms 3000
```

启动测试记录命令、退出码、stdout/stderr、运行时间；不得仅报告“打开过”。

## 7. 验收标准

- Debug 构建通过；
- 所有 QtTest 通过；
- 演示启动 3 秒正常退出；
- 无 Qt fatal/critical；
- README 可让零上下文用户构建运行；
- 测试失败必须定位所属 SubStage，不能隐藏或跳过；
- 完成后提交阶段三完成报告草稿所需证据。

## 8. 禁止事项

- 禁止把失败测试标记 disabled；
- 禁止降低断言绕过错误；
- 禁止以编译通过替代启动测试；
- 禁止把演示测试替代真实硬件；
- 禁止修改仓库根 CMake；
- 遇 Qt 缺失或不可启动，立即报告 MainAgent并记录阻塞。
