#pragma once

#include "sensor_id.h"

#include <QMetaType>
#include <QQuaternion>

#include <array>

namespace handstudio {

enum class HandSide { Left, Right };

struct FingerObservation {
    SensorId sensorId = SensorId::Thumb;
    QQuaternion worldOrientation;
    QQuaternion palmRelativeOrientation;
    QQuaternion mountCorrectedOrientation;
    float flexionDegrees = 0.0F;
    float abductionDegrees = 0.0F;
    float twistDegrees = 0.0F;
    bool valid = false;
    float confidence = 0.0F;
};

struct HandObservationFrame {
    quint8 sequence = 0;
    qint64 timestampNs = 0;
    HandSide handSide = HandSide::Left;
    QQuaternion wristWorldOrientation;
    std::array<FingerObservation, 5> fingers{};
};

}

Q_DECLARE_METATYPE(handstudio::HandSide)
Q_DECLARE_METATYPE(handstudio::FingerObservation)
Q_DECLARE_METATYPE(handstudio::HandObservationFrame)
