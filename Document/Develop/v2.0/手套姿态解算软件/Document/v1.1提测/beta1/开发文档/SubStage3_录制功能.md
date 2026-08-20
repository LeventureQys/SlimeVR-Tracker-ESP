# 任务书：SubStage 3 — IMU 数据录制功能

- **所属 Stage**：阶段三·开发
- **依赖前置**：无（独立可执行，但 UI 集成依赖 SubStage 2 的布局修改）
- **并行状态**：DataRecorder 类可独立开发；UI 集成部分需等 SubStage 2 完成后合并
- **所属版本**：v1.1 beta1

---

## 1. 目标

新增 IMU 数据录制功能：开始/暂停/继续/停止录制，录制时间显示，CSV 文件输出。

## 2. 文件清单

| 文件 | 操作 | 说明 |
| --- | --- | --- |
| `archived/src/recorder/data_recorder.h` | **新增** | DataRecorder 类头文件 |
| `archived/src/recorder/data_recorder.cpp` | **新增** | DataRecorder 类实现 |
| `archived/src/ui/main_window.h` | **修改** | 新增录制相关成员声明 |
| `archived/src/ui/main_window.cpp` | **修改** | 录制 UI 构建 + 信号连接 |
| `archived/CMakeLists.txt` | **修改** | 新增 recorder 源文件 |

## 3. DataRecorder 类设计

### 3.1 头文件 (`data_recorder.h`)

```cpp
#pragma once

#include "imu/imu_data.h"

#include <QDateTime>
#include <QObject>
#include <QElapsedTimer>
#include <QString>

#include <memory>

class QFile;
class QTextStream;

namespace handdemo::recorder {

enum class RecorderState {
    Idle,
    Recording,
    Paused
};

class DataRecorder final : public QObject {
    Q_OBJECT

public:
    explicit DataRecorder(QObject *parent = nullptr);
    ~DataRecorder() override;

    bool startRecording(const QString &baseDir = QStringLiteral("recordings"));
    void pauseRecording();
    void resumeRecording();
    void stopRecording();

    RecorderState state() const { return state_; }
    qint64 elapsedMs() const;  // 实际录制时间（排除暂停）

public slots:
    void recordFrame(const handdemo::imu::ImuData &data);

signals:
    void stateChanged(handdemo::recorder::RecorderState state);
    void errorOccurred(const QString &message);

private:
    bool openFile(const QString &folderPath);
    void closeFile();

    RecorderState state_{RecorderState::Idle};
    std::unique_ptr<QFile> file_;
    std::unique_ptr<QTextStream> stream_;
    QElapsedTimer elapsedTimer_;
    qint64 accumulatedMs_{0};  // 累计录制时间（暂停前）
    qint64 pauseStartMs_{0};
    int frameCount_{0};
};

}
```

### 3.2 实现文件 (`data_recorder.cpp`)

关键实现要点：

**startRecording**：
1. 生成文件夹名：`baseDir/rec_YYYYMMDD_HHMMSS/`
2. `QDir().mkpath(folderPath)` 创建目录
3. 打开 `imu_data.csv`，写入表头
4. 启动 `elapsedTimer_`
5. 状态 → Recording，emit stateChanged

**pauseRecording**：
1. 记录 `pauseStartMs_ = elapsedTimer_.elapsed()`
2. `accumulatedMs_ += pauseStartMs_`
3. 状态 → Paused，emit stateChanged

**resumeRecording**：
1. `elapsedTimer_.restart()`
2. 状态 → Recording，emit stateChanged

**stopRecording**：
1. closeFile()
2. 状态 → Idle，emit stateChanged

**recordFrame**：
1. 仅在 `state_ == Recording` 时写入
2. 格式：`timestamp_ms,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,angle_x,angle_y,angle_z,mag_x,mag_y,mag_z,battery_pct,temp_c,frame_count`
3. 使用 `lastUpdated.toMSecsSinceEpoch()` 作为 timestamp_ms
4. 浮点数精度：6 位小数

**elapsedMs**：
```cpp
if (state_ == Paused) return accumulatedMs_;
if (state_ == Recording) return accumulatedMs_ + elapsedTimer_.elapsed();
return 0;
```

### 3.3 CSV 表头

```
timestamp_ms,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,angle_x,angle_y,angle_z,mag_x,mag_y,mag_z,battery_pct,temp_c,frame_count
```

## 4. MainWindow 集成

### 4.1 新增头文件成员 (`main_window.h`)

在 `handdemo::recorder` 前向声明：
```cpp
namespace handdemo::recorder {
class DataRecorder;
enum class RecorderState;
}
```

新增成员：
```cpp
// 录制相关
recorder::DataRecorder *recorder_{nullptr};
QPushButton *recordStartButton_{nullptr};
QPushButton *recordPauseButton_{nullptr};
QPushButton *recordStopButton_{nullptr};
QLabel *recordTimeLabel_{nullptr};
QTimer *recordDisplayTimer_{nullptr};
```

### 4.2 UI 构建 (`main_window.cpp`)

在 `buildRealImuPanel()` 方法中，在现有控件之后（约 274 行 `layout->addWidget(imuFrameLabel_);` 之后）添加录制控件：

```cpp
// 录制控件
auto *recordSeparator = new QFrame(imuPanel_);
recordSeparator->setFrameShape(QFrame::HLine);
layout->addWidget(recordSeparator);

auto *recordLabel = new QLabel(QStringLiteral("数据录制"), imuPanel_);
recordLabel->setStyleSheet(QStringLiteral("font-weight: bold;"));
layout->addWidget(recordLabel);

recordStartButton_ = new QPushButton(QStringLiteral("开始录制"), imuPanel_);
recordPauseButton_ = new QPushButton(QStringLiteral("暂停"), imuPanel_);
recordStopButton_ = new QPushButton(QStringLiteral("停止"), imuPanel_);
recordTimeLabel_ = new QLabel(QStringLiteral("00:00"), imuPanel_);
recordTimeLabel_->setStyleSheet(QStringLiteral("font-family: monospace; font-size: 14px;"));

auto *recordButtonRow = new QHBoxLayout;
recordButtonRow->addWidget(recordStartButton_);
recordButtonRow->addWidget(recordPauseButton_);
recordButtonRow->addWidget(recordStopButton_);
layout->addLayout(recordButtonRow);
layout->addWidget(recordTimeLabel_);
```

### 4.3 信号连接

在构造函数中（`buildInterface()` 之后）：
```cpp
recorder_ = new recorder::DataRecorder(this);
recordDisplayTimer_ = new QTimer(this);
recordDisplayTimer_->setInterval(1000);
```

在 `buildRealImuPanel()` 末尾或构造函数中连接信号：
```cpp
connect(recordStartButton_, &QPushButton::clicked, this, [this] {
    if (recorder_->state() == recorder::RecorderState::Idle) {
        if (recorder_->startRecording()) {
            recordStartButton_->setEnabled(false);
            recordPauseButton_->setEnabled(true);
            recordStopButton_->setEnabled(true);
            recordDisplayTimer_->start();
        }
    }
});
connect(recordPauseButton_, &QPushButton::clicked, this, [this] {
    if (recorder_->state() == recorder::RecorderState::Recording) {
        recorder_->pauseRecording();
        recordPauseButton_->setText(QStringLiteral("继续"));
    } else if (recorder_->state() == recorder::RecorderState::Paused) {
        recorder_->resumeRecording();
        recordPauseButton_->setText(QStringLiteral("暂停"));
    }
});
connect(recordStopButton_, &QPushButton::clicked, this, [this] {
    recorder_->stopRecording();
    recordStartButton_->setEnabled(true);
    recordPauseButton_->setEnabled(false);
    recordStopButton_->setEnabled(false);
    recordPauseButton_->setText(QStringLiteral("暂停"));
    recordDisplayTimer_->stop();
    recordTimeLabel_->setText(QStringLiteral("00:00"));
});
connect(recordDisplayTimer_, &QTimer::timeout, this, [this] {
    const qint64 ms = recorder_->elapsedMs();
    const int secs = static_cast<int>(ms / 1000);
    recordTimeLabel_->setText(QStringLiteral("%1:%2")
        .arg(secs / 60, 2, 10, QLatin1Char('0'))
        .arg(secs % 60, 2, 10, QLatin1Char('0')));
});
// 录制数据连接：protocolParser_ 的 dataUpdated 同时喂给 recorder_
connect(protocolParser_, &imu::WitProtocolParser::dataUpdated, recorder_, &recorder::DataRecorder::recordFrame);
```

### 4.4 初始状态

录制按钮初始状态：start 启用，pause/stop 禁用。录制功能在所有模式下均可使用（不仅限于"真实单 IMU"模式），因为 `protocolParser_` 始终在接收数据。

### 4.5 需要新增的 include

```cpp
#include "recorder/data_recorder.h"
#include <QFrame>
```

## 5. CMakeLists.txt 修改

在 `archived/CMakeLists.txt` 的 `hand_rig_demo` 目标中添加新源文件：

找到约 46-51 行：
```cmake
add_executable(hand_rig_demo
    src/app/main.cpp
    src/ui/main_window.cpp
    src/ui/main_window.h
)
```
改为：
```cmake
add_executable(hand_rig_demo
    src/app/main.cpp
    src/ui/main_window.cpp
    src/ui/main_window.h
    src/recorder/data_recorder.cpp
    src/recorder/data_recorder.h
)
```

## 6. 验收标准

| # | 检查项 | 通过标准 |
| --- | --- | --- |
| 1 | 编译 | 构建成功，零 warning |
| 2 | 按钮状态 | 初始：start 启用、pause/stop 禁用；录制中：start 禁用、pause/stop 启用 |
| 3 | 时间显示 | 录制中每秒更新 `mm:ss`；暂停时冻结；停止后重置 `00:00` |
| 4 | 暂停/继续 | 暂停后时间冻结，数据不写入；继续后时间恢复，数据继续写入 |
| 5 | CSV 文件 | 停止后 `recordings/rec_*/imu_data.csv` 存在，16 列，有表头，数据行非空 |
| 6 | 文件夹 | 每次录制创建独立文件夹，命名含时间戳 |

## 7. 禁止事项

- 不得修改 `wit_protocol_parser.h/cpp`。
- 不得修改 `imu_data.h`。
- 不得修改 BLE 管理器。
- 不得改变现有的 UI 信号连接逻辑。
