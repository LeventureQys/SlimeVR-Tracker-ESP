#include "fusion_bank.h"

#include "fusion/madgwick_fusion_filter.h"
#include "fusion/vqf_fusion_filter.h"

#include <algorithm>
#include <cmath>

namespace handstudio {
namespace {

constexpr double Pi = 3.14159265358979323846;

float baseConfidence(CalibrationState calibrationState, MagneticHealth health)
{
    float base = 0.5F;
    switch (calibrationState) {
    case CalibrationState::Invalid:
        base = 0.1F;
        break;
    case CalibrationState::Uncalibrated:
        base = 0.5F;
        break;
    case CalibrationState::Partial:
        base = 0.7F;
        break;
    case CalibrationState::Calibrated:
        base = 0.9F;
        break;
    }
    if (health == MagneticHealth::Healthy) {
        base = std::min(base + 0.05F, 1.0F);
    } else if (health == MagneticHealth::Disturbed) {
        base = std::max(base - 0.1F, 0.1F);
    }
    return base;
}

}

FusionBank::Settings FusionBank::Settings::defaults()
{
    return Settings{};
}

bool FusionBank::Settings::isValid(QString *reason) const
{
    if (!(nominalDtSeconds > 0.0 && std::isfinite(nominalDtSeconds) && nominalDtSeconds <= 0.1)
        || !(maxDtSeconds > 0.0 && std::isfinite(maxDtSeconds) && maxDtSeconds <= 0.5)
        || !(confidenceHoldPenalty >= 0.0 && confidenceHoldPenalty <= 1.0
             && std::isfinite(confidenceHoldPenalty))
        || !(maxHeadingRecoveryRadPerSec >= 0.0 && std::isfinite(maxHeadingRecoveryRadPerSec))
        || !magneticHealth.isValid(reason) || !rest.isValid(reason) || !guard.isValid(reason)) {
        if (reason && reason->isEmpty()) {
            *reason = QStringLiteral("融合参数非法");
        }
        return false;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

FusionBank::FusionBank(Algorithm algorithm)
    : algorithm_(algorithm)
    , settings_(Settings::defaults())
{
    for (std::size_t index = 0; index < AllSensorIds.size(); ++index) {
        channels_[index].filter = createFilter(algorithm);
        channels_[index].magneticHealth = MagneticHealthMonitor(settings_.magneticHealth);
        channels_[index].restDetector = RestDetector(settings_.rest);
        channels_[index].poseGuard = PoseGuard(settings_.guard);
        channels_[index].calibrationParams = SensorCalibrationParams::defaults(AllSensorIds[index]);
    }
}

FusionBank::~FusionBank() = default;

void FusionBank::addDiagnostic(DiagnosticSeverity severity, QString code, QString message, QString detail)
{
    diagnostics_.append({severity, std::move(code), std::move(message), std::move(detail), 0});
}

void FusionBank::emitRateLimitedDtDiagnostic(Channel &channel, qint64 nowNs)
{
    // One aggregated diagnostic per second per channel; high-frequency dt
    // anomalies (batch arrival zero-dt, out-of-range dt) would otherwise
    // flood recordings and starve each other's rate-limit slots.
    if (nowNs - channel.lastDtDiagnosticNs < 1'000'000'000) {
        return;
    }
    channel.lastDtDiagnosticNs = nowNs;
    addDiagnostic(DiagnosticSeverity::Warning, QStringLiteral("fusion.dt.anomaly"),
                  QStringLiteral("时间步异常（批量到达或超限），使用标称 dt"),
                  QStringLiteral("零dt组=%1 回退组=%2 最近dt=%3 s")
                      .arg(channel.zeroDtBatchGroups)
                      .arg(channel.fallbackDtGroups)
                      .arg(channel.lastFallbackDtSeconds));
    channel.zeroDtBatchGroups = 0;
    channel.fallbackDtGroups = 0;
    channel.lastFallbackDtSeconds = 0.0;
}

std::unique_ptr<IFusionFilter> FusionBank::createFilter(Algorithm algorithm)
{
    switch (algorithm) {
    case Algorithm::Madgwick:
        return std::make_unique<MadgwickFusionFilter>();
    case Algorithm::Vqf:
        return std::make_unique<VqfFusionFilter>();
    }
    return std::make_unique<MadgwickFusionFilter>();
}

void FusionBank::setAlgorithm(Algorithm algorithm)
{
    algorithm_ = algorithm;
    for (Channel &channel : channels_) {
        channel.filter = createFilter(algorithm);
        channel.poseGuard.reset();
        channel.lastTimestampNs = 0;
        channel.lastYawRad = 0.0;
        channel.hasLastYaw = false;
        channel.lastMode = FusionMode::Invalid;
        channel.restDetector.reset();
        channel.magneticHealth.reset();
    }
    diagnostics_.clear();
}

FusionBank::Algorithm FusionBank::algorithm() const
{
    return algorithm_;
}

void FusionBank::setSettings(const Settings &settings)
{
    if (!settings.isValid()) {
        addDiagnostic(DiagnosticSeverity::Error, QStringLiteral("fusion.settings.invalid"),
                      QStringLiteral("融合参数非法，忽略"));
        return;
    }
    settings_ = settings;
    for (Channel &channel : channels_) {
        channel.magneticHealth.setConfig(settings_.magneticHealth);
        channel.restDetector.setConfig(settings_.rest);
        channel.poseGuard = PoseGuard(settings_.guard);
    }
}

FusionBank::Settings FusionBank::settings() const
{
    return settings_;
}

void FusionBank::setCalibrationParams(SensorId sensorId, const SensorCalibrationParams &params)
{
    const auto index = sensorIndex(sensorId);
    if (!index) {
        return;
    }
    channels_[static_cast<std::size_t>(*index)].calibrationParams = params;
}

void FusionBank::reset()
{
    for (Channel &channel : channels_) {
        channel.filter->reset();
        channel.magneticHealth.reset();
        channel.restDetector.reset();
        channel.poseGuard.reset();
        channel.lastTimestampNs = 0;
        channel.lastYawRad = 0.0;
        channel.hasLastYaw = false;
        channel.lastMode = FusionMode::Invalid;
    }
    diagnostics_.clear();
}

FusedImuPose FusionBank::process(const CalibratedImuSample &sample)
{
    const auto index = sensorIndex(sample.sensorId);
    if (!index) {
        FusedImuPose pose;
        pose.sensorId = sample.sensorId;
        pose.valid = false;
        pose.mode = FusionMode::Invalid;
        addDiagnostic(DiagnosticSeverity::Error, QStringLiteral("fusion.sensor.invalid"),
                      QStringLiteral("无法处理未知传感器样本"));
        return pose;
    }
    return processChannel(channels_[static_cast<std::size_t>(*index)], sample);
}

std::array<FusedImuPose, 6> FusionBank::processGroup(const std::array<CalibratedImuSample, 6> &samples)
{
    std::array<FusedImuPose, 6> result{};
    for (const CalibratedImuSample &sample : samples) {
        const auto index = sensorIndex(sample.sensorId);
        if (index) {
            result[static_cast<std::size_t>(*index)] = process(sample);
        }
    }
    return result;
}

FusedImuPose FusionBank::processChannel(Channel &channel, const CalibratedImuSample &sample)
{
    FusedImuPose result;
    result.sensorId = sample.sensorId;
    result.sequence = sample.sequence;
    result.timestampNs = sample.timestampNs;
    result.calibrationState = sample.calibrationState;
    result.gyroBiasRadPerSec = channel.calibrationParams.gyroBiasValid
        ? channel.calibrationParams.gyroBiasRadPerSec
        : QVector3D{};

    double dtSeconds = settings_.nominalDtSeconds;
    if (channel.lastTimestampNs != 0) {
        dtSeconds = double(sample.timestampNs - channel.lastTimestampNs) / 1.0e9;
        if (!std::isfinite(dtSeconds) || dtSeconds > settings_.maxDtSeconds) {
            // Out-of-range or non-finite dt: use nominal dt, aggregate the event.
            ++channel.fallbackDtGroups;
            channel.lastFallbackDtSeconds = dtSeconds;
            dtSeconds = settings_.nominalDtSeconds;
        } else if (dtSeconds <= 0.0) {
            // Batch arrival: several complete groups share one serial-read
            // timestamp (real hardware delivers ~200 groups/s in chunks, so
            // timestamps quantize into ~130 ms buckets). Treat as nominal dt
            // and aggregate instead of flooding one diagnostic per group.
            ++channel.zeroDtBatchGroups;
            dtSeconds = settings_.nominalDtSeconds;
        }
        if (channel.zeroDtBatchGroups || channel.fallbackDtGroups) {
            emitRateLimitedDtDiagnostic(channel, sample.timestampNs);
        }
    }
    channel.lastTimestampNs = sample.timestampNs;

    channel.restDetector.update(sample.accelerationMps2, sample.gyroscopeRadPerSec, dtSeconds);
    result.restDetected = channel.restDetector.isRest();

    const MagneticHealth health = channel.magneticHealth.update(sample.magneticMicroTesla);
    result.magneticHealth = health;

    if (!sample.valid) {
        result.valid = false;
        result.mode = FusionMode::Invalid;
        result.confidence = 0.0F;
        result.stale = false;
        return result;
    }

    const bool useMagnetometer = (health == MagneticHealth::Healthy || health == MagneticHealth::Recovering)
        && channel.calibrationParams.magnetometerEnabled;
    CalibratedImuSample fusionSample = sample;
    if (!useMagnetometer) {
        fusionSample.magneticMicroTesla = QVector3D{};
    }

    const FusedImuPose candidate = channel.filter->update(fusionSample, dtSeconds);
    if (!candidate.valid) {
        addDiagnostic(DiagnosticSeverity::Warning, QStringLiteral("fusion.filter.rejected"),
                      QStringLiteral("融合器拒绝本帧，保持上一有效姿态"));
    }
    PoseGuardResult guarded = candidate.valid
        ? channel.poseGuard.protect(candidate.worldOrientation)
        : channel.poseGuard.holdLast();

    if (!guarded.valid) {
        result.valid = false;
        result.mode = FusionMode::Invalid;
        result.confidence = 0.0F;
        result.stale = false;
        return result;
    }

    QQuaternion orientation = guarded.orientation;
    if (health == MagneticHealth::Recovering && !guarded.held) {
        const double candidateYaw = yawZyxRadians(orientation);
        if (!channel.hasLastYaw) {
            channel.lastYawRad = candidateYaw;
            channel.hasLastYaw = true;
        } else {
            const double deltaYaw = wrapToPi(candidateYaw - channel.lastYawRad);
            const double maxStep = settings_.maxHeadingRecoveryRadPerSec * dtSeconds;
            const double applied = std::clamp(deltaYaw, -maxStep, maxStep);
            if (std::abs(deltaYaw - applied) > 1.0e-12) {
                const double correction = applied - deltaYaw;
                const QQuaternion yawRotation = QQuaternion::fromAxisAndAngle(
                    0.0F, 0.0F, 1.0F, float(correction * 180.0 / Pi));
                orientation = yawRotation * orientation;
                const auto normalized = normalizedQuaternion(orientation);
                if (normalized) {
                    orientation = *normalized;
                }
            }
            channel.lastYawRad = wrapToPi(channel.lastYawRad + applied);
        }
    } else {
        channel.lastYawRad = yawZyxRadians(orientation);
        channel.hasLastYaw = true;
    }

    result.worldOrientation = orientation;
    result.mode = guarded.held ? channel.lastMode : candidate.mode;
    if (!guarded.held) {
        channel.lastMode = candidate.mode;
    }
    result.valid = true;
    result.stale = guarded.held;

    float confidence = baseConfidence(sample.calibrationState, health);
    if (guarded.held) {
        confidence *= float(1.0 - settings_.confidenceHoldPenalty);
    }
    result.confidence = std::clamp(confidence, 0.0F, 1.0F);
    return result;
}

QVector<Diagnostic> FusionBank::takeDiagnostics()
{
    QVector<Diagnostic> result = diagnostics_;
    diagnostics_.clear();
    return result;
}

}
