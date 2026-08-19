#pragma once

#include <QBluetoothDeviceInfo>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyDescriptor>
#include <QObject>
#include <QSet>
#include <QVector>

class QBluetoothDeviceDiscoveryAgent;
class QLowEnergyController;
class QLowEnergyService;
class QTimer;

namespace handdemo::imu {

enum class BleConnectionState {
    Idle, Scanning, Connecting, DiscoveringServices, Subscribing, Connected, Disconnecting, Error
};

struct DiscoveredDevice {
    QString name;
    QString address;
    QBluetoothDeviceInfo info;
};

class WitBleManager final : public QObject {
    Q_OBJECT

public:
    explicit WitBleManager(QObject *parent = nullptr);
    ~WitBleManager() override;
    void startScan();
    void stopScan();
    void connectToDevice(const QBluetoothDeviceInfo &device);
    void disconnectDevice();
    BleConnectionState state() const;

signals:
    void scanReset();
    void deviceDiscovered(const handdemo::imu::DiscoveredDevice &device);
    void stateChanged(handdemo::imu::BleConnectionState state, const QString &message);
    void notificationReceived(const QByteArray &bytes);
    void errorOccurred(const QString &message);

private:
    void handleDeviceDiscovered(const QBluetoothDeviceInfo &device);
    void handleScanFinished();
    void handleControllerConnected();
    void handleControllerDisconnected();
    void handleServiceDiscovered(const QBluetoothUuid &uuid);
    void handleServiceDiscoveryFinished();
    void handleServiceDetailsDiscovered();
    void handleCharacteristicChanged(const QLowEnergyCharacteristic &characteristic, const QByteArray &value);
    void handleDescriptorWritten(const QLowEnergyDescriptor &descriptor, const QByteArray &value);
    void pollNextCommand();
    void setState(BleConnectionState state, const QString &message);
    void fail(const QString &message);
    void clearConnection(bool disconnectController);
    QString deviceKey(const QBluetoothDeviceInfo &device) const;
    QString displayAddress(const QBluetoothDeviceInfo &device) const;

    QBluetoothDeviceDiscoveryAgent *discoveryAgent_{nullptr};
    QLowEnergyController *controller_{nullptr};
    QLowEnergyService *service_{nullptr};
    QTimer *pollingTimer_{nullptr};
    QTimer *subscriptionTimer_{nullptr};
    QSet<QString> discoveredDeviceKeys_;
    QVector<QByteArray> pollingCommands_;
    QLowEnergyCharacteristic notifyCharacteristic_;
    QLowEnergyCharacteristic writeCharacteristic_;
    QLowEnergyDescriptor notifyDescriptor_;
    BleConnectionState state_{BleConnectionState::Idle};
    qsizetype pollingCommandIndex_{0};
    bool targetServiceFound_{false};
};

}

Q_DECLARE_METATYPE(handdemo::imu::BleConnectionState)
Q_DECLARE_METATYPE(handdemo::imu::DiscoveredDevice)
