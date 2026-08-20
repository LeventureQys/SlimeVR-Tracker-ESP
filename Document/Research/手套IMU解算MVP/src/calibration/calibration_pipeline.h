#pragma once

#include "calibration_parameters.h"
#include "magnetic_calibration.h"
#include "static_gyro_bias_estimator.h"

#include "core/calibrated_types.h"
#include "core/diagnostic.h"
#include "core/imu_frames.h"

#include <QVector>
#include <QVector3D>

#include <array>

namespace handstudio {

// Converts raw per-sensor frames into CalibratedImuSample values and owns the
// startup stationary gyro-bias estimation and magnetic calibration collection.
// Invalid per-device configuration produces invalid samples and structured
// diagnostics instead of silently using defaults.
class CalibrationPipeline {
public:
    CalibrationPipeline();

    void reset();

    void setParams(SensorId sensorId, const SensorCalibrationParams &params);
    SensorCalibrationParams params(SensorId sensorId) const;

    void setRestBiasEstimationEnabled(bool enabled);
    bool restBiasEstimationEnabled() const;
    void beginRestBiasEstimation();

    bool restBiasConverged(SensorId sensorId) const;
    QVector3D effectiveGyroBias(SensorId sensorId) const;

    CalibratedImuSample calibrate(const RawImuFrame &frame);
    std::array<CalibratedImuSample, 6> calibrateGroup(const SixImuSampleGroup &group);

    void collectMagneticSample(SensorId sensorId, const QVector3D &magneticMicroTesla);
    MagneticCalibration::Result computeMagneticCalibration(SensorId sensorId) const;
    bool applyMagneticCalibration(SensorId sensorId, const MagneticCalibration::Result &result,
                                  QString *error = nullptr);

    QVector<Diagnostic> takeDiagnostics();

private:
    struct SensorState {
        SensorCalibrationParams params{};
        StaticGyroBiasEstimator biasEstimator{};
        MagneticCalibration magneticCalibration{};
        qint64 lastTimestampNs = 0;
    };

    std::array<SensorState, 6> states_{};
    bool restBiasEstimationEnabled_ = true;
    QVector<Diagnostic> diagnostics_;

    void addDiagnostic(DiagnosticSeverity severity, QString code, QString message, QString detail = {});
};

}
