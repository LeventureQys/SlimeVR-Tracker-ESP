#include "calibration_pipeline.h"

#include <cmath>

namespace handstudio {
namespace {

constexpr double Pi = 3.14159265358979323846;
constexpr double StandardGravityMps2 = 9.80665;
constexpr double NominalDtSeconds = 0.005;
constexpr double MaxDtSeconds = 0.1;

bool finiteVector(const QVector3D &value)
{
    return std::isfinite(double(value.x())) && std::isfinite(double(value.y()))
        && std::isfinite(double(value.z()));
}

QVector3D rawToVector(const std::array<qint16, 3> &raw)
{
    return QVector3D(float(raw[0]), float(raw[1]), float(raw[2]));
}

}

CalibrationPipeline::CalibrationPipeline()
{
    for (std::size_t index = 0; index < AllSensorIds.size(); ++index) {
        const SensorId sensorId = AllSensorIds[index];
        states_[index].params = SensorCalibrationParams::defaults(sensorId);
    }
}

void CalibrationPipeline::reset()
{
    for (SensorState &state : states_) {
        state.biasEstimator.reset();
        state.magneticCalibration.reset();
        state.lastTimestampNs = 0;
    }
    diagnostics_.clear();
}

void CalibrationPipeline::addDiagnostic(DiagnosticSeverity severity, QString code, QString message, QString detail)
{
    diagnostics_.append({severity, std::move(code), std::move(message), std::move(detail), 0});
}

void CalibrationPipeline::setParams(SensorId sensorId, const SensorCalibrationParams &params)
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        return;
    }
    states_[static_cast<std::size_t>(*index)].params = params;
    states_[static_cast<std::size_t>(*index)].biasEstimator.reset();
}

SensorCalibrationParams CalibrationPipeline::params(SensorId sensorId) const
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        return SensorCalibrationParams::defaults(sensorId);
    }
    return states_[static_cast<std::size_t>(*index)].params;
}

void CalibrationPipeline::setRestBiasEstimationEnabled(bool enabled)
{
    restBiasEstimationEnabled_ = enabled;
}

bool CalibrationPipeline::restBiasEstimationEnabled() const
{
    return restBiasEstimationEnabled_;
}

void CalibrationPipeline::beginRestBiasEstimation()
{
    for (SensorState &state : states_) {
        state.biasEstimator.reset();
    }
}

bool CalibrationPipeline::restBiasConverged(SensorId sensorId) const
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        return false;
    }
    return states_[static_cast<std::size_t>(*index)].biasEstimator.converged();
}

QVector3D CalibrationPipeline::effectiveGyroBias(SensorId sensorId) const
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        return {};
    }
    const SensorState &state = states_[static_cast<std::size_t>(*index)];
    if (restBiasEstimationEnabled_ && state.biasEstimator.converged()) {
        return state.biasEstimator.bias();
    }
    if (state.params.gyroBiasValid) {
        return state.params.gyroBiasRadPerSec;
    }
    return {};
}

CalibratedImuSample CalibrationPipeline::calibrate(const RawImuFrame &frame)
{
    CalibratedImuSample sample;
    sample.sensorId = frame.sensorId;
    sample.sequence = frame.sequence;
    sample.timestampNs = frame.receivedMonotonicNs;

    const auto index = sensorIndex(frame.sensorId);
    if (!index) {
        sample.valid = false;
        sample.calibrationState = CalibrationState::Invalid;
        addDiagnostic(DiagnosticSeverity::Error, QStringLiteral("calibration.sensor.invalid"),
                      QStringLiteral("无法校准未知传感器帧"));
        return sample;
    }

    SensorState &state = states_[static_cast<std::size_t>(*index)];
    const SensorCalibrationParams &params = state.params;

    QString reason;
    if (!params.isValid(&reason)) {
        sample.valid = false;
        sample.calibrationState = CalibrationState::Invalid;
        addDiagnostic(DiagnosticSeverity::Error, QStringLiteral("calibration.params.invalid"),
                      QStringLiteral("设备校准参数非法"), reason);
        return sample;
    }
    sample.calibrationState = params.calibrationState();

    if (frame.allZero) {
        sample.valid = false;
        addDiagnostic(DiagnosticSeverity::Warning, QStringLiteral("calibration.frame.allZero"),
                      QStringLiteral("原始帧九轴全零，不参与校准"));
        return sample;
    }

    double dtSeconds = NominalDtSeconds;
    if (state.lastTimestampNs != 0) {
        dtSeconds = double(frame.receivedMonotonicNs - state.lastTimestampNs) / 1.0e9;
        if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0 || dtSeconds > MaxDtSeconds) {
            dtSeconds = NominalDtSeconds;
        }
    }
    state.lastTimestampNs = frame.receivedMonotonicNs;

    const double accelerometerScale = params.accelerometerRangeG / 32768.0 * StandardGravityMps2;
    const double gyroscopeScale = params.gyroscopeRangeDps / 32768.0 * Pi / 180.0;
    const double magnetometerScale = params.magnetometerGainMicroTeslaPerLsb;

    const QVector3D acceleration = params.accelerometerAxes.apply(rawToVector(frame.accelerationRaw))
        * accelerometerScale;
    const QVector3D gyroscopeRaw = params.gyroscopeAxes.apply(rawToVector(frame.gyroscopeRaw)) * gyroscopeScale;
    QVector3D magnetic = params.magnetometerAxes.apply(rawToVector(frame.magnetometerRaw)) * magnetometerScale;

    if (!finiteVector(acceleration) || !finiteVector(gyroscopeRaw) || !finiteVector(magnetic)) {
        sample.valid = false;
        addDiagnostic(DiagnosticSeverity::Error, QStringLiteral("calibration.sample.nonFinite"),
                      QStringLiteral("校准后样本包含非有限值"));
        return sample;
    }

    if (params.magnetometerCalibrated) {
        magnetic = MagneticCalibration::apply(magnetic, params.magnetometerHardIronMicroTesla,
                                              params.magnetometerSoftIron);
        if (!finiteVector(magnetic)) {
            sample.valid = false;
            addDiagnostic(DiagnosticSeverity::Error, QStringLiteral("calibration.magnetic.nonFinite"),
                          QStringLiteral("磁校准修正后样本包含非有限值"));
            return sample;
        }
    }

    if (restBiasEstimationEnabled_) {
        state.biasEstimator.update(acceleration, gyroscopeRaw, dtSeconds);
    }

    const QVector3D bias = restBiasEstimationEnabled_
        ? state.biasEstimator.bias()
        : (params.gyroBiasValid ? params.gyroBiasRadPerSec : QVector3D{});
    const QVector3D gyroscope = gyroscopeRaw - bias;

    sample.accelerationMps2 = acceleration;
    sample.gyroscopeRadPerSec = gyroscope;
    sample.magneticMicroTesla = params.magnetometerEnabled ? magnetic : QVector3D{};
    sample.valid = true;
    return sample;
}

std::array<CalibratedImuSample, 6> CalibrationPipeline::calibrateGroup(const SixImuSampleGroup &group)
{
    std::array<CalibratedImuSample, 6> result{};
    for (const RawImuFrame &frame : group.samples) {
        CalibratedImuSample sample = calibrate(frame);
        const auto index = sensorIndex(frame.sensorId);
        if (index) {
            result[static_cast<std::size_t>(*index)] = sample;
        }
    }
    return result;
}

void CalibrationPipeline::collectMagneticSample(SensorId sensorId, const QVector3D &magneticMicroTesla)
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        return;
    }
    states_[static_cast<std::size_t>(*index)].magneticCalibration.addSample(magneticMicroTesla);
}

MagneticCalibration::Result CalibrationPipeline::computeMagneticCalibration(SensorId sensorId) const
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        return {};
    }
    return states_[static_cast<std::size_t>(*index)].magneticCalibration.compute();
}

bool CalibrationPipeline::applyMagneticCalibration(SensorId sensorId, const MagneticCalibration::Result &result,
                                                   QString *error)
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        if (error) {
            *error = QStringLiteral("未知传感器");
        }
        return false;
    }
    if (!result.valid) {
        if (error) {
            *error = result.error;
        }
        return false;
    }
    SensorState &state = states_[static_cast<std::size_t>(*index)];
    state.params.magnetometerHardIronMicroTesla = result.hardIronMicroTesla;
    state.params.magnetometerSoftIron = result.softIron;
    state.params.magnetometerCalibrated = true;
    if (error) {
        error->clear();
    }
    return true;
}

QVector<Diagnostic> CalibrationPipeline::takeDiagnostics()
{
    QVector<Diagnostic> result = diagnostics_;
    diagnostics_.clear();
    return result;
}

}
