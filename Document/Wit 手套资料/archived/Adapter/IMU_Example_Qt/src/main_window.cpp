#include "main_window.h"

#include "demo_data_source.h"
#include "wit_protocol_parser.h"

#include <QCheckBox>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

struct MetricRow {
    QString name;
    QString unit;
};

const QVector<MetricRow> &metricRows()
{
    static const QVector<MetricRow> rows = {
        {QStringLiteral("加速度 X"), QStringLiteral("g")},
        {QStringLiteral("加速度 Y"), QStringLiteral("g")},
        {QStringLiteral("加速度 Z"), QStringLiteral("g")},
        {QStringLiteral("角速度 X"), QStringLiteral("°/s")},
        {QStringLiteral("角速度 Y"), QStringLiteral("°/s")},
        {QStringLiteral("角速度 Z"), QStringLiteral("°/s")},
        {QStringLiteral("角度 X"), QStringLiteral("°")},
        {QStringLiteral("角度 Y"), QStringLiteral("°")},
        {QStringLiteral("角度 Z"), QStringLiteral("°")},
        {QStringLiteral("磁场 X"), QStringLiteral("协议值")},
        {QStringLiteral("磁场 Y"), QStringLiteral("协议值")},
        {QStringLiteral("磁场 Z"), QStringLiteral("协议值")},
        {QStringLiteral("电量"), QStringLiteral("%")},
        {QStringLiteral("温度"), QStringLiteral("℃")},
        {QStringLiteral("固件版本"), QStringLiteral("-")}
    };
    return rows;
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      bleManager_(new WitBleManager(this)),
      parser_(new WitProtocolParser(this)),
      demoSource_(new DemoDataSource(this)),
      refreshTimer_(new QTimer(this))
{
    buildInterface();
    connectSignals();
    refreshTimer_->setInterval(100);
    refreshTimer_->start();
    updateControls(BleConnectionState::Idle);
}

MainWindow::~MainWindow()
{
    demoSource_->stop();
    bleManager_->stopScan();
    bleManager_->disconnectDevice();
}

void MainWindow::displayData(const ImuData &data)
{
    pendingData_ = data;
    hasPendingData_ = true;
}

void MainWindow::setDemoModeEnabled(bool enabled)
{
    demoModeCheckBox_->setChecked(enabled);
}

void MainWindow::buildInterface()
{
    setWindowTitle(QStringLiteral("WIT IMU 实时数据 - Qt"));
    resize(780, 680);

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    auto *deviceGroup = new QGroupBox(QStringLiteral("BLE 设备"), central);
    auto *deviceLayout = new QGridLayout(deviceGroup);

    scanButton_ = new QPushButton(QStringLiteral("扫描"), deviceGroup);
    scanButton_->setObjectName(QStringLiteral("scanButton"));
    stopScanButton_ = new QPushButton(QStringLiteral("停止扫描"), deviceGroup);
    stopScanButton_->setObjectName(QStringLiteral("stopScanButton"));
    connectButton_ = new QPushButton(QStringLiteral("连接"), deviceGroup);
    connectButton_->setObjectName(QStringLiteral("connectButton"));
    disconnectButton_ = new QPushButton(QStringLiteral("断开"), deviceGroup);
    disconnectButton_->setObjectName(QStringLiteral("disconnectButton"));
    demoModeCheckBox_ = new QCheckBox(QStringLiteral("演示模式"), deviceGroup);
    demoModeCheckBox_->setObjectName(QStringLiteral("demoModeCheckBox"));
    deviceListWidget_ = new QListWidget(deviceGroup);
    deviceListWidget_->setObjectName(QStringLiteral("deviceListWidget"));

    deviceLayout->addWidget(scanButton_, 0, 0);
    deviceLayout->addWidget(stopScanButton_, 0, 1);
    deviceLayout->addWidget(connectButton_, 0, 2);
    deviceLayout->addWidget(disconnectButton_, 0, 3);
    deviceLayout->addWidget(demoModeCheckBox_, 0, 4);
    deviceLayout->addWidget(deviceListWidget_, 1, 0, 1, 5);

    statusLabel_ = new QLabel(QStringLiteral("未连接"), central);
    statusLabel_->setObjectName(QStringLiteral("statusLabel"));
    dataTableWidget_ = new QTableWidget(metricRows().size(), 3, central);
    dataTableWidget_->setObjectName(QStringLiteral("dataTableWidget"));
    dataTableWidget_->setHorizontalHeaderLabels(
        {QStringLiteral("指标"), QStringLiteral("数值"), QStringLiteral("单位")});
    dataTableWidget_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    dataTableWidget_->verticalHeader()->setVisible(false);
    dataTableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dataTableWidget_->setSelectionMode(QAbstractItemView::NoSelection);
    for (int row = 0; row < metricRows().size(); ++row) {
        dataTableWidget_->setItem(row, 0, new QTableWidgetItem(metricRows().at(row).name));
        dataTableWidget_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("0")));
        dataTableWidget_->setItem(row, 2, new QTableWidgetItem(metricRows().at(row).unit));
    }

    auto *footerLayout = new QGridLayout;
    frameCountLabel_ = new QLabel(QStringLiteral("帧计数：0"), central);
    frameCountLabel_->setObjectName(QStringLiteral("frameCountLabel"));
    lastUpdatedLabel_ = new QLabel(QStringLiteral("最近更新：--"), central);
    lastUpdatedLabel_->setObjectName(QStringLiteral("lastUpdatedLabel"));
    footerLayout->addWidget(frameCountLabel_, 0, 0);
    footerLayout->addWidget(lastUpdatedLabel_, 0, 1);

    rootLayout->addWidget(deviceGroup);
    rootLayout->addWidget(statusLabel_);
    rootLayout->addWidget(dataTableWidget_, 1);
    rootLayout->addLayout(footerLayout);
    setCentralWidget(central);
}

void MainWindow::connectSignals()
{
    connect(scanButton_, &QPushButton::clicked, this, [this]() {
        demoModeCheckBox_->setChecked(false);
        bleManager_->startScan();
    });
    connect(stopScanButton_, &QPushButton::clicked, bleManager_, &WitBleManager::stopScan);
    connect(connectButton_, &QPushButton::clicked, this, &MainWindow::startSelectedConnection);
    connect(disconnectButton_, &QPushButton::clicked, this, [this]() {
        bleManager_->disconnectDevice();
        parser_->reset();
        resetData();
    });
    connect(demoModeCheckBox_, &QCheckBox::toggled, this, &MainWindow::handleDemoMode);
    connect(deviceListWidget_, &QListWidget::itemSelectionChanged, this, [this]() {
        updateControls(connectionState_);
    });
    connect(bleManager_, &WitBleManager::scanReset, this, [this]() {
        devices_.clear();
        deviceListWidget_->clear();
    });
    connect(bleManager_, &WitBleManager::deviceDiscovered, this, &MainWindow::addDevice);
    connect(bleManager_, &WitBleManager::stateChanged, this,
            [this](BleConnectionState state, const QString &message) {
                connectionState_ = state;
                connectionMessage_ = message;
                statusLabel_->setText(message);
                updateControls(state);
            });
    connect(bleManager_, &WitBleManager::notificationReceived,
            parser_, &WitProtocolParser::appendBytes);
    connect(parser_, &WitProtocolParser::dataUpdated, this, &MainWindow::displayData);
    connect(demoSource_, &DemoDataSource::dataUpdated, this, &MainWindow::displayData);
    connect(refreshTimer_, &QTimer::timeout, this, &MainWindow::refreshData);
}

void MainWindow::refreshData()
{
    if (!hasPendingData_) {
        if (!demoSource_->isActive() && connectionState_ == BleConnectionState::Connected &&
            pendingData_.lastUpdated.isValid() && pendingData_.lastUpdated.msecsTo(QDateTime::currentDateTime()) > 2000) {
            statusLabel_->setText(connectionMessage_ + QStringLiteral(" · 数据等待中"));
        }
        return;
    }

    hasPendingData_ = false;
    const QStringList values = {
        QString::number(pendingData_.accelerationX, 'f', 3),
        QString::number(pendingData_.accelerationY, 'f', 3),
        QString::number(pendingData_.accelerationZ, 'f', 3),
        QString::number(pendingData_.angularVelocityX, 'f', 3),
        QString::number(pendingData_.angularVelocityY, 'f', 3),
        QString::number(pendingData_.angularVelocityZ, 'f', 3),
        QString::number(pendingData_.angleX, 'f', 2),
        QString::number(pendingData_.angleY, 'f', 2),
        QString::number(pendingData_.angleZ, 'f', 2),
        QString::number(pendingData_.magneticX, 'f', 3),
        QString::number(pendingData_.magneticY, 'f', 3),
        QString::number(pendingData_.magneticZ, 'f', 3),
        QString::number(pendingData_.batteryPercent, 'f', 0),
        QString::number(pendingData_.temperatureCelsius, 'f', 2),
        pendingData_.firmwareVersion.isEmpty() ? QStringLiteral("--") : pendingData_.firmwareVersion
    };
    for (int row = 0; row < values.size(); ++row) {
        dataTableWidget_->item(row, 1)->setText(values.at(row));
    }
    frameCountLabel_->setText(QStringLiteral("帧计数：%1").arg(pendingData_.frameCount));
    lastUpdatedLabel_->setText(QStringLiteral("最近更新：%1").arg(
        pendingData_.lastUpdated.isValid()
            ? pendingData_.lastUpdated.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            : QStringLiteral("--")));
    if (!demoSource_->isActive() && connectionState_ == BleConnectionState::Connected) {
        statusLabel_->setText(connectionMessage_);
    }
}

void MainWindow::resetData()
{
    pendingData_ = {};
    hasPendingData_ = true;
}

void MainWindow::updateControls(BleConnectionState state)
{
    const bool demo = demoSource_->isActive() || demoModeCheckBox_->isChecked();
    const bool busy = state == BleConnectionState::Connecting ||
                      state == BleConnectionState::DiscoveringServices ||
                      state == BleConnectionState::Subscribing ||
                      state == BleConnectionState::Disconnecting;
    scanButton_->setEnabled(!demo && !busy && state != BleConnectionState::Connected &&
                            state != BleConnectionState::Scanning);
    stopScanButton_->setEnabled(!demo && state == BleConnectionState::Scanning);
    connectButton_->setEnabled(!demo && !busy && state != BleConnectionState::Connected &&
                               state != BleConnectionState::Scanning &&
                               deviceListWidget_->currentRow() >= 0);
    disconnectButton_->setEnabled(!demo && (state == BleConnectionState::Connected || busy));
    deviceListWidget_->setEnabled(!demo && !busy && state != BleConnectionState::Connected);
}

void MainWindow::addDevice(const DiscoveredDevice &device)
{
    devices_.append(device.info);
    deviceListWidget_->addItem(QStringLiteral("%1 (%2)").arg(device.name, device.address));
}

void MainWindow::startSelectedConnection()
{
    const int row = deviceListWidget_->currentRow();
    if (row < 0 || row >= devices_.size()) {
        statusLabel_->setText(QStringLiteral("请先选择设备"));
        return;
    }
    demoModeCheckBox_->setChecked(false);
    parser_->reset();
    resetData();
    bleManager_->connectToDevice(devices_.at(row));
}

void MainWindow::handleDemoMode(bool enabled)
{
    if (enabled) {
        bleManager_->stopScan();
        bleManager_->disconnectDevice();
        parser_->reset();
        resetData();
        demoSource_->start();
        statusLabel_->setText(QStringLiteral("演示模式 · 非真实设备数据"));
    } else {
        demoSource_->stop();
        connectionState_ = BleConnectionState::Idle;
        connectionMessage_ = QStringLiteral("未连接");
        statusLabel_->setText(connectionMessage_);
    }
    updateControls(connectionState_);
}
