#include "wit_ble_manager.h"

#include <QBluetoothDeviceDiscoveryAgent>
#include <QBluetoothUuid>
#include <QLowEnergyController>
#include <QLowEnergyService>
#include <QTimer>

namespace handdemo::imu {

namespace {

const QBluetoothUuid serviceUuid(QStringLiteral("0000ffe5-0000-1000-8000-00805f9a34fb"));
const QBluetoothUuid notifyUuid(QStringLiteral("0000ffe4-0000-1000-8000-00805f9a34fb"));
const QBluetoothUuid writeUuid(QStringLiteral("0000ffe9-0000-1000-8000-00805f9a34fb"));
const QBluetoothUuid cccdUuid(QStringLiteral("00002902-0000-1000-8000-00805f9b34fb"));

}

WitBleManager::WitBleManager(QObject *parent)
    : QObject(parent), discoveryAgent_(new QBluetoothDeviceDiscoveryAgent(this)),
      pollingTimer_(new QTimer(this)), subscriptionTimer_(new QTimer(this))
{
    discoveryAgent_->setLowEnergyDiscoveryTimeout(10000);
    pollingTimer_->setInterval(500);
    subscriptionTimer_->setInterval(5000);
    subscriptionTimer_->setSingleShot(true);
    connect(discoveryAgent_, &QBluetoothDeviceDiscoveryAgent::deviceDiscovered,
            this, &WitBleManager::handleDeviceDiscovered);
    connect(discoveryAgent_, &QBluetoothDeviceDiscoveryAgent::finished,
            this, &WitBleManager::handleScanFinished);
    connect(discoveryAgent_, &QBluetoothDeviceDiscoveryAgent::canceled,
            this, &WitBleManager::handleScanFinished);
    connect(discoveryAgent_, &QBluetoothDeviceDiscoveryAgent::errorOccurred, this,
            [this](QBluetoothDeviceDiscoveryAgent::Error) {
                fail(QStringLiteral("蓝牙扫描失败：%1").arg(discoveryAgent_->errorString()));
            });
    connect(pollingTimer_, &QTimer::timeout, this, &WitBleManager::pollNextCommand);
    connect(subscriptionTimer_, &QTimer::timeout, this, [this] {
        if (state_ == BleConnectionState::Subscribing) fail(QStringLiteral("WIT 数据通知订阅超时"));
    });
}

WitBleManager::~WitBleManager()
{
    if (discoveryAgent_->isActive()) discoveryAgent_->stop();
    clearConnection(true);
}

void WitBleManager::startScan()
{
    stopScan();
    discoveredDeviceKeys_.clear();
    emit scanReset();
    if (!QBluetoothDeviceDiscoveryAgent::supportedDiscoveryMethods().testFlag(
            QBluetoothDeviceDiscoveryAgent::LowEnergyMethod)) {
        fail(QStringLiteral("当前平台或蓝牙适配器不支持低功耗蓝牙扫描"));
        return;
    }
    setState(BleConnectionState::Scanning, QStringLiteral("正在扫描 WT 设备"));
    discoveryAgent_->start(QBluetoothDeviceDiscoveryAgent::LowEnergyMethod);
}

void WitBleManager::stopScan()
{
    if (discoveryAgent_->isActive()) discoveryAgent_->stop();
    if (state_ == BleConnectionState::Scanning) setState(BleConnectionState::Idle, QStringLiteral("扫描已停止"));
}

void WitBleManager::connectToDevice(const QBluetoothDeviceInfo &device)
{
    stopScan();
    clearConnection(true);
    if (!device.isValid()) {
        fail(QStringLiteral("无法连接：设备信息无效"));
        return;
    }
    controller_ = QLowEnergyController::createCentral(device, this);
    if (!controller_) {
        fail(QStringLiteral("无法创建低功耗蓝牙控制器"));
        return;
    }
    connect(controller_, &QLowEnergyController::connected, this, &WitBleManager::handleControllerConnected);
    connect(controller_, &QLowEnergyController::disconnected, this, &WitBleManager::handleControllerDisconnected);
    connect(controller_, &QLowEnergyController::serviceDiscovered, this, &WitBleManager::handleServiceDiscovered);
    connect(controller_, &QLowEnergyController::discoveryFinished, this, &WitBleManager::handleServiceDiscoveryFinished);
    connect(controller_, &QLowEnergyController::errorOccurred, this, [this](QLowEnergyController::Error) {
        if (controller_) fail(QStringLiteral("蓝牙连接失败：%1").arg(controller_->errorString()));
    });
    setState(BleConnectionState::Connecting, QStringLiteral("正在连接 %1").arg(
                 device.name().isEmpty() ? displayAddress(device) : device.name()));
    controller_->connectToDevice();
}

void WitBleManager::disconnectDevice()
{
    stopScan();
    pollingTimer_->stop();
    if (!controller_) {
        clearConnection(false);
        setState(BleConnectionState::Idle, QStringLiteral("未连接"));
        return;
    }
    setState(BleConnectionState::Disconnecting, QStringLiteral("正在断开设备"));
    controller_->disconnectFromDevice();
}

BleConnectionState WitBleManager::state() const { return state_; }

void WitBleManager::handleDeviceDiscovered(const QBluetoothDeviceInfo &device)
{
    if (state_ != BleConnectionState::Scanning || !device.name().startsWith(QStringLiteral("WT"))) return;
    const QString key = deviceKey(device);
    if (discoveredDeviceKeys_.contains(key)) return;
    discoveredDeviceKeys_.insert(key);
    emit deviceDiscovered({device.name(), displayAddress(device), device});
}

void WitBleManager::handleScanFinished()
{
    if (state_ == BleConnectionState::Scanning) setState(BleConnectionState::Idle, QStringLiteral("扫描完成"));
}

void WitBleManager::handleControllerConnected()
{
    setState(BleConnectionState::DiscoveringServices, QStringLiteral("正在发现设备服务"));
    controller_->discoverServices();
}

void WitBleManager::handleControllerDisconnected()
{
    const bool preserveError = state_ == BleConnectionState::Error;
    clearConnection(false);
    if (!preserveError) setState(BleConnectionState::Idle, QStringLiteral("设备已断开"));
}

void WitBleManager::handleServiceDiscovered(const QBluetoothUuid &uuid)
{
    if (uuid == serviceUuid) targetServiceFound_ = true;
}

void WitBleManager::handleServiceDiscoveryFinished()
{
    if (!controller_ || !targetServiceFound_) {
        fail(QStringLiteral("设备未提供 WIT 数据服务"));
        return;
    }
    service_ = controller_->createServiceObject(serviceUuid, this);
    if (!service_) {
        fail(QStringLiteral("无法创建 WIT 数据服务对象"));
        return;
    }
    connect(service_, &QLowEnergyService::stateChanged, this, [this](QLowEnergyService::ServiceState state) {
        if (state == QLowEnergyService::RemoteServiceDiscovered) handleServiceDetailsDiscovered();
    });
    connect(service_, &QLowEnergyService::characteristicChanged, this, &WitBleManager::handleCharacteristicChanged);
    connect(service_, &QLowEnergyService::descriptorWritten, this, &WitBleManager::handleDescriptorWritten);
    connect(service_, &QLowEnergyService::errorOccurred, this, [this](QLowEnergyService::ServiceError) {
        fail(QStringLiteral("WIT 服务操作失败"));
    });
    service_->discoverDetails();
}

void WitBleManager::handleServiceDetailsDiscovered()
{
    notifyCharacteristic_ = service_->characteristic(notifyUuid);
    writeCharacteristic_ = service_->characteristic(writeUuid);
    if (!notifyCharacteristic_.isValid() || !writeCharacteristic_.isValid()) {
        fail(QStringLiteral("WIT 通知或写入特征缺失"));
        return;
    }
    notifyDescriptor_ = notifyCharacteristic_.descriptor(cccdUuid);
    if (!notifyDescriptor_.isValid()) {
        fail(QStringLiteral("WIT 通知配置描述符缺失"));
        return;
    }
    setState(BleConnectionState::Subscribing, QStringLiteral("正在订阅 WIT 数据通知"));
    subscriptionTimer_->start();
    service_->writeDescriptor(notifyDescriptor_, QByteArray::fromHex("0100"));
}

void WitBleManager::handleCharacteristicChanged(const QLowEnergyCharacteristic &characteristic,
                                                const QByteArray &value)
{
    if (state_ == BleConnectionState::Connected && characteristic.uuid() == notifyUuid) emit notificationReceived(value);
}

void WitBleManager::handleDescriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &value)
{
    if (state_ != BleConnectionState::Subscribing || descriptor != notifyDescriptor_
        || value != QByteArray::fromHex("0100")) return;
    subscriptionTimer_->stop();
    pollingCommands_ = {QByteArray::fromHex("ffaa273a00"), QByteArray::fromHex("ffaa276400"),
                        QByteArray::fromHex("ffaa274000"), QByteArray::fromHex("ffaa272e00")};
    setState(BleConnectionState::Connected, QStringLiteral("设备已连接"));
    pollingTimer_->start();
}

void WitBleManager::pollNextCommand()
{
    if (state_ != BleConnectionState::Connected || !service_ || pollingCommands_.isEmpty()) return;
    const auto mode = writeCharacteristic_.properties().testFlag(QLowEnergyCharacteristic::WriteNoResponse)
        ? QLowEnergyService::WriteWithoutResponse : QLowEnergyService::WriteWithResponse;
    service_->writeCharacteristic(writeCharacteristic_, pollingCommands_.at(pollingCommandIndex_), mode);
    pollingCommandIndex_ = (pollingCommandIndex_ + 1) % pollingCommands_.size();
}

void WitBleManager::setState(BleConnectionState state, const QString &message)
{
    state_ = state;
    emit stateChanged(state, message);
}

void WitBleManager::fail(const QString &message)
{
    setState(BleConnectionState::Error, message);
    emit errorOccurred(message);
    clearConnection(true);
}

void WitBleManager::clearConnection(bool disconnectController)
{
    pollingTimer_->stop();
    subscriptionTimer_->stop();
    pollingCommands_.clear();
    pollingCommandIndex_ = 0;
    targetServiceFound_ = false;
    notifyCharacteristic_ = {};
    writeCharacteristic_ = {};
    notifyDescriptor_ = {};
    if (service_) {
        service_->disconnect(this);
        service_->deleteLater();
        service_ = nullptr;
    }
    if (controller_) {
        controller_->disconnect(this);
        if (disconnectController && controller_->state() != QLowEnergyController::UnconnectedState)
            controller_->disconnectFromDevice();
        controller_->deleteLater();
        controller_ = nullptr;
    }
}

QString WitBleManager::deviceKey(const QBluetoothDeviceInfo &device) const
{
    if (!device.deviceUuid().isNull()) return QStringLiteral("uuid:%1").arg(device.deviceUuid().toString());
    if (!device.address().isNull()) return QStringLiteral("address:%1").arg(device.address().toString());
    return QStringLiteral("name:%1").arg(device.name());
}

QString WitBleManager::displayAddress(const QBluetoothDeviceInfo &device) const
{
    if (!device.address().isNull()) return device.address().toString();
    if (!device.deviceUuid().isNull()) return device.deviceUuid().toString();
    return QStringLiteral("地址不可用");
}

}
