#pragma once

#include "sensor_id.h"

#include <QMetaType>
#include <QtGlobal>

#include <array>

namespace handstudio {

struct RawImuFrame {
    SensorId sensorId = SensorId::Wrist;
    quint8 address = 0;
    quint8 sequence = 0;
    qint64 receivedMonotonicNs = 0;
    std::array<qint16, 3> accelerationRaw{};
    std::array<qint16, 3> gyroscopeRaw{};
    std::array<qint16, 3> magnetometerRaw{};
    bool crcValid = false;
    bool allZero = true;
};

struct SixImuSampleGroup {
    quint8 sequence = 0;
    qint64 emittedMonotonicNs = 0;
    std::array<RawImuFrame, 6> samples{};
    bool complete = false;
    quint8 presentMask = 0;
};

}

Q_DECLARE_METATYPE(handstudio::RawImuFrame)
Q_DECLARE_METATYPE(handstudio::SixImuSampleGroup)
