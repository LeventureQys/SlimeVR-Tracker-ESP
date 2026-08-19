# WIT BLE 5.0 Android 示例

这是一个使用 Java 编写的 Android BLE 5.0 示例项目，用于扫描、连接并配置以 `WT` 开头的 WIT 蓝牙姿态传感器。项目包含可直接运行的示例应用和独立的 `WitSDK` 模块，可作为 WIT 传感器接入其他 Android 应用时的参考。

## 功能特性

- 扫描附近的 BLE 设备，并筛选名称以 `WT` 开头的传感器
- 自动建立 GATT 连接并订阅传感器通知
- 同时管理和展示多个已发现设备
- 实时解析并显示以下数据：
  - 三轴加速度 `AccX`、`AccY`、`AccZ`
  - 三轴角速度 `AsX`、`AsY`、`AsZ`
  - 三轴角度 `AngleX`、`AngleY`、`AngleZ`
  - 三轴磁场 `HX`、`HY`、`HZ`
  - 电量、温度和固件版本
- 支持恢复出厂设置
- 支持设置回传速率和带宽
- 支持角度归零和磁场校准

## 项目结构

```text
wit-example-ble5/
├── app/                         # Android 示例应用
│   └── src/main/
│       ├── java/com/wit/example/
│       │   ├── MainActivity.java    # 权限申请、设备扫描与设备列表
│       │   ├── DeviceActivity.java  # 数据展示与传感器配置
│       │   └── UI/                  # 设备列表适配器
│       ├── res/                     # 布局、字符串和主题资源
│       └── AndroidManifest.xml      # 蓝牙与定位权限声明
├── WitSDK/                      # BLE 通信与 WIT 协议解析模块
│   └── src/main/java/com/wit/witsdk/
│       ├── Bluetooth/               # BLE 扫描和权限管理
│       └── Device/                  # 设备连接、事件、数据解析与管理
├── gradle/wrapper/              # Gradle Wrapper
├── build.gradle                 # 根项目构建配置
└── settings.gradle              # app 与 WitSDK 模块声明
```

## 开发环境

| 项目 | 配置 |
| --- | --- |
| 开发工具 | Android Studio |
| 开发语言 | Java 8 |
| Android Gradle Plugin | 8.1.0 |
| Gradle | 8.0 |
| `compileSdk` | 33 |
| `targetSdk` | 33 |
| `minSdk` | 24（Android 7.0） |

运行设备还需要：

- 支持 Bluetooth Low Energy
- 已开启蓝牙
- 已开启系统定位服务；部分 Android 版本要求开启定位后才能获得 BLE 扫描结果
- 已授予应用请求的蓝牙和定位权限
- 一台广播名称以 `WT` 开头、并使用本项目 GATT UUID 的兼容 WIT 传感器

## 快速开始

### 使用 Android Studio

1. 使用 Android Studio 打开本项目根目录。
2. 等待 Gradle Sync 完成，并确认已安装 Android SDK 33。
3. 连接 Android 真机。BLE 功能建议使用真机测试，不建议依赖模拟器。
4. 选择 `app` 运行配置并启动应用。
5. 首次启动时允许蓝牙和定位权限。

### 使用命令行构建

Windows：

```powershell
.\gradlew.bat assembleDebug
```

macOS 或 Linux：

```bash
./gradlew assembleDebug
```

构建产物默认位于：

```text
app/build/outputs/apk/debug/app-debug.apk
```

安装到已连接的 Android 设备：

```powershell
.\gradlew.bat installDebug
```

运行本地单元测试：

```powershell
.\gradlew.bat test
```

## 使用方法

1. 打开 WIT 传感器，并确保设备处于可被扫描状态。
2. 启动应用，按系统提示授予权限。
3. 打开主界面的“开始搜索”开关。
4. 应用会以低延迟模式扫描 BLE 设备，并在 10 秒后自动停止扫描。
5. 名称以 `WT` 开头的设备会显示在列表中，并自动开始连接。
6. 点击设备条目进入详情页，查看实时姿态、磁场、电量、温度和版本数据。
7. 在详情页可执行恢复出厂、设置回传速率、设置带宽、角度参考和磁场校准。

> 注意：回传速率、带宽、校准和恢复出厂等操作会修改传感器配置。请确认设备型号及协议兼容后再操作。

## 权限说明

项目会根据系统版本申请相应权限：

| Android 版本 | 主要权限 |
| --- | --- |
| Android 12 及以上 | `BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT`、`ACCESS_FINE_LOCATION` |
| Android 10～11 | `BLUETOOTH`、`BLUETOOTH_ADMIN`、`ACCESS_FINE_LOCATION` |
| Android 9 及以下 | 在上述旧版蓝牙权限基础上增加 `ACCESS_COARSE_LOCATION` |

权限声明位于 `app/src/main/AndroidManifest.xml`，运行时申请逻辑位于 `WitBluetoothManager.requestPermissions()`。

## BLE 通信说明

`DeviceModel` 使用以下 UUID 与传感器通信：

| 类型 | UUID |
| --- | --- |
| Service | `0000ffe5-0000-1000-8000-00805f9a34fb` |
| Notify/Read | `0000ffe4-0000-1000-8000-00805f9a34fb` |
| Write | `0000ffe9-0000-1000-8000-00805f9a34fb` |

连接后的主要流程如下：

1. `WitBluetoothManager` 扫描附近的 BLE 设备。
2. `MainActivity` 筛选 `WT` 前缀设备并创建 `DeviceModel`。
3. `DeviceModel` 建立 GATT 连接、发现服务并启用通知。
4. 通知数据由 `OnReceiveBle()` 按 20 字节数据帧解析。
5. `DeviceManager` 统一保存设备，并向监听器分发设备发现、数据和连接状态事件。

## 在其他项目中使用 WitSDK

当前示例通过 Gradle 模块依赖引用 SDK：

```groovy
dependencies {
    implementation project(':WitSDK')
}
```

接入时需要完成以下步骤：

1. 将 `WitSDK` 模块复制到目标 Android 项目。
2. 在目标项目的 `settings.gradle` 中加入：

   ```groovy
   include ':WitSDK'
   ```

3. 在应用模块中添加 `implementation project(':WitSDK')`。
4. 在应用清单中声明所需蓝牙和定位权限。
5. 在 Activity 中申请权限，并注册设备发现及数据监听器。

核心调用示例：

```java
WitBluetoothManager.requestPermissions(this);

DeviceManager deviceManager = DeviceManager.getInstance();
deviceManager.AddDeviceFindListener(this);
deviceManager.AddDeviceListener(this);

WitBluetoothManager bluetoothManager =
        WitBluetoothManager.getInstance(this);
bluetoothManager.startScan();
```

使用完毕后应停止扫描、关闭设备连接并移除监听器，避免 Activity 泄漏或后台持续占用蓝牙资源。

## 数据单位

根据当前解析公式，各字段含义如下：

| 字段 | 含义 | 单位 |
| --- | --- | --- |
| `AccX/Y/Z` | 三轴加速度 | g |
| `AsX/Y/Z` | 三轴角速度 | °/s |
| `AngX/Y/Z` | 三轴角度 | ° |
| `HX/HY/HZ` | 三轴磁场 | 取决于设备协议标定 |
| `Electricity` | 估算剩余电量 | % |
| `Temperature` | 设备温度 | ℃ |
| `VersionNumber` | 固件版本 | 版本字符串 |

## 常见问题

### 搜索不到设备

- 确认传感器已开机且未被其他手机连接。
- 确认应用已获得蓝牙及定位权限。
- 确认手机蓝牙和系统定位服务均已开启。
- 当前示例只在列表中显示名称以 `WT` 开头的设备；其他名称会被忽略。
- 扫描会在 10 秒后自动停止，可关闭再重新打开搜索开关。

### 能找到设备但没有数据

- 确认设备提供的 Service、Notify 和 Write UUID 与项目配置一致。
- 确认设备支持本示例解析的 `0x55 0x61` 和 `0x55 0x71` 数据帧。
- 使用 Android Studio Logcat 搜索标签 `WitLOG`，查看连接、服务发现和写入日志。

### Gradle 同步或构建失败

- 确认网络可以访问 `google()`、`mavenCentral()` 和 Gradle 分发地址。
- 确认使用与 Android Gradle Plugin 8.1.0 兼容的 JDK；通常建议使用 JDK 17。
- 确认已通过 Android SDK Manager 安装 Android SDK 33。
- 如 `local.properties` 中的 `sdk.dir` 不适用于当前电脑，请让 Android Studio 重新生成或手动修改该路径。

## 已知限制

- 设备筛选规则固定为名称以 `WT` 开头。
- GATT UUID 和传感器协议固定在 `DeviceModel` 中，其他型号可能需要适配。
- 扫描默认持续 10 秒，当前界面不会同步重置搜索开关状态。
- 示例以演示设备接入为主，未实现断线自动重连、后台服务和数据持久化。
- 仓库中的测试文件为 Android Studio 默认示例，未覆盖真实 BLE 设备通信。

## 许可证

本项目当前未提供明确的开源许可证。复制、修改或分发代码前，请先确认代码所有者和设备厂商的授权条款。
