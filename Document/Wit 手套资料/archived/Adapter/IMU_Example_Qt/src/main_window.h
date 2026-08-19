#pragma once

#include "imu_data.h"
#include "wit_ble_manager.h"

#include <QMainWindow>
#include <QVector>

class DemoDataSource;
class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTimer;
class WitProtocolParser;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void displayData(const ImuData &data);
    void setDemoModeEnabled(bool enabled);

private:
    void buildInterface();
    void connectSignals();
    void refreshData();
    void resetData();
    void updateControls(BleConnectionState state);
    void addDevice(const DiscoveredDevice &device);
    void startSelectedConnection();
    void handleDemoMode(bool enabled);

    WitBleManager *bleManager_ = nullptr;
    WitProtocolParser *parser_ = nullptr;
    DemoDataSource *demoSource_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    QPushButton *scanButton_ = nullptr;
    QPushButton *stopScanButton_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *disconnectButton_ = nullptr;
    QCheckBox *demoModeCheckBox_ = nullptr;
    QListWidget *deviceListWidget_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QTableWidget *dataTableWidget_ = nullptr;
    QLabel *frameCountLabel_ = nullptr;
    QLabel *lastUpdatedLabel_ = nullptr;
    QVector<QBluetoothDeviceInfo> devices_;
    ImuData pendingData_;
    bool hasPendingData_ = false;
    BleConnectionState connectionState_ = BleConnectionState::Idle;
    QString connectionMessage_ = QStringLiteral("未连接");
};
