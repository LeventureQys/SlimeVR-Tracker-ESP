#pragma once

#include "slimevr_pose_adapter.h"
#include "slimevr_udp_client.h"

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <array>
#include <optional>

// Converts latest SixImuSnapshot data into SensorInfo + RotationData traffic.
// Holds exactly one pending snapshot (latest wins), never queues history.
// SensorInfo is sent immediately on (re)connect and re-sent once per second
// while connected, mirroring the firmware update cadence.
class SlimeVrPoseSender final : public QObject {
    Q_OBJECT

public:
    struct Statistics {
        std::array<quint64, 6> rotationSent{};
        std::array<quint64, 6> rotationSkipped{};
        quint64 sensorInfoSent = 0;
        quint64 sendFailures = 0;
    };

    explicit SlimeVrPoseSender(SlimeVrUdpClient *client, QObject *parent = nullptr);

    void setGloveSide(GloveSide side);
    void setSendRateHz(int rateHz);
    void setMaxAgeMs(qint64 maxAgeMs);
    void setMountings(const std::array<QQuaternion, 6> &mountings);

    void submitSnapshot(const SixImuSnapshot &snapshot);
    void start();
    void stop();

    Statistics statistics() const;

signals:
    void statisticsChanged();

private:
    void onTick();
    void onClientStateChanged(SlimeVrConnectionState state);
    void sendSensorInfos();
    void markStatisticsDirty();

    SlimeVrUdpClient *client_ = nullptr;
    GloveSide gloveSide_ = GloveSide::Left;
    int sendRateHz_ = 75;
    qint64 maxAgeMs_ = 500;
    SlimeVrPoseAdapter adapter_;
    std::optional<SixImuSnapshot> latestSnapshot_;
    qint64 latestArrivalMs_ = 0;
    qint64 lastSensorInfoMs_ = -1;

    QTimer tickTimer_;
    QTimer statisticsTimer_;
    QElapsedTimer monotonicClock_;
    bool statisticsDirty_ = false;
    Statistics statistics_;
};
