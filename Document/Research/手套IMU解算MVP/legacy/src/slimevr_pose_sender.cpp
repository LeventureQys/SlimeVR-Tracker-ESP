#include "slimevr_pose_sender.h"

#include "slimevr_protocol.h"
#include "slimevr_sensor_mapping.h"

#include <algorithm>

namespace {
constexpr qint64 SensorInfoResendIntervalMs = 1000;
constexpr int StatisticsFlushIntervalMs = 250;
constexpr int MinSendRateHz = 1;
constexpr int MaxSendRateHz = 200;
} // namespace

SlimeVrPoseSender::SlimeVrPoseSender(SlimeVrUdpClient *client, QObject *parent)
    : QObject(parent)
    , client_(client)
{
    tickTimer_.setInterval(qMax(1, 1000 / sendRateHz_));
    connect(&tickTimer_, &QTimer::timeout, this, &SlimeVrPoseSender::onTick);

    statisticsTimer_.setInterval(StatisticsFlushIntervalMs);
    connect(&statisticsTimer_, &QTimer::timeout, this, [this] {
        if (statisticsDirty_) {
            statisticsDirty_ = false;
            emit statisticsChanged();
        }
    });

    if (client_) {
        connect(client_, &SlimeVrUdpClient::stateChanged, this, &SlimeVrPoseSender::onClientStateChanged);
    }
    monotonicClock_.start();
}

void SlimeVrPoseSender::setGloveSide(GloveSide side)
{
    gloveSide_ = side;
    adapter_.setGloveSide(side);
}

void SlimeVrPoseSender::setSendRateHz(int rateHz)
{
    sendRateHz_ = std::clamp(rateHz, MinSendRateHz, MaxSendRateHz);
    tickTimer_.setInterval(qMax(1, 1000 / sendRateHz_));
}

void SlimeVrPoseSender::setMaxAgeMs(qint64 maxAgeMs)
{
    maxAgeMs_ = maxAgeMs;
}

void SlimeVrPoseSender::setMountings(const std::array<QQuaternion, 6> &mountings)
{
    adapter_.setMountings(mountings);
}

void SlimeVrPoseSender::submitSnapshot(const SixImuSnapshot &snapshot)
{
    latestSnapshot_ = snapshot;
    latestArrivalMs_ = monotonicClock_.elapsed();
}

void SlimeVrPoseSender::start()
{
    if (!client_) {
        return;
    }
    lastSensorInfoMs_ = -1;
    tickTimer_.start();
    statisticsTimer_.start();
}

void SlimeVrPoseSender::stop()
{
    tickTimer_.stop();
    statisticsTimer_.stop();
    latestSnapshot_.reset();
    latestArrivalMs_ = 0;
    lastSensorInfoMs_ = -1;
}

SlimeVrPoseSender::Statistics SlimeVrPoseSender::statistics() const
{
    return statistics_;
}

void SlimeVrPoseSender::onClientStateChanged(SlimeVrConnectionState state)
{
    if (state == SlimeVrConnectionState::Connected) {
        lastSensorInfoMs_ = -1;
        sendSensorInfos();
    }
}

void SlimeVrPoseSender::onTick()
{
    if (!client_ || !client_->isConnected()) {
        return;
    }
    const qint64 nowMs = monotonicClock_.elapsed();
    if (lastSensorInfoMs_ < 0 || nowMs - lastSensorInfoMs_ >= SensorInfoResendIntervalMs) {
        sendSensorInfos();
        lastSensorInfoMs_ = nowMs;
    }

    if (!latestSnapshot_) {
        return;
    }
    if (latestArrivalMs_ == 0 || nowMs - latestArrivalMs_ > maxAgeMs_) {
        // Stale snapshot: skip all rotations this tick, never replay history.
        return;
    }

    const std::array<SlimeVrPoseSample, 6> samples = adapter_.adapt(*latestSnapshot_);
    for (int index = 0; index < 6; ++index) {
        const SlimeVrPoseSample &sample = samples[size_t(index)];
        if (!sample.valid) {
            statistics_.rotationSkipped[size_t(index)]++;
            markStatisticsDirty();
            continue;
        }
        const QByteArray payload = SlimeVrProtocol::encodeRotationData(
            sample.sensorId,
            1, // DATA_TYPE_NORMAL
            sample.orientation.x(),
            sample.orientation.y(),
            sample.orientation.z(),
            sample.orientation.scalar(),
            0); // accuracy info
        if (client_->sendNormalPacket(
                static_cast<quint32>(SlimeVrProtocol::SendPacketType::RotationData), payload)) {
            statistics_.rotationSent[size_t(index)]++;
        } else {
            statistics_.sendFailures++;
        }
        markStatisticsDirty();
    }
}

void SlimeVrPoseSender::sendSensorInfos()
{
    if (!client_ || !client_->isConnected()) {
        return;
    }
    const auto descriptors = SlimeVrSensorMapping::descriptors(gloveSide_);
    for (const SlimeVrSensorDescriptor &descriptor : descriptors) {
        const SlimeVrProtocol::SensorInfoFields fields{
            descriptor.sensorId,
            1, // SENSOR_OK
            0, // SensorTypeID::Unknown (fused externally)
            0, // sensor config data
            0, // rest calibration not completed
            descriptor.sensorPosition,
            0, // SENSOR_DATATYPE_ROTATION
        };
        if (client_->sendNormalPacket(
                static_cast<quint32>(SlimeVrProtocol::SendPacketType::SensorInfo),
                SlimeVrProtocol::encodeSensorInfo(fields))) {
            statistics_.sensorInfoSent++;
        } else {
            statistics_.sendFailures++;
        }
        markStatisticsDirty();
    }
}

void SlimeVrPoseSender::markStatisticsDirty()
{
    statisticsDirty_ = true;
}
