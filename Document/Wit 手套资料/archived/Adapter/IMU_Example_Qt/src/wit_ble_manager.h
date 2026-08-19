#pragma once

#include <QBluetoothDeviceInfo>
#include <QLowEnergyCharacteristic>
#include <QLowEnergyDescriptor>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class QBluetoothDeviceDiscoveryAgent;
class QLowEnergyController;
class QLowEnergyService;
class QTimer;

enum class BleConnectionState {
    Idle,
    Scanning,
    Connecting,
    DiscoveringServices,
    Subscribing,
    Connected,
    Disconnecting,
    Error
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
    void deviceDiscovered(const DiscoveredDevice &device);
    void stateChanged(BleConnectionState state, const QString &message);
    void notificationReceived(const QByteArray &bytes);
    void errorOccurred(const QString &message);

private:
    void handleDeviceDiscovered(const QBluetoothDeviceInfo &device);
    void handleScanFinished();
    void handleScanError();
    void handleControllerConnected();
    void handleControllerDisconnected();
    void handleServiceDiscovered(const QBluetoothUuid &serviceUuid);
    void handleServiceDiscoveryFinished();
    void handleControllerError();
    void handleServiceDetailsDiscovered();
    void handleCharacteristicChanged(const QLowEnergyCharacteristic &characteristic,
                                     const QByteArray &value);
    void handleDescriptorWritten(const QLowEnergyDescriptor &descriptor,
                                 const QByteArray &value);
    void handleServiceError();
    void pollNextCommand();
    void setState(BleConnectionState state, const QString &message);
    void fail(const QString &message);
    void clearConnection(bool disconnectController);
    QString deviceKey(const QBluetoothDeviceInfo &device) const;
    QString displayAddress(const QBluetoothDeviceInfo &device) const;

    QBluetoothDeviceDiscoveryAgent *discoveryAgent_ = nullptr;
    QLowEnergyController *controller_ = nullptr;
    QLowEnergyService *service_ = nullptr;
    QTimer *pollingTimer_ = nullptr;
    QTimer *subscriptionTimer_ = nullptr;
    QSet<QString> discoveredDeviceKeys_;
    QVector<QByteArray> pollingCommands_;
    QLowEnergyCharacteristic notifyCharacteristic_;
    QLowEnergyCharacteristic writeCharacteristic_;
    QLowEnergyDescriptor notifyDescriptor_;
    BleConnectionState state_ = BleConnectionState::Idle;
    qsizetype pollingCommandIndex_ = 0;
    bool targetServiceFound_ = false;
};

Q_DECLARE_METATYPE(BleConnectionState)
Q_DECLARE_METATYPE(DiscoveredDevice)
