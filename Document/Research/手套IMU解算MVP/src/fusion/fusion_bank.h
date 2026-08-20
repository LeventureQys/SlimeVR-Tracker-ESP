#pragma once

#include "fusion/ifusion_filter.h"
#include "fusion/magnetic_health_monitor.h"
#include "fusion/pose_guard.h"

#include "calibration/calibration_parameters.h"
#include "calibration/rest_detector.h"

#include "core/calibrated_types.h"
#include "core/diagnostic.h"
#include "core/fusion_types.h"

#include <QVector>

#include <array>
#include <memory>

namespace handstudio {

// Six independent fusion channels. Owns per-sensor filters, magnetic health
// monitors, rest detectors and pose guards; computes dt from monotonic
// timestamps (with fallback + diagnostic) and applies pre-publish protection.
class FusionBank {
public:
    enum class Algorithm { Madgwick, Vqf };

    struct Settings {
        double nominalDtSeconds = 0.005;
        double maxDtSeconds = 0.1;
        double confidenceHoldPenalty = 0.1;
        double maxHeadingRecoveryRadPerSec = 0.5;
        MagneticHealthMonitor::Config magneticHealth;
        RestDetector::Config rest;
        PoseGuard::Config guard;

        static Settings defaults();
        bool isValid(QString *reason = nullptr) const;
    };

    explicit FusionBank(Algorithm algorithm = Algorithm::Madgwick);
    ~FusionBank();

    void setAlgorithm(Algorithm algorithm);
    Algorithm algorithm() const;
    void setSettings(const Settings &settings);
    Settings settings() const;
    void setCalibrationParams(SensorId sensorId, const SensorCalibrationParams &params);
    void reset();

    FusedImuPose process(const CalibratedImuSample &sample);
    std::array<FusedImuPose, 6> processGroup(const std::array<CalibratedImuSample, 6> &samples);

    QVector<Diagnostic> takeDiagnostics();

private:
    struct Channel {
        std::unique_ptr<IFusionFilter> filter;
        MagneticHealthMonitor magneticHealth{};
        RestDetector restDetector{};
        PoseGuard poseGuard{};
        SensorCalibrationParams calibrationParams{};
        qint64 lastTimestampNs = 0;
        quint64 zeroDtBatchGroups = 0;
        quint64 fallbackDtGroups = 0;
        double lastFallbackDtSeconds = 0.0;
        qint64 lastDtDiagnosticNs = -1'000'000'000;
        double lastYawRad = 0.0;
        bool hasLastYaw = false;
        FusionMode lastMode = FusionMode::Invalid;
    };

    Algorithm algorithm_ = Algorithm::Madgwick;
    Settings settings_;
    std::array<Channel, 6> channels_{};
    QVector<Diagnostic> diagnostics_;

    void addDiagnostic(DiagnosticSeverity severity, QString code, QString message, QString detail = {});
    void emitRateLimitedDtDiagnostic(Channel &channel, qint64 nowNs);
    FusedImuPose processChannel(Channel &channel, const CalibratedImuSample &sample);
    static std::unique_ptr<IFusionFilter> createFilter(Algorithm algorithm);
};

}
