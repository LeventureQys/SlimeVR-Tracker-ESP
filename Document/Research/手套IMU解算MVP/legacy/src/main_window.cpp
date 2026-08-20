#include "main_window.h"

#include "sensor_panel.h"
#include "settings_dialog.h"
#include "settings_store.h"
#include "slimevr_mount_dialog.h"
#include "slimevr_settings_store.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {
constexpr qint64 TimeoutNs = 500000000;
const std::array<SensorId, SixImuProtocol::SensorCount> SensorOrder{
    SensorId::Wrist, SensorId::Thumb, SensorId::Index,
    SensorId::Middle, SensorId::Ring, SensorId::Pinky};

QString sourceStateText(SourceState state)
{
    switch (state) {
    case SourceState::Closed: return QStringLiteral("串口已关闭");
    case SourceState::Opening: return QStringLiteral("正在打开串口");
    case SourceState::Open: return QStringLiteral("串口已打开");
    case SourceState::Closing: return QStringLiteral("正在关闭串口");
    case SourceState::Error: return QStringLiteral("串口错误");
    }
    return QStringLiteral("串口状态未知");
}

QString slimeStateText(SlimeVrConnectionState state)
{
    switch (state) {
    case SlimeVrConnectionState::Disabled: return QStringLiteral("已禁用");
    case SlimeVrConnectionState::Discovering: return QStringLiteral("自动发现中");
    case SlimeVrConnectionState::Handshaking: return QStringLiteral("握手中");
    case SlimeVrConnectionState::Connected: return QStringLiteral("已连接");
    case SlimeVrConnectionState::Backoff: return QStringLiteral("退避重连中");
    case SlimeVrConnectionState::Error: return QStringLiteral("配置错误");
    }
    return QStringLiteral("未知");
}

void setStatistic(QTableWidget *table, int row, const QString &name, quint64 value)
{
    table->setItem(row, 0, new QTableWidgetItem(name));
    table->setItem(row, 1, new QTableWidgetItem(QString::number(value)));
}
}

MainWindow::MainWindow(QSettings *settingsOverride, QWidget *parent)
    : QMainWindow(parent)
    , parser_(this)
    , grouper_(SixImuProtocol::DefaultPendingGroupLimit, this)
    , solver_(this)
    , serialSource_(this)
    , demoSource_(this)
    , slimeClient_(this)
    , slimeSender_(&slimeClient_, this)
{
    if (settingsOverride) {
        settings_ = settingsOverride;
    } else {
        ownedSettings_ = std::make_unique<QSettings>();
        settings_ = ownedSettings_.get();
    }

    buildInterface();
    connectSignals();
    uiClock_.start();

    SettingsStore store(*settings_);
    currentSettings_ = store.load();
    solver_.applySettings(currentSettings_);

    SlimeVrSettingsStore slimeStore(*settings_);
    slimeSettings_ = slimeStore.load();
    slimeClient_.setIdentity(buildSlimeIdentity());
    applySlimeSettings(slimeSettings_);

    refreshPorts();

    refreshTimer_->start(33);
    statusLabel_->setText(QStringLiteral("就绪"));
    updateControlStates();
}

MainWindow::~MainWindow()
{
    refreshTimer_->stop();
    slimeSender_.stop();
    slimeClient_.stop();
    demoSource_.stop();
    serialSource_.closePort();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    slimeSender_.stop();
    slimeClient_.stop();
    QMainWindow::closeEvent(event);
}

void MainWindow::setDemoModeEnabled(bool enabled)
{
    if (demoModeCheckBox_->isChecked() == enabled) {
        handleDemoMode(enabled);
        return;
    }
    demoModeCheckBox_->setChecked(enabled);
}

void MainWindow::buildInterface()
{
    setWindowTitle(QStringLiteral("手套六 IMU 实时解算"));
    setMinimumSize(1200, 800);

    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    auto *controlsLayout = new QGridLayout;

    portComboBox_ = new QComboBox(centralWidget);
    portComboBox_->setObjectName(QStringLiteral("portComboBox"));
    refreshPortsButton_ = new QPushButton(QStringLiteral("刷新串口"), centralWidget);
    refreshPortsButton_->setObjectName(QStringLiteral("refreshPortsButton"));
    openPortButton_ = new QPushButton(QStringLiteral("打开串口"), centralWidget);
    openPortButton_->setObjectName(QStringLiteral("openPortButton"));
    closePortButton_ = new QPushButton(QStringLiteral("关闭串口"), centralWidget);
    closePortButton_->setObjectName(QStringLiteral("closePortButton"));
    demoModeCheckBox_ = new QCheckBox(QStringLiteral("演示模式"), centralWidget);
    demoModeCheckBox_->setObjectName(QStringLiteral("demoModeCheckBox"));
    settingsButton_ = new QPushButton(QStringLiteral("姿态参数"), centralWidget);
    settingsButton_->setObjectName(QStringLiteral("settingsButton"));
    calibrateZeroButton_ = new QPushButton(QStringLiteral("六路零位标定"), centralWidget);
    calibrateZeroButton_->setObjectName(QStringLiteral("calibrateZeroButton"));
    clearCalibrationButton_ = new QPushButton(QStringLiteral("清除零位"), centralWidget);
    clearCalibrationButton_->setObjectName(QStringLiteral("clearCalibrationButton"));
    resetStatisticsButton_ = new QPushButton(QStringLiteral("清空统计"), centralWidget);
    resetStatisticsButton_->setObjectName(QStringLiteral("resetStatisticsButton"));

    controlsLayout->addWidget(new QLabel(QStringLiteral("串口"), centralWidget), 0, 0);
    controlsLayout->addWidget(portComboBox_, 0, 1);
    controlsLayout->addWidget(refreshPortsButton_, 0, 2);
    controlsLayout->addWidget(openPortButton_, 0, 3);
    controlsLayout->addWidget(closePortButton_, 0, 4);
    controlsLayout->addWidget(demoModeCheckBox_, 0, 5);
    controlsLayout->addWidget(settingsButton_, 0, 6);
    controlsLayout->addWidget(calibrateZeroButton_, 1, 3);
    controlsLayout->addWidget(clearCalibrationButton_, 1, 4);
    controlsLayout->addWidget(resetStatisticsButton_, 1, 6);
    controlsLayout->setColumnStretch(1, 1);
    mainLayout->addLayout(controlsLayout);

    auto *slimeGroupBox = new QGroupBox(QStringLiteral("SlimeVR 输出"), centralWidget);
    slimeGroupBox->setObjectName(QStringLiteral("slimeGroupBox"));
    auto *slimeLayout = new QGridLayout(slimeGroupBox);
    slimeEnableCheckBox_ = new QCheckBox(QStringLiteral("启用"), slimeGroupBox);
    slimeEnableCheckBox_->setObjectName(QStringLiteral("slimeEnableCheckBox"));
    slimeModeComboBox_ = new QComboBox(slimeGroupBox);
    slimeModeComboBox_->setObjectName(QStringLiteral("slimeModeComboBox"));
    slimeModeComboBox_->addItem(QStringLiteral("自动发现"), int(SlimeVrDiscoveryMode::Broadcast));
    slimeModeComboBox_->addItem(QStringLiteral("固定地址"), int(SlimeVrDiscoveryMode::FixedHost));
    slimeHostLineEdit_ = new QLineEdit(slimeGroupBox);
    slimeHostLineEdit_->setObjectName(QStringLiteral("slimeHostLineEdit"));
    slimeHostLineEdit_->setPlaceholderText(QStringLiteral("127.0.0.1"));
    slimePortSpinBox_ = new QSpinBox(slimeGroupBox);
    slimePortSpinBox_->setObjectName(QStringLiteral("slimePortSpinBox"));
    slimePortSpinBox_->setRange(1, 65535);
    slimePortSpinBox_->setValue(6969);
    slimeSideComboBox_ = new QComboBox(slimeGroupBox);
    slimeSideComboBox_->setObjectName(QStringLiteral("slimeSideComboBox"));
    slimeSideComboBox_->addItem(QStringLiteral("左手"), int(GloveSide::Left));
    slimeSideComboBox_->addItem(QStringLiteral("右手"), int(GloveSide::Right));
    slimeRateSpinBox_ = new QSpinBox(slimeGroupBox);
    slimeRateSpinBox_->setObjectName(QStringLiteral("slimeRateSpinBox"));
    slimeRateSpinBox_->setRange(50, 100);
    slimeRateSpinBox_->setValue(75);
    slimeRateSpinBox_->setSuffix(QStringLiteral(" Hz"));
    slimeApplyButton_ = new QPushButton(QStringLiteral("应用设置"), slimeGroupBox);
    slimeApplyButton_->setObjectName(QStringLiteral("slimeApplyButton"));
    slimeMountButton_ = new QPushButton(QStringLiteral("安装旋转"), slimeGroupBox);
    slimeMountButton_->setObjectName(QStringLiteral("slimeMountButton"));

    slimeLayout->addWidget(slimeEnableCheckBox_, 0, 0);
    slimeLayout->addWidget(new QLabel(QStringLiteral("模式"), slimeGroupBox), 0, 1);
    slimeLayout->addWidget(slimeModeComboBox_, 0, 2);
    slimeLayout->addWidget(new QLabel(QStringLiteral("地址"), slimeGroupBox), 0, 3);
    slimeLayout->addWidget(slimeHostLineEdit_, 0, 4);
    slimeLayout->addWidget(new QLabel(QStringLiteral("端口"), slimeGroupBox), 0, 5);
    slimeLayout->addWidget(slimePortSpinBox_, 0, 6);
    slimeLayout->addWidget(new QLabel(QStringLiteral("手侧"), slimeGroupBox), 0, 7);
    slimeLayout->addWidget(slimeSideComboBox_, 0, 8);
    slimeLayout->addWidget(new QLabel(QStringLiteral("发送率"), slimeGroupBox), 0, 9);
    slimeLayout->addWidget(slimeRateSpinBox_, 0, 10);
    slimeLayout->addWidget(slimeMountButton_, 0, 11);
    slimeLayout->addWidget(slimeApplyButton_, 0, 12);
    slimeStatusLabel_ = new QLabel(slimeGroupBox);
    slimeStatusLabel_->setObjectName(QStringLiteral("slimeStatusLabel"));
    slimeStatsLabel_ = new QLabel(slimeGroupBox);
    slimeStatsLabel_->setObjectName(QStringLiteral("slimeStatsLabel"));
    slimeLayout->addWidget(slimeStatusLabel_, 1, 0, 1, 8);
    slimeLayout->addWidget(slimeStatsLabel_, 1, 8, 1, 5);
    slimeLayout->setColumnStretch(4, 1);
    mainLayout->addWidget(slimeGroupBox);

    statusLabel_ = new QLabel(centralWidget);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    statusLabel_->setWordWrap(true);
    latestSequenceLabel_ = new QLabel(QStringLiteral("最新组 SEQ: --"), centralWidget);
    latestSequenceLabel_->setObjectName(QStringLiteral("latestSequenceLabel"));
    auto *statusLayout = new QGridLayout;
    statusLayout->addWidget(statusLabel_, 0, 0);
    statusLayout->addWidget(latestSequenceLabel_, 0, 1, Qt::AlignRight);
    mainLayout->addLayout(statusLayout);

    auto *sensorContainer = new QWidget(centralWidget);
    auto *sensorLayout = new QGridLayout(sensorContainer);
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        sensorPanels_[index] = new SensorPanel(SensorOrder[index], sensorContainer);
        sensorLayout->addWidget(sensorPanels_[index], index / 3, index % 3);
    }

    auto *scrollArea = new QScrollArea(centralWidget);
    scrollArea->setObjectName(QStringLiteral("sensorScrollArea"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(sensorContainer);
    mainLayout->addWidget(scrollArea, 1);

    statisticsTableWidget_ = new QTableWidget(13, 2, centralWidget);
    statisticsTableWidget_->setObjectName(QStringLiteral("statisticsTableWidget"));
    statisticsTableWidget_->setHorizontalHeaderLabels({QStringLiteral("统计项"), QStringLiteral("值")});
    statisticsTableWidget_->verticalHeader()->hide();
    statisticsTableWidget_->horizontalHeader()->setStretchLastSection(true);
    statisticsTableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    statisticsTableWidget_->setMaximumHeight(250);
    mainLayout->addWidget(statisticsTableWidget_);

    setCentralWidget(centralWidget);
}

void MainWindow::connectSignals()
{
    connect(&serialSource_, &SerialDataSource::bytesReady,
            &parser_, &FrameStreamParser::appendBytes);
    connect(&demoSource_, &DemoDataSource::bytesReady,
            &parser_, &FrameStreamParser::appendBytes);
    connect(&parser_, &FrameStreamParser::frameParsed,
            &grouper_, &SequenceGrouper::addFrame);
    connect(&grouper_, &SequenceGrouper::completeGroupReady,
            &solver_, &SixImuSolver::processCompleteGroup);
    connect(&solver_, &SixImuSolver::snapshotReady, this,
            [this](const SixImuSnapshot &snapshot) {
                pendingSnapshot_ = snapshot;
                lastSnapshotUiNs_ = uiClock_.nsecsElapsed();
                slimeSender_.submitSnapshot(snapshot);
            });

    connect(refreshPortsButton_, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    connect(openPortButton_, &QPushButton::clicked, this, &MainWindow::openSelectedPort);
    connect(closePortButton_, &QPushButton::clicked, this, [this] {
        closeSerialInput();
        parser_.reset();
        grouper_.reset();
        solver_.reset();
        pendingSnapshot_.reset();
        for (SensorPanel *panel : sensorPanels_) {
            panel->resetDisplay();
        }
        latestSequenceLabel_->setText(QStringLiteral("最新组 SEQ: --"));
        statusLabel_->setText(QStringLiteral("串口已关闭，协议统计已保留"));
        updateControlStates();
    });
    connect(demoModeCheckBox_, &QCheckBox::toggled, this, &MainWindow::handleDemoMode);
    connect(settingsButton_, &QPushButton::clicked, this, &MainWindow::openSettingsDialog);
    connect(calibrateZeroButton_, &QPushButton::clicked, this, &MainWindow::calibrateZero);
    connect(clearCalibrationButton_, &QPushButton::clicked, this, [this] {
        solver_.clearCalibration();
        statusLabel_->setText(QStringLiteral("零位已清除"));
        updateControlStates();
    });
    connect(resetStatisticsButton_, &QPushButton::clicked, this, [this] {
        parser_.reset();
        grouper_.reset();
        statusLabel_->setText(QStringLiteral("协议统计已清空"));
    });

    connect(slimeApplyButton_, &QPushButton::clicked, this, &MainWindow::applySlimeSettingsFromUi);
    connect(slimeMountButton_, &QPushButton::clicked, this, &MainWindow::openSlimeMountDialog);
    connect(&slimeClient_, &SlimeVrUdpClient::stateChanged, this, [this](SlimeVrConnectionState) {
        updateSlimeLabels();
    });
    connect(&slimeClient_, &SlimeVrUdpClient::statisticsChanged, this, [this](const SlimeVrNetworkStatistics &) {
        updateSlimeLabels();
    });
    connect(&slimeSender_, &SlimeVrPoseSender::statisticsChanged, this, [this] {
        updateSlimeLabels();
    });
    connect(&slimeClient_, &SlimeVrUdpClient::protocolError, this,
            [this](const QString &message) {
                statusLabel_->setText(QStringLiteral("SlimeVR：%1").arg(message));
            });

    connect(&serialSource_, &SerialDataSource::stateChanged, this,
            [this](SourceState state, const QString &message) {
                statusLabel_->setText(message.isEmpty() ? sourceStateText(state) : message);
                updateControlStates();
            });
    connect(&serialSource_, &SerialDataSource::errorOccurred, this,
            [this](const QString &message) {
                statusLabel_->setText(QStringLiteral("串口错误：%1").arg(message));
                updateControlStates();
            });

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(33);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshUi);
}

void MainWindow::refreshPorts()
{
    const QString selectedPort = portComboBox_->currentData().toString();
    portComboBox_->clear();
    const QList<SerialPortDescriptor> ports = serialSource_.availablePorts();
    for (const SerialPortDescriptor &port : ports) {
        QString display = port.portName;
        if (!port.description.isEmpty()) {
            display += QStringLiteral(" · ") + port.description;
        }
        portComboBox_->addItem(display, port.portName);
    }
    const int selectedIndex = portComboBox_->findData(selectedPort);
    if (selectedIndex >= 0) {
        portComboBox_->setCurrentIndex(selectedIndex);
    }
    if (ports.isEmpty()) {
        statusLabel_->setText(QStringLiteral("未发现可用串口"));
    }
    updateControlStates();
}

void MainWindow::openSelectedPort()
{
    const QString portName = portComboBox_->currentData().toString();
    if (portName.isEmpty()) {
        statusLabel_->setText(QStringLiteral("请选择有效串口"));
        return;
    }

    if (demoSource_.isActive() || demoModeCheckBox_->isChecked()) {
        demoSource_.stop();
        QSignalBlocker blocker(demoModeCheckBox_);
        demoModeCheckBox_->setChecked(false);
    }
    closeSerialInput();
    resetPipeline(QStringLiteral("打开新串口"));
    serialSource_.openPort(portName);
    updateControlStates();
}

void MainWindow::handleDemoMode(bool enabled)
{
    if (enabled) {
        closeSerialInput();
        resetPipeline(QStringLiteral("开启演示模式"));
        demoSource_.start();
        statusLabel_->setText(QStringLiteral("演示模式 · 非真实设备数据"));
    } else {
        demoSource_.stop();
        parser_.reset();
        grouper_.reset();
        solver_.reset();
        pendingSnapshot_.reset();
        for (SensorPanel *panel : sensorPanels_) {
            panel->resetDisplay();
        }
        latestSequenceLabel_->setText(QStringLiteral("最新组 SEQ: --"));
        statusLabel_->setText(QStringLiteral("演示模式已关闭 · 非真实设备数据已停止"));
    }
    updateControlStates();
}

void MainWindow::openSettingsDialog()
{
    SettingsDialog dialog(currentSettings_, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const SolverSettings candidate = dialog.editedSettings();
    QString errorMessage;
    SettingsStore store(*settings_);
    if (!store.save(candidate, &errorMessage)) {
        QMessageBox::critical(this, QStringLiteral("保存参数失败"), errorMessage);
        return;
    }

    applySettings(candidate);
}

void MainWindow::applySettings(const SolverSettings &settings)
{
    currentSettings_ = settings;
    solver_.applySettings(settings);
    pendingSnapshot_.reset();
    for (SensorPanel *panel : sensorPanels_) {
        panel->resetDisplay();
    }
    latestSequenceLabel_->setText(QStringLiteral("最新组 SEQ: --"));
    statusLabel_->setText(QStringLiteral("参数已应用，请重新静止标定"));
    updateControlStates();
}

void MainWindow::calibrateZero()
{
    QString errorMessage;
    if (!solver_.calibrateZero(&errorMessage)) {
        statusLabel_->setText(QStringLiteral("零位标定失败：%1").arg(errorMessage));
        updateControlStates();
        return;
    }
    if (const auto snapshot = solver_.latestSnapshot()) {
        pendingSnapshot_ = snapshot;
        lastSnapshotUiNs_ = uiClock_.nsecsElapsed();
    }
    statusLabel_->setText(QStringLiteral("六路零位标定成功"));
    updateControlStates();
}

void MainWindow::refreshUi()
{
    const ParserStatistics parserStatistics = parser_.statistics();
    const GroupStatistics groupStatistics = grouper_.statistics();
    setStatistic(statisticsTableWidget_, 0, QStringLiteral("输入字节"), parserStatistics.inputBytes);
    setStatistic(statisticsTableWidget_, 1, QStringLiteral("候选帧头"), parserStatistics.headerCandidates);
    setStatistic(statisticsTableWidget_, 2, QStringLiteral("有效帧"), parserStatistics.validFrames);
    setStatistic(statisticsTableWidget_, 3, QStringLiteral("长度错误"), parserStatistics.invalidLength);
    setStatistic(statisticsTableWidget_, 4, QStringLiteral("CRC 错误"), parserStatistics.invalidCrc);
    setStatistic(statisticsTableWidget_, 5, QStringLiteral("丢弃字节"), parserStatistics.discardedBytes);
    setStatistic(statisticsTableWidget_, 6, QStringLiteral("未知地址帧"), parserStatistics.unknownAddressFrames);
    setStatistic(statisticsTableWidget_, 7, QStringLiteral("完整组"), groupStatistics.completeGroups);
    setStatistic(statisticsTableWidget_, 8, QStringLiteral("不完整组"), groupStatistics.partialGroups);
    setStatistic(statisticsTableWidget_, 9, QStringLiteral("重复帧"), groupStatistics.duplicateFrames);
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        setStatistic(statisticsTableWidget_, 10 + (index % 3),
                     QStringLiteral("%1帧").arg(sensorDisplayName(SensorOrder[index])),
                     groupStatistics.sensorFrameCounts[index]);
    }

    if (!pendingSnapshot_) {
        updateControlStates();
        return;
    }

    const SixImuSnapshot snapshot = *pendingSnapshot_;
    const bool timedOut = lastSnapshotUiNs_ > 0
        && uiClock_.nsecsElapsed() - lastSnapshotUiNs_ > TimeoutNs;
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        SensorPose pose = snapshot.poses[index];
        if (timedOut) {
            pose.status = pose.status.isEmpty()
                ? QStringLiteral("超时")
                : pose.status + QStringLiteral(" · 超时");
        }
        sensorPanels_[index]->display(snapshot.rawFrames[index], pose);
    }
    latestSequenceLabel_->setText(QStringLiteral("最新组 SEQ: %1").arg(snapshot.sequence));
    updateControlStates();
}

void MainWindow::resetPipeline(const QString &reason)
{
    parser_.reset();
    grouper_.reset();
    solver_.reset();
    pendingSnapshot_.reset();
    lastSnapshotUiNs_ = 0;
    for (SensorPanel *panel : sensorPanels_) {
        panel->resetDisplay();
    }
    latestSequenceLabel_->setText(QStringLiteral("最新组 SEQ: --"));
    statusLabel_->setText(QStringLiteral("管线已重置：%1").arg(reason));
}

void MainWindow::applySlimeSettingsFromUi()
{
    SlimeVrSettings candidate;
    candidate.enabled = slimeEnableCheckBox_->isChecked();
    candidate.discoveryMode = SlimeVrDiscoveryMode(slimeModeComboBox_->currentData().toInt());
    candidate.host = slimeHostLineEdit_->text().trimmed();
    candidate.port = quint16(slimePortSpinBox_->value());
    candidate.sendRateHz = slimeRateSpinBox_->value();
    candidate.gloveSide = GloveSide(slimeSideComboBox_->currentData().toInt());
    candidate.deviceId = slimeSettings_.deviceId;
    candidate.mountings = slimeSettings_.mountings;

    QString error;
    if (!validateSlimeVrSettings(candidate, &error)) {
        QMessageBox::warning(this, QStringLiteral("SlimeVR 设置无效"), error);
        return;
    }

    SlimeVrSettingsStore store(*settings_);
    if (!store.save(candidate, &error)) {
        QMessageBox::critical(this, QStringLiteral("保存 SlimeVR 设置失败"), error);
        return;
    }

    slimeSettings_ = candidate;
    slimeClient_.setIdentity(buildSlimeIdentity());
    applySlimeSettings(slimeSettings_);
    statusLabel_->setText(QStringLiteral("SlimeVR 设置已应用"));
    updateControlStates();
}

void MainWindow::openSlimeMountDialog()
{
    SlimeVrMountDialog dialog(slimeSettings_.mountings, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const std::array<QQuaternion, 6> mountings = dialog.mountings();
    SlimeVrSettings candidate = slimeSettings_;
    candidate.mountings = mountings;

    QString error;
    SlimeVrSettingsStore store(*settings_);
    if (!store.save(candidate, &error)) {
        QMessageBox::critical(this, QStringLiteral("保存安装旋转失败"), error);
        return;
    }
    slimeSettings_ = candidate;
    slimeSender_.setMountings(slimeSettings_.mountings);
    statusLabel_->setText(QStringLiteral("安装旋转已保存并应用"));
}

void MainWindow::applySlimeSettings(const SlimeVrSettings &settings)
{
    slimeSender_.setGloveSide(settings.gloveSide);
    slimeSender_.setSendRateHz(settings.sendRateHz);
    slimeSender_.setMountings(settings.mountings);
    if (settings.enabled) {
        slimeSender_.start();
        slimeClient_.applySettings(settings);
    } else {
        slimeClient_.applySettings(settings);
        slimeSender_.stop();
    }
    // Mirror into the widgets when called from startup.
    QSignalBlocker blocker1(slimeEnableCheckBox_);
    QSignalBlocker blocker2(slimeModeComboBox_);
    QSignalBlocker blocker3(slimeHostLineEdit_);
    QSignalBlocker blocker4(slimePortSpinBox_);
    QSignalBlocker blocker5(slimeSideComboBox_);
    QSignalBlocker blocker6(slimeRateSpinBox_);
    slimeEnableCheckBox_->setChecked(settings.enabled);
    slimeModeComboBox_->setCurrentIndex(slimeModeComboBox_->findData(int(settings.discoveryMode)));
    slimeHostLineEdit_->setText(settings.host);
    slimePortSpinBox_->setValue(settings.port);
    slimeSideComboBox_->setCurrentIndex(slimeSideComboBox_->findData(int(settings.gloveSide)));
    slimeRateSpinBox_->setValue(settings.sendRateHz);
    updateSlimeLabels();
}

SlimeVrProtocol::HandshakeIdentity MainWindow::buildSlimeIdentity() const
{
    SlimeVrProtocol::HandshakeIdentity identity;
    identity.trackerType = slimeSettings_.gloveSide == GloveSide::Right
        ? SlimeVrProtocol::IdentityDefaults::TrackerTypeGloveRight
        : SlimeVrProtocol::IdentityDefaults::TrackerTypeGloveLeft;
    if (slimeSettings_.deviceId.size() == 6) {
        for (int index = 0; index < 6; ++index) {
            identity.deviceId[size_t(index)] = quint8(slimeSettings_.deviceId.at(index));
        }
    }
    return identity;
}

void MainWindow::updateSlimeLabels()
{
    slimeStatusLabel_->setText(
        QStringLiteral("SlimeVR 状态：%1").arg(slimeStateText(slimeClient_.state())));
    const SlimeVrNetworkStatistics network = slimeClient_.statistics();
    const SlimeVrPoseSender::Statistics sending = slimeSender_.statistics();
    quint64 rotationSent = 0;
    quint64 rotationSkipped = 0;
    for (int index = 0; index < 6; ++index) {
        rotationSent += sending.rotationSent[size_t(index)];
        rotationSkipped += sending.rotationSkipped[size_t(index)];
    }
    slimeStatsLabel_->setText(
        QStringLiteral("发送 %1 · 注册 %2 · 姿态跳过 %3 · 错误 %4 · 重连 %5")
            .arg(network.datagramsSent)
            .arg(sending.sensorInfoSent)
            .arg(rotationSkipped)
            .arg(network.sendErrors)
            .arg(network.reconnects));
}

void MainWindow::updateControlStates()
{
    const SourceState state = serialSource_.state();
    const bool serialBusy = state == SourceState::Opening || state == SourceState::Closing;
    const bool serialOpen = serialSource_.isOpen();
    const bool demoActive = demoSource_.isActive();

    portComboBox_->setEnabled(!serialOpen && !serialBusy && !demoActive);
    refreshPortsButton_->setEnabled(!serialOpen && !serialBusy && !demoActive);
    openPortButton_->setEnabled(!serialOpen && !serialBusy && !demoActive
                                && portComboBox_->currentIndex() >= 0);
    closePortButton_->setEnabled(serialOpen || serialBusy);
    demoModeCheckBox_->setEnabled(!serialBusy);
    calibrateZeroButton_->setEnabled(latestPosesAreValid());
    clearCalibrationButton_->setEnabled(pendingSnapshot_.has_value());
    const bool fixedHost = slimeModeComboBox_
        && SlimeVrDiscoveryMode(slimeModeComboBox_->currentData().toInt())
            == SlimeVrDiscoveryMode::FixedHost;
    if (slimeHostLineEdit_) {
        slimeHostLineEdit_->setEnabled(fixedHost);
    }
}

void MainWindow::closeSerialInput()
{
    if (serialSource_.state() != SourceState::Closed || serialSource_.isOpen()) {
        serialSource_.closePort();
    }
}

bool MainWindow::latestPosesAreValid() const
{
    if (!pendingSnapshot_) {
        return false;
    }
    for (const SensorPose &pose : pendingSnapshot_->poses) {
        if (!pose.valid) {
            return false;
        }
    }
    return true;
}
