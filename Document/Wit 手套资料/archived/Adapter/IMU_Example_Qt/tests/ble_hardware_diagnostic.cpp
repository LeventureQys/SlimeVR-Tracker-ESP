#include "wit_ble_manager.h"
#include "wit_protocol_parser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTextStream>
#include <QTimer>

class BleHardwareDiagnostic final : public QObject {
    Q_OBJECT

public:
    BleHardwareDiagnostic(QString targetName, int timeoutMilliseconds, QObject *parent = nullptr)
        : QObject(parent), targetName_(std::move(targetName))
    {
        connect(&manager_, &WitBleManager::deviceDiscovered, this,
                [this](const DiscoveredDevice &device) {
                    log(QStringLiteral("发现设备 name=%1 address=%2").arg(device.name, device.address));
                    if (!connectionAttempted_ && device.name == targetName_) {
                        connectionAttempted_ = true;
                        log(QStringLiteral("连接目标设备 %1").arg(targetName_));
                        manager_.connectToDevice(device.info);
                    }
                });
        connect(&manager_, &WitBleManager::stateChanged, this,
                [this](BleConnectionState state, const QString &message) {
                    log(QStringLiteral("状态=%1 message=%2").arg(static_cast<int>(state)).arg(message));
                    if (state == BleConnectionState::Connected) {
                        connected_ = true;
                        observationTimer_.start(6000);
                    }
                });
        connect(&manager_, &WitBleManager::errorOccurred, this,
                [this](const QString &message) { finish(false, QStringLiteral("BLE 错误：%1").arg(message)); });
        connect(&manager_, &WitBleManager::notificationReceived, this,
                [this](const QByteArray &bytes) {
                    inspectNotification(bytes);
                    parser_.appendBytes(bytes);
                });
        connect(&parser_, &WitProtocolParser::dataUpdated, this,
                [this](const ImuData &data) {
                    latestData_ = data;
                });
        observationTimer_.setSingleShot(true);
        connect(&observationTimer_, &QTimer::timeout, this, [this]() {
            finish(motionFrameCount_ >= 10,
                   motionFrameCount_ >= 10
                       ? QStringLiteral("真实连接观察完成")
                       : QStringLiteral("真实姿态帧不足 10 帧"));
        });
        timeoutTimer_.setSingleShot(true);
        timeoutTimer_.setInterval(timeoutMilliseconds);
        connect(&timeoutTimer_, &QTimer::timeout, this, [this]() {
            finish(false, QStringLiteral("硬件诊断超时"));
        });
    }

    void start()
    {
        log(QStringLiteral("开始扫描目标设备 %1").arg(targetName_));
        timeoutTimer_.start();
        manager_.startScan();
    }

private:
    void inspectNotification(const QByteArray &bytes)
    {
        QByteArray combined = rawBuffer_ + bytes;
        rawBuffer_.clear();
        while (true) {
            int header = -1;
            for (int index = 0; index + 1 < combined.size(); ++index) {
                const quint8 first = static_cast<quint8>(combined.at(index));
                const quint8 second = static_cast<quint8>(combined.at(index + 1));
                if (first == 0x55 && (second == 0x61 || second == 0x71)) {
                    header = index;
                    break;
                }
            }
            if (header < 0) {
                if (!combined.isEmpty() && static_cast<quint8>(combined.back()) == 0x55) {
                    rawBuffer_ = combined.right(1);
                }
                return;
            }
            combined.remove(0, header);
            if (combined.size() < 20) {
                rawBuffer_ = combined;
                return;
            }
            const QByteArray frame = combined.left(20);
            combined.remove(0, 20);
            if (static_cast<quint8>(frame.at(1)) == 0x61) {
                ++motionFrameCount_;
            } else {
                const quint8 reg = static_cast<quint8>(frame.at(2));
                if (reg == 0x3a) magneticReceived_ = true;
                if (reg == 0x64) batteryReceived_ = true;
                if (reg == 0x40) temperatureReceived_ = true;
                if (reg == 0x2e) versionReceived_ = true;
            }
        }
    }

    void finish(bool success, const QString &reason)
    {
        if (finished_) {
            return;
        }
        finished_ = true;
        timeoutTimer_.stop();
        observationTimer_.stop();
        manager_.stopScan();
        manager_.disconnectDevice();
        log(QStringLiteral("结果=%1 reason=%2").arg(success ? QStringLiteral("PASS") : QStringLiteral("FAIL"), reason));
        log(QStringLiteral("connected=%1 motionFrames=%2 magnetic=%3 battery=%4 temperature=%5 version=%6")
                .arg(connected_).arg(motionFrameCount_).arg(magneticReceived_).arg(batteryReceived_)
                .arg(temperatureReceived_).arg(versionReceived_));
        log(QStringLiteral("Acc=(%1,%2,%3) Gyro=(%4,%5,%6) Angle=(%7,%8,%9) Mag=(%10,%11,%12) Battery=%13 Temperature=%14 Version=%15")
                .arg(latestData_.accelerationX, 0, 'f', 3)
                .arg(latestData_.accelerationY, 0, 'f', 3)
                .arg(latestData_.accelerationZ, 0, 'f', 3)
                .arg(latestData_.angularVelocityX, 0, 'f', 3)
                .arg(latestData_.angularVelocityY, 0, 'f', 3)
                .arg(latestData_.angularVelocityZ, 0, 'f', 3)
                .arg(latestData_.angleX, 0, 'f', 2)
                .arg(latestData_.angleY, 0, 'f', 2)
                .arg(latestData_.angleZ, 0, 'f', 2)
                .arg(latestData_.magneticX, 0, 'f', 3)
                .arg(latestData_.magneticY, 0, 'f', 3)
                .arg(latestData_.magneticZ, 0, 'f', 3)
                .arg(latestData_.batteryPercent, 0, 'f', 0)
                .arg(latestData_.temperatureCelsius, 0, 'f', 2)
                .arg(latestData_.firmwareVersion));
        QCoreApplication::exit(success ? 0 : 1);
    }

    static void log(const QString &message)
    {
        QTextStream(stdout) << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                            << ' ' << message << Qt::endl;
    }

    QString targetName_;
    WitBleManager manager_;
    WitProtocolParser parser_;
    QTimer timeoutTimer_;
    QTimer observationTimer_;
    QByteArray rawBuffer_;
    ImuData latestData_;
    quint64 motionFrameCount_ = 0;
    bool connectionAttempted_ = false;
    bool connected_ = false;
    bool magneticReceived_ = false;
    bool batteryReceived_ = false;
    bool temperatureReceived_ = false;
    bool versionReceived_ = false;
    bool finished_ = false;
};

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QString targetName = application.arguments().value(1, QStringLiteral("WT901BLE67"));
    BleHardwareDiagnostic diagnostic(targetName, 30000);
    QTimer::singleShot(0, &diagnostic, &BleHardwareDiagnostic::start);
    return application.exec();
}

#include "ble_hardware_diagnostic.moc"
