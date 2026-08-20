#include "ui/main_window.h"

#include "render/hand_render_widget.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSerialPortInfo>
#include <QSplitter>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace handstudio {
namespace {

QString sourceStateText(SourceState state)
{
    switch (state) {
    case SourceState::Idle: return QStringLiteral("未连接");
    case SourceState::Starting: return QStringLiteral("启动中");
    case SourceState::Running: return QStringLiteral("运行中");
    case SourceState::Paused: return QStringLiteral("已暂停");
    case SourceState::Stopping: return QStringLiteral("停止中");
    case SourceState::Error: return QStringLiteral("错误");
    }
    return {};
}

QString fusionModeText(FusionMode mode)
{
    switch (mode) {
    case FusionMode::SixD: return QStringLiteral("6D");
    case FusionMode::NineD: return QStringLiteral("9D");
    default: return QStringLiteral("无效");
    }
}

QString boneSourceText(bool valid)
{
    return valid ? QStringLiteral("Estimated") : QStringLiteral("Held");
}

QTableWidgetItem *item(const QString &text)
{
    auto *value = new QTableWidgetItem(text);
    value->setFlags(value->flags() & ~Qt::ItemIsEditable);
    return value;
}

}

MainWindow::MainWindow(RuntimeController *controller, QWidget *parent)
    : QMainWindow(parent), controller_(controller)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("Hand Skeleton Studio 2.0"));
    resize(1400, 900);
    buildUi();

    connect(controller_, &RuntimeController::modelReady, renderWidget_, &HandRenderWidget::setModel);
    connect(controller_, &RuntimeController::skeletonFrameReady, renderWidget_, &HandRenderWidget::setSkeletonFrame);
    connect(controller_, &RuntimeController::sourceStateChanged, this, &MainWindow::updateSourceState);
    connect(controller_, &RuntimeController::recorderStateChanged, this, &MainWindow::updateRecorderState);
    connect(controller_, &RuntimeController::fusedPosesReady, this, &MainWindow::updateFusedPoses);
    connect(controller_, &RuntimeController::observationReady, this, &MainWindow::updateObservation);
    connect(controller_, &RuntimeController::diagnosticsReady, this, &MainWindow::appendDiagnostic);
    connect(controller_, &RuntimeController::statisticsChanged, this,
            [this](quint64 groups, qint64 latencyNs, quint64 dropped) {
                statisticsLabel_->setText(QStringLiteral("完整组 %1 | 延迟 %2 ms | UI丢帧 %3")
                                              .arg(groups).arg(double(latencyNs) / 1.0e6, 0, 'f', 2).arg(dropped));
            });
}

MainWindow::~MainWindow() = default;
HandRenderWidget *MainWindow::renderWidget() const noexcept { return renderWidget_; }
RuntimeController *MainWindow::controller() const noexcept { return controller_; }

void MainWindow::buildUi()
{
    auto *splitter = new QSplitter(this);
    renderWidget_ = new HandRenderWidget(splitter);
    renderWidget_->setObjectName(QStringLiteral("handRenderWidget"));

    auto *controls = new QWidget(splitter);
    controls->setMinimumWidth(430);
    auto *layout = new QVBoxLayout(controls);

    auto *sourceGroup = new QGroupBox(QStringLiteral("数据源"), controls);
    auto *sourceLayout = new QFormLayout(sourceGroup);
    sourceLayout_ = sourceLayout;
    sourceCombo_ = new QComboBox(sourceGroup);
    sourceCombo_->setObjectName(QStringLiteral("sourceCombo"));
    sourceCombo_->addItems({QStringLiteral("demo"), QStringLiteral("serial"), QStringLiteral("replay")});
    portCombo_ = new QComboBox(sourceGroup);
    portCombo_->setObjectName(QStringLiteral("portCombo"));
    refreshPortsButton_ = new QPushButton(QStringLiteral("刷新"), sourceGroup);
    refreshPortsButton_->setObjectName(QStringLiteral("refreshPortsButton"));
    auto *portRow = new QWidget(sourceGroup);
    auto *portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->addWidget(portCombo_, 1);
    portLayout->addWidget(refreshPortsButton_);
    sourceDetailEdit_ = new QLineEdit(sourceGroup);
    sourceDetailEdit_->setObjectName(QStringLiteral("sourceDetailEdit"));
    sourceDetailEdit_->setPlaceholderText(QStringLiteral("会话目录或 raw.bin 路径"));
    replaySpeedSpin_ = new QDoubleSpinBox(sourceGroup);
    replaySpeedSpin_->setRange(0.1, 20.0);
    replaySpeedSpin_->setValue(1.0);
    replaySpeedSpin_->setSuffix(QStringLiteral(" ×"));
    connectButton_ = new QPushButton(QStringLiteral("连接"), sourceGroup);
    connectButton_->setObjectName(QStringLiteral("connectButton"));
    pauseReplayButton_ = new QPushButton(QStringLiteral("暂停/继续回放"), sourceGroup);
    pauseReplayButton_->setCheckable(true);
    stepButton_ = new QPushButton(QStringLiteral("逐组"), sourceGroup);
    sourceStateLabel_ = new QLabel(QStringLiteral("未连接"), sourceGroup);
    sourceLayout->addRow(QStringLiteral("类型"), sourceCombo_);
    sourceLayout->addRow(QStringLiteral("串口设备"), portRow);
    portRowIndex_ = sourceLayout->rowCount() - 1;
    sourceLayout->addRow(QStringLiteral("会话目录"), sourceDetailEdit_);
    detailRowIndex_ = sourceLayout->rowCount() - 1;
    sourceLayout->addRow(QStringLiteral("回放速度"), replaySpeedSpin_);
    speedRowIndex_ = sourceLayout->rowCount() - 1;
    sourceLayout->addRow(connectButton_);
    sourceLayout->addRow(pauseReplayButton_, stepButton_);
    sourceLayout->addRow(QStringLiteral("状态"), sourceStateLabel_);
    connect(sourceCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::updateSourceWidgets);
    connect(refreshPortsButton_, &QPushButton::clicked, this, &MainWindow::refreshSerialPorts);
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::applySource);
    connect(pauseReplayButton_, &QPushButton::toggled, controller_, &RuntimeController::setReplayPaused);
    connect(stepButton_, &QPushButton::clicked, controller_, &RuntimeController::stepReplayGroup);
    layout->addWidget(sourceGroup);
    portRefreshTimer_ = new QTimer(this);
    portRefreshTimer_->setInterval(2000);
    connect(portRefreshTimer_, &QTimer::timeout, this, &MainWindow::refreshSerialPorts);
    portRefreshTimer_->start();
    refreshSerialPorts();
    updateSourceWidgets();

    auto *calibrationGroup = new QGroupBox(QStringLiteral("校准与视图"), controls);
    auto *calibrationLayout = new QVBoxLayout(calibrationGroup);
    auto *rawCalibration = new QPushButton(QStringLiteral("原始参数"), calibrationGroup);
    rawCalibration->setObjectName(QStringLiteral("rawCalibrationButton"));
    auto *biasCalibration = new QPushButton(QStringLiteral("启动零偏"), calibrationGroup);
    biasCalibration->setObjectName(QStringLiteral("biasCalibrationButton"));
    auto *neutralCalibration = new QPushButton(QStringLiteral("调零（中立姿态）"), calibrationGroup);
    neutralCalibration->setObjectName(QStringLiteral("zeroHandPoseButton"));
    auto *cameraReset = new QPushButton(QStringLiteral("重置相机"), calibrationGroup);
    cameraReset->setObjectName(QStringLiteral("cameraResetButton"));
    calibrationLayout->addWidget(rawCalibration);
    calibrationLayout->addWidget(biasCalibration);
    calibrationLayout->addWidget(neutralCalibration);
    calibrationLayout->addWidget(cameraReset);
    connect(biasCalibration, &QPushButton::clicked, controller_, &RuntimeController::beginRestBiasCalibration);
    connect(neutralCalibration, &QPushButton::clicked, controller_, &RuntimeController::zeroHandPose);
    connect(cameraReset, &QPushButton::clicked, renderWidget_, &HandRenderWidget::resetCamera);
    layout->addWidget(calibrationGroup);

    sensorTable_ = new QTableWidget(6, 7, controls);
    sensorTable_->setObjectName(QStringLiteral("sensorTable"));
    sensorTable_->setHorizontalHeaderLabels({QStringLiteral("节点"), QStringLiteral("有效"), QStringLiteral("模式"),
                                             QStringLiteral("静止"), QStringLiteral("磁场"), QStringLiteral("校准"),
                                             QStringLiteral("置信度")});
    sensorTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    for (int row = 0; row < 6; ++row) sensorTable_->setItem(row, 0, item(QString::number(row)));
    layout->addWidget(sensorTable_);

    fingerTable_ = new QTableWidget(5, 5, controls);
    fingerTable_->setObjectName(QStringLiteral("fingerTable"));
    fingerTable_->setHorizontalHeaderLabels({QStringLiteral("手指"), QStringLiteral("屈伸"), QStringLiteral("张合"),
                                             QStringLiteral("扭转"), QStringLiteral("状态")});
    fingerTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    const QStringList fingerNames{QStringLiteral("拇指"), QStringLiteral("食指"), QStringLiteral("中指"),
                                  QStringLiteral("无名指"), QStringLiteral("小指")};
    for (int row = 0; row < 5; ++row) fingerTable_->setItem(row, 0, item(fingerNames[row]));
    layout->addWidget(fingerTable_);

    auto *recordGroup = new QGroupBox(QStringLiteral("录制"), controls);
    auto *recordLayout = new QVBoxLayout(recordGroup);
    recordButton_ = new QPushButton(QStringLiteral("开始录制"), recordGroup);
    recordButton_->setObjectName(QStringLiteral("recordButton"));
    recordPauseButton_ = new QPushButton(QStringLiteral("暂停"), recordGroup);
    recordPauseButton_->setObjectName(QStringLiteral("recordPauseButton"));
    recordStopButton_ = new QPushButton(QStringLiteral("停止"), recordGroup);
    recordStopButton_->setObjectName(QStringLiteral("recordStopButton"));
    recordingStateLabel_ = new QLabel(QStringLiteral("空闲"), recordGroup);
    recordPauseButton_->setEnabled(false);
    recordStopButton_->setEnabled(false);
    recordLayout->addWidget(recordButton_);
    recordLayout->addWidget(recordPauseButton_);
    recordLayout->addWidget(recordStopButton_);
    recordLayout->addWidget(recordingStateLabel_);
    connect(recordButton_, &QPushButton::clicked, this, &MainWindow::chooseRecordingDirectory);
    connect(recordPauseButton_, &QPushButton::clicked, this, &MainWindow::toggleRecordingPause);
    connect(recordStopButton_, &QPushButton::clicked, controller_, &RuntimeController::stopRecording);
    layout->addWidget(recordGroup);

    statisticsLabel_ = new QLabel(QStringLiteral("完整组 0 | 延迟 0 ms | UI丢帧 0"), controls);
    diagnosticsEdit_ = new QTextEdit(controls);
    diagnosticsEdit_->setObjectName(QStringLiteral("diagnosticsEdit"));
    diagnosticsEdit_->setReadOnly(true);
    layout->addWidget(statisticsLabel_);
    layout->addWidget(diagnosticsEdit_, 1);
    splitter->addWidget(renderWidget_);
    splitter->addWidget(controls);
    splitter->setStretchFactor(0, 1);
    setCentralWidget(splitter);
}

void MainWindow::applySource()
{
    const QString source = sourceCombo_->currentText();
    QString detail;
    if (source == QStringLiteral("serial")) {
        // The combo item shows "COM12 - USB Serial Port" but stores the real
        // port name ("COM12") as userData; opening the label would fail.
        detail = portCombo_->currentData().toString();
        if (detail.isEmpty()) {
            detail = sourceDetailEdit_->text();
        }
    } else {
        detail = sourceDetailEdit_->text();
    }
    controller_->selectSource(source, detail);
    controller_->start();
}

void MainWindow::refreshSerialPorts()
{
    const QString previous = portCombo_->currentText();
    portCombo_->blockSignals(true);
    portCombo_->clear();
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : ports) {
        const QString description = info.description().trimmed();
        const QString label = description.isEmpty() ? info.portName()
                                                    : QStringLiteral("%1 - %2").arg(info.portName(), description);
        portCombo_->addItem(label, info.portName());
    }
    if (!previous.isEmpty()) {
        const int index = portCombo_->findText(previous);
        if (index >= 0) portCombo_->setCurrentIndex(index);
    }
    portCombo_->blockSignals(false);
    if (sourceCombo_->currentText() == QStringLiteral("serial") && portCombo_->count() == 0) {
        appendDiagnostic({DiagnosticSeverity::Warning, QStringLiteral("serial.no-ports"),
                          QStringLiteral("未检测到串口设备"), {}});
    }
}

void MainWindow::updateSourceWidgets()
{
    const QString source = sourceCombo_->currentText();
    const bool serial = source == QStringLiteral("serial");
    const bool replay = source == QStringLiteral("replay");
    if (sourceLayout_) {
        sourceLayout_->setRowVisible(portRowIndex_, serial);
        sourceLayout_->setRowVisible(detailRowIndex_, replay);
        sourceLayout_->setRowVisible(speedRowIndex_, replay);
    }
    connectButton_->setText(serial ? QStringLiteral("连接") : QStringLiteral("启动"));
}

void MainWindow::updateSourceState(SourceState state, const Diagnostic &diagnostic)
{
    sourceStateLabel_->setText(sourceStateText(state));
    if (!diagnostic.message.isEmpty()) appendDiagnostic(diagnostic);
}

void MainWindow::updateRecorderState(RecorderState state, const Diagnostic &diagnostic)
{
    const bool active = state == RecorderState::Recording || state == RecorderState::Paused;
    recordButton_->setEnabled(!active);
    recordPauseButton_->setEnabled(active);
    recordStopButton_->setEnabled(active);
    recordPauseButton_->setText(state == RecorderState::Paused ? QStringLiteral("继续") : QStringLiteral("暂停"));
    recordingStateLabel_->setText(state == RecorderState::Recording ? QStringLiteral("录制中")
                                  : state == RecorderState::Paused ? QStringLiteral("已暂停") : QStringLiteral("空闲"));
    if (!diagnostic.message.isEmpty()) appendDiagnostic(diagnostic);
}

void MainWindow::updateFusedPoses(const std::array<FusedImuPose, 6> &poses)
{
    for (int row = 0; row < 6; ++row) {
        const auto &pose = poses[std::size_t(row)];
        sensorTable_->setItem(row, 1, item(pose.valid ? QStringLiteral("是") : QStringLiteral("否")));
        sensorTable_->setItem(row, 2, item(fusionModeText(pose.mode)));
        sensorTable_->setItem(row, 3, item(pose.restDetected ? QStringLiteral("是") : QStringLiteral("否")));
        sensorTable_->setItem(row, 4, item(QString::number(int(pose.magneticHealth))));
        sensorTable_->setItem(row, 5, item(QString::number(int(pose.calibrationState))));
        sensorTable_->setItem(row, 6, item(QString::number(pose.confidence, 'f', 2)));
    }
}

void MainWindow::updateObservation(const HandObservationFrame &observation)
{
    for (int row = 0; row < 5; ++row) {
        const auto &finger = observation.fingers[std::size_t(row)];
        fingerTable_->setItem(row, 1, item(QString::number(finger.flexionDegrees, 'f', 1)));
        fingerTable_->setItem(row, 2, item(QString::number(finger.abductionDegrees, 'f', 1)));
        fingerTable_->setItem(row, 3, item(QString::number(finger.twistDegrees, 'f', 1)));
        fingerTable_->setItem(row, 4, item(boneSourceText(finger.valid)));
    }
}

void MainWindow::appendDiagnostic(const Diagnostic &diagnostic)
{
    diagnosticsEdit_->append(QStringLiteral("[%1] %2: %3 %4")
                                 .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
                                      diagnostic.code, diagnostic.message, diagnostic.detail));
}

void MainWindow::chooseRecordingDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("选择录制父目录"));
    if (directory.isEmpty()) return;
    recordingDirectory_ = QDir(directory).filePath(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    controller_->startRecording(recordingDirectory_);
}

void MainWindow::toggleRecordingPause()
{
    controller_->recorderState() == RecorderState::Paused ? controller_->resumeRecording()
                                                          : controller_->pauseRecording();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    controller_->stop();
    QMainWindow::closeEvent(event);
}

}
