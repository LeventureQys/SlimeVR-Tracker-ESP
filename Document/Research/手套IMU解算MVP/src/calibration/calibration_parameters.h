#pragma once

#include "axis_remap.h"

#include "core/fusion_types.h"
#include "core/sensor_id.h"

#include <QString>
#include <QVector3D>

#include <array>

namespace handstudio {

// Per-device calibration parameters. Each of the six sensors owns an independent
// instance; the persistence layer additionally binds a non-empty device identity
// and a schema version to the whole document (see calibration_store.h).
struct SensorCalibrationParams {
    SensorId sensorId = SensorId::Wrist;
    QString deviceId;
    int schemaVersion = 0;

    double accelerometerRangeG = 16.0;
    double gyroscopeRangeDps = 2000.0;
    double magnetometerGainMicroTeslaPerLsb = 1.0;

    AxisRemap accelerometerAxes{};
    AxisRemap gyroscopeAxes{};
    AxisRemap magnetometerAxes{};

    QVector3D gyroBiasRadPerSec{};
    QVector3D magnetometerHardIronMicroTesla{};
    std::array<double, 9> magnetometerSoftIron{{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}};

    bool gyroBiasValid = false;
    bool magnetometerCalibrated = false;
    bool magnetometerEnabled = true;

    static SensorCalibrationParams defaults(SensorId sensorId);
    bool isValid(QString *reason = nullptr) const;
    CalibrationState calibrationState() const;
    bool operator==(const SensorCalibrationParams &other) const;
};

}
