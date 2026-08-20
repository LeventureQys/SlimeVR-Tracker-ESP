#include "calibration_parameters.h"

#include <cmath>

namespace handstudio {
namespace {

bool fail(QString *reason, const QString &message)
{
    if (reason) {
        *reason = message;
    }
    return false;
}

bool finite(double value)
{
    return std::isfinite(value);
}

bool finiteVector(const QVector3D &value)
{
    return finite(double(value.x())) && finite(double(value.y())) && finite(double(value.z()));
}

double determinant3(const std::array<double, 9> &matrix)
{
    const double a = matrix[0];
    const double b = matrix[1];
    const double c = matrix[2];
    const double d = matrix[3];
    const double e = matrix[4];
    const double f = matrix[5];
    const double g = matrix[6];
    const double h = matrix[7];
    const double i = matrix[8];
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

}

SensorCalibrationParams SensorCalibrationParams::defaults(SensorId sensorId)
{
    SensorCalibrationParams result;
    result.sensorId = sensorId;
    return result;
}

bool SensorCalibrationParams::isValid(QString *reason) const
{
    if (!finite(accelerometerRangeG) || accelerometerRangeG <= 0.0 || accelerometerRangeG > 128.0) {
        return fail(reason, QStringLiteral("加速度量程必须在 (0, 128] g"));
    }
    if (!finite(gyroscopeRangeDps) || gyroscopeRangeDps <= 0.0 || gyroscopeRangeDps > 10000.0) {
        return fail(reason, QStringLiteral("陀螺仪量程必须在 (0, 10000] °/s"));
    }
    if (!finite(magnetometerGainMicroTeslaPerLsb) || magnetometerGainMicroTeslaPerLsb <= 0.0) {
        return fail(reason, QStringLiteral("磁力计增益必须为有限正数"));
    }
    if (!accelerometerAxes.isValid(reason)) {
        return false;
    }
    if (!gyroscopeAxes.isValid(reason)) {
        return false;
    }
    if (!magnetometerAxes.isValid(reason)) {
        return false;
    }
    for (double entry : magnetometerSoftIron) {
        if (!finite(entry)) {
            return fail(reason, QStringLiteral("磁软铁矩阵必须为有限数"));
        }
    }
    if (determinant3(magnetometerSoftIron) == 0.0) {
        return fail(reason, QStringLiteral("磁软铁矩阵必须可逆"));
    }
    if (gyroBiasValid && !finiteVector(gyroBiasRadPerSec)) {
        return fail(reason, QStringLiteral("陀螺零偏必须为有限数"));
    }
    if (magnetometerCalibrated && !finiteVector(magnetometerHardIronMicroTesla)) {
        return fail(reason, QStringLiteral("磁硬铁偏移必须为有限数"));
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

CalibrationState SensorCalibrationParams::calibrationState() const
{
    if (!isValid()) {
        return CalibrationState::Invalid;
    }
    if (gyroBiasValid && magnetometerCalibrated) {
        return CalibrationState::Calibrated;
    }
    if (gyroBiasValid || magnetometerCalibrated) {
        return CalibrationState::Partial;
    }
    return CalibrationState::Uncalibrated;
}

bool SensorCalibrationParams::operator==(const SensorCalibrationParams &other) const
{
    return sensorId == other.sensorId
        && deviceId == other.deviceId
        && schemaVersion == other.schemaVersion
        && accelerometerRangeG == other.accelerometerRangeG
        && gyroscopeRangeDps == other.gyroscopeRangeDps
        && magnetometerGainMicroTeslaPerLsb == other.magnetometerGainMicroTeslaPerLsb
        && accelerometerAxes.matrix == other.accelerometerAxes.matrix
        && gyroscopeAxes.matrix == other.gyroscopeAxes.matrix
        && magnetometerAxes.matrix == other.magnetometerAxes.matrix
        && gyroBiasRadPerSec == other.gyroBiasRadPerSec
        && magnetometerHardIronMicroTesla == other.magnetometerHardIronMicroTesla
        && magnetometerSoftIron == other.magnetometerSoftIron
        && gyroBiasValid == other.gyroBiasValid
        && magnetometerCalibrated == other.magnetometerCalibrated
        && magnetometerEnabled == other.magnetometerEnabled;
}

}
