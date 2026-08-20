#pragma once

#include "fusion_types.h"

#include <QMetaType>
#include <QVector3D>

namespace handstudio {

struct CalibratedImuSample {
    SensorId sensorId = SensorId::Wrist;
    quint8 sequence = 0;
    qint64 timestampNs = 0;
    QVector3D accelerationMps2;
    QVector3D gyroscopeRadPerSec;
    QVector3D magneticMicroTesla;
    float temperatureC = 0.0F;
    bool temperatureValid = false;
    bool valid = false;
    CalibrationState calibrationState = CalibrationState::Uncalibrated;
};

}

Q_DECLARE_METATYPE(handstudio::CalibratedImuSample)
