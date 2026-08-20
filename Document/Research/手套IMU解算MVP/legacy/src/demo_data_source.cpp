#include "demo_data_source.h"

#include "crc16.h"

#include <QByteArrayView>

#include <algorithm>
#include <cmath>

namespace {
constexpr int FrameSize = 25;
constexpr int GroupSize = 150;
constexpr int SplitOffset = 37;
constexpr double TwoPi = 6.28318530717958647692;
constexpr double PhaseStep = TwoPi / 2000.0;

qint16 boundedAxis(double value)
{
    const double rounded = std::round(value);
    return static_cast<qint16>(std::clamp(rounded, -32768.0, 32767.0));
}

void appendBigEndian(QByteArray &bytes, qint16 value)
{
    const quint16 encoded = static_cast<quint16>(value);
    bytes.append(static_cast<char>((encoded >> 8) & 0xFF));
    bytes.append(static_cast<char>(encoded & 0xFF));
}
}

DemoDataSource::DemoDataSource(QObject *parent)
    : QObject(parent)
{
    timer_.setInterval(5);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &DemoDataSource::generateTick);
}

DemoDataSource::~DemoDataSource()
{
    stop();
}

void DemoDataSource::start()
{
    if (timer_.isActive()) {
        return;
    }

    sequence_ = 0;
    tickIndex_ = 0;
    phase_ = 0.0;
    monotonicTimer_.start();
    timer_.start();
}

void DemoDataSource::stop()
{
    if (timer_.isActive()) {
        timer_.stop();
    }
}

bool DemoDataSource::isActive() const
{
    return timer_.isActive();
}

void DemoDataSource::generateTick()
{
    QByteArray group;
    group.reserve(GroupSize);

    for (int sensorIndex = 0; sensorIndex < 6; ++sensorIndex) {
        group.append(makeFrame(static_cast<quint8>(0x50 + sensorIndex),
                               sequence_, sensorIndex));
    }

    const qint64 timestamp = monotonicTimer_.nsecsElapsed();
    if ((tickIndex_ % 10) == 9) {
        emit bytesReady(group.first(SplitOffset), timestamp);
        emit bytesReady(group.sliced(SplitOffset), timestamp);
    } else {
        emit bytesReady(group, timestamp);
    }

    ++sequence_;
    ++tickIndex_;
    phase_ += PhaseStep;
    if (phase_ >= TwoPi) {
        phase_ -= TwoPi;
    }
}

QByteArray DemoDataSource::makeFrame(quint8 address,
                                     quint8 sequence,
                                     int sensorIndex) const
{
    const double sensorPhase = phase_ + sensorIndex * 0.13;
    const double slowTilt = std::sin(sensorPhase);
    const double slowTurn = std::cos(sensorPhase * 0.5);

    const qint16 accelerationX = boundedAxis(700.0 * slowTilt);
    const qint16 accelerationY = boundedAxis(500.0 * std::cos(sensorPhase));
    const qint16 accelerationZ = boundedAxis(2048.0);

    const qint16 gyroscopeX = boundedAxis(80.0 * slowTurn);
    const qint16 gyroscopeY = boundedAxis(55.0 * slowTilt);
    const qint16 gyroscopeZ = boundedAxis(180.0 + sensorIndex * 12.0);

    const qint16 magnetometerX = boundedAxis(360.0 * std::cos(sensorPhase));
    const qint16 magnetometerY = boundedAxis(360.0 * std::sin(sensorPhase));
    const qint16 magnetometerZ = boundedAxis(120.0 + sensorIndex * 5.0);

    QByteArray frame;
    frame.reserve(FrameSize);
    frame.append(static_cast<char>(0xAA));
    frame.append(static_cast<char>(0x55));
    frame.append(static_cast<char>(address));
    frame.append(static_cast<char>(sequence));
    frame.append(static_cast<char>(0x12));

    appendBigEndian(frame, accelerationX);
    appendBigEndian(frame, accelerationY);
    appendBigEndian(frame, accelerationZ);
    appendBigEndian(frame, gyroscopeX);
    appendBigEndian(frame, gyroscopeY);
    appendBigEndian(frame, gyroscopeZ);
    appendBigEndian(frame, magnetometerX);
    appendBigEndian(frame, magnetometerY);
    appendBigEndian(frame, magnetometerZ);

    const quint16 crc = SixImuProtocol::crc16Modbus(QByteArrayView(frame));
    frame.append(static_cast<char>(crc & 0xFF));
    frame.append(static_cast<char>((crc >> 8) & 0xFF));
    return frame;
}
