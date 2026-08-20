#pragma once

#include "sensor_id.h"

#include <QMetaType>
#include <QQuaternion>
#include <QVector3D>

namespace handstudio {

enum class FusionMode { Invalid, SixD, NineD };
enum class MagneticHealth { Unavailable, Healthy, Disturbed, Recovering };
enum class CalibrationState { Uncalibrated, Partial, Calibrated, Invalid };

struct FusedImuPose {
    SensorId sensorId = SensorId::Wrist;
    quint8 sequence = 0;
    qint64 timestampNs = 0;
    QQuaternion worldOrientation;
    FusionMode mode = FusionMode::Invalid;
    bool valid = false;
    bool stale = false;
    bool restDetected = false;
    QVector3D gyroBiasRadPerSec;
    MagneticHealth magneticHealth = MagneticHealth::Unavailable;
    CalibrationState calibrationState = CalibrationState::Uncalibrated;
    float confidence = 0.0F;
};

}

Q_DECLARE_METATYPE(handstudio::FusionMode)
Q_DECLARE_METATYPE(handstudio::MagneticHealth)
Q_DECLARE_METATYPE(handstudio::CalibrationState)
Q_DECLARE_METATYPE(handstudio::FusedImuPose)
