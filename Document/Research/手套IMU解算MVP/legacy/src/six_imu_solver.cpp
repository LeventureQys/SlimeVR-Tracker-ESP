#include "six_imu_solver.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double NominalDtSeconds = 0.005;
constexpr double Pi = 3.14159265358979323846;
constexpr double MinimumQuaternionNorm = 1.0e-12;
const std::array<SensorId, 6> SensorIds = {SensorId::Wrist, SensorId::Thumb, SensorId::Index,
                                           SensorId::Middle, SensorId::Ring, SensorId::Pinky};

Vector3d convertAxes(const RawAxes &axes, double scale)
{
    return {double(axes.x) * scale, double(axes.y) * scale, double(axes.z) * scale};
}

bool finite(const Vector3d &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double vectorNorm(const Vector3d &value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

std::optional<QQuaternion> normalizedQuaternion(const QQuaternion &value)
{
    const double squaredNorm = double(value.scalar()) * value.scalar() + double(value.x()) * value.x()
        + double(value.y()) * value.y() + double(value.z()) * value.z();
    if (!std::isfinite(squaredNorm) || squaredNorm < MinimumQuaternionNorm * MinimumQuaternionNorm) {
        return std::nullopt;
    }
    const double reciprocalNorm = 1.0 / std::sqrt(squaredNorm);
    return QQuaternion(float(value.scalar() * reciprocalNorm), float(value.x() * reciprocalNorm),
                       float(value.y() * reciprocalNorm), float(value.z() * reciprocalNorm));
}

EulerAngles toEulerZyx(const QQuaternion &orientation)
{
    const double w = orientation.scalar();
    const double x = orientation.x();
    const double y = orientation.y();
    const double z = orientation.z();
    const double roll = std::atan2(2.0 * (w * x + y * z), 1.0 - 2.0 * (x * x + y * y));
    const double pitchArgument = std::clamp(2.0 * (w * y - z * x), -1.0, 1.0);
    const double pitch = std::asin(pitchArgument);
    const double yaw = std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
    return {roll * 180.0 / Pi, pitch * 180.0 / Pi, yaw * 180.0 / Pi};
}
}

SixImuSolver::SixImuSolver(QObject *parent)
    : QObject(parent)
    , settings_(SolverSettings::defaults())
    , filters_{MadgwickFilter(settings_.madgwickBeta), MadgwickFilter(settings_.madgwickBeta),
               MadgwickFilter(settings_.madgwickBeta), MadgwickFilter(settings_.madgwickBeta),
               MadgwickFilter(settings_.madgwickBeta), MadgwickFilter(settings_.madgwickBeta)}
{
    zeroOrientations_.fill(QQuaternion(1.0f, 0.0f, 0.0f, 0.0f));
}

SolverSettings SixImuSolver::settings() const
{
    return settings_;
}

void SixImuSolver::applySettings(const SolverSettings &settings)
{
    QString reason;
    if (!settings.isValid(&reason)) {
        diagnostics_.append({handstudio::DiagnosticSeverity::Error,
                             QStringLiteral("solver.settings.invalid"),
                             QStringLiteral("姿态参数非法，忽略"),
                             reason, 0});
        return;
    }
    settings_ = settings;
    resetState(QStringLiteral("姿态参数已应用，融合状态与零位已重置"));
}

void SixImuSolver::processCompleteGroup(const ImuSampleGroup &group)
{
    if (!group.complete) {
        diagnostics_.append({handstudio::DiagnosticSeverity::Warning,
                             QStringLiteral("solver.group.incomplete"),
                             QStringLiteral("组不完整，拒绝处理"), {}, 0});
        return;
    }
    for (const auto &frame : group.frames) {
        if (!frame) {
            diagnostics_.append({handstudio::DiagnosticSeverity::Warning,
                                 QStringLiteral("solver.frame.missing"),
                                 QStringLiteral("组内存在缺失帧，拒绝处理"), {}, 0});
            return;
        }
    }

    bool correctedDt = false;
    double dtSeconds = NominalDtSeconds;
    if (lastGroupMonotonicNs_ != 0) {
        dtSeconds = double(group.emittedMonotonicNs - lastGroupMonotonicNs_) / 1.0e9;
        if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0 || dtSeconds > 0.1) {
            dtSeconds = NominalDtSeconds;
            correctedDt = true;
        }
    }

    auto candidateFilters = filters_;
    SixImuSnapshot snapshot;
    snapshot.sequence = group.sequence;
    snapshot.updatedMonotonicNs = group.emittedMonotonicNs;
    const double accelerationScale = settings_.accelerometerRangeG / 32768.0;
    const double gyroscopeScale = settings_.gyroscopeRangeDps / 32768.0 * Pi / 180.0;
    const double magnetometerScale = 1.0 / settings_.magnetometerDivisor;

    for (int index = 0; index < 6; ++index) {
        const ImuFrame &frame = *group.frames[size_t(index)];
        snapshot.rawFrames[size_t(index)] = frame;
        if (frame.allZero) {
            continue;
        }
        const Vector3d acceleration = convertAxes(frame.acceleration, accelerationScale);
        const Vector3d gyroscope = convertAxes(frame.gyroscope, gyroscopeScale);
        const Vector3d magnetometer = convertAxes(frame.magnetometer, magnetometerScale);
        std::optional<Vector3d> magnetic;
        const double magneticNorm = vectorNorm(magnetometer);
        if (settings_.magnetometerEnabled && finite(magnetometer) && std::isfinite(magneticNorm)
            && magneticNorm >= settings_.magnetometerMinNorm && magneticNorm <= settings_.magnetometerMaxNorm) {
            magnetic = magnetometer;
        }
        if (!candidateFilters[size_t(index)].update(gyroscope, acceleration, magnetic, dtSeconds)) {
            diagnostics_.append({handstudio::DiagnosticSeverity::Warning,
                                 QStringLiteral("solver.filter.rejected"),
                                 QStringLiteral("姿态融合失败，整组回滚"), {}, 0});
            return;
        }
    }

    for (int index = 0; index < 6; ++index) {
        const ImuFrame &frame = snapshot.rawFrames[size_t(index)];
        SensorPose pose;
        pose.sensorId = SensorIds[size_t(index)];
        pose.sequence = group.sequence;
        pose.updatedMonotonicNs = group.emittedMonotonicNs;
        pose.sourceAllZero = frame.allZero;
        pose.valid = !frame.allZero;
        pose.mode = candidateFilters[size_t(index)].mode();
        pose.worldOrientation = candidateFilters[size_t(index)].quaternion();
        if (frame.allZero && latestSnapshot_) {
            const SensorPose &previousPose = latestSnapshot_->poses[size_t(index)];
            pose.worldOrientation = previousPose.worldOrientation;
            pose.mode = previousPose.mode;
        }
        const auto world = normalizedQuaternion(pose.worldOrientation);
        if (!world) {
            diagnostics_.append({handstudio::DiagnosticSeverity::Warning,
                                 QStringLiteral("solver.quaternion.nonNormalizable"),
                                 QStringLiteral("姿态四元数不可归一化，整组回滚"), {}, 0});
            return;
        }
        pose.worldOrientation = *world;
        pose.calibrated = calibrated_;
        const QQuaternion relativeCandidate = calibrated_
            ? zeroOrientations_[size_t(index)].conjugated() * pose.worldOrientation
            : pose.worldOrientation;
        const auto relative = normalizedQuaternion(relativeCandidate);
        if (!relative) {
            diagnostics_.append({handstudio::DiagnosticSeverity::Warning,
                                 QStringLiteral("solver.relative.nonNormalizable"),
                                 QStringLiteral("相对姿态四元数不可归一化，整组回滚"), {}, 0});
            return;
        }
        pose.relativeOrientation = *relative;
        pose.relativeEuler = toEulerZyx(*relative);
        if (frame.allZero) {
            pose.status = QStringLiteral("源帧九轴全零，保留上一姿态");
        } else if (correctedDt) {
            pose.status = QStringLiteral("有效，时间步异常已修正为 0.005 s");
        } else if (pose.mode == FusionMode::NineAxis) {
            pose.status = QStringLiteral("有效，九轴融合");
        } else {
            pose.status = QStringLiteral("有效，磁场退化为六轴融合");
        }
        snapshot.poses[size_t(index)] = pose;
    }

    filters_ = candidateFilters;
    lastGroupMonotonicNs_ = group.emittedMonotonicNs;
    latestSnapshot_ = snapshot;
    emit snapshotReady(snapshot);
}

bool SixImuSolver::calibrateZero(QString *errorMessage)
{
    if (!latestSnapshot_) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("尚无六路姿态快照");
        }
        return false;
    }
    for (const SensorPose &pose : latestSnapshot_->poses) {
        if (!pose.valid) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("六路姿态并非全部有效，无法标定零位");
            }
            return false;
        }
    }
    for (int index = 0; index < 6; ++index) {
        const auto normalized = normalizedQuaternion(latestSnapshot_->poses[size_t(index)].worldOrientation);
        if (!normalized) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("姿态四元数不可归一化，无法标定零位");
            }
            return false;
        }
        zeroOrientations_[size_t(index)] = *normalized;
    }
    calibrated_ = true;
    for (SensorPose &pose : latestSnapshot_->poses) {
        pose.relativeOrientation = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
        pose.relativeEuler = {};
        pose.calibrated = true;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void SixImuSolver::clearCalibration()
{
    calibrated_ = false;
    zeroOrientations_.fill(QQuaternion(1.0f, 0.0f, 0.0f, 0.0f));
    if (latestSnapshot_) {
        for (SensorPose &pose : latestSnapshot_->poses) {
            pose.calibrated = false;
            pose.relativeOrientation = pose.worldOrientation;
            pose.relativeEuler = toEulerZyx(pose.relativeOrientation);
        }
    }
}

void SixImuSolver::reset()
{
    resetState(QStringLiteral("姿态融合状态已重置"));
}

std::optional<SixImuSnapshot> SixImuSolver::latestSnapshot() const
{
    return latestSnapshot_;
}

QVector<handstudio::Diagnostic> SixImuSolver::takeDiagnostics()
{
    QVector<handstudio::Diagnostic> result = diagnostics_;
    diagnostics_.clear();
    return result;
}

std::optional<std::array<handstudio::FusedImuPose, 6>> SixImuSolver::latestFusedPoses() const
{
    if (!latestSnapshot_) {
        return std::nullopt;
    }
    std::array<handstudio::FusedImuPose, 6> result{};
    for (int index = 0; index < 6; ++index) {
        const SensorPose &pose = latestSnapshot_->poses[size_t(index)];
        handstudio::FusedImuPose &out = result[size_t(index)];
        out.sensorId = static_cast<handstudio::SensorId>(static_cast<quint8>(pose.sensorId));
        out.sequence = pose.sequence;
        out.timestampNs = pose.updatedMonotonicNs;
        out.worldOrientation = pose.worldOrientation;
        switch (pose.mode) {
        case FusionMode::SixAxis:
            out.mode = handstudio::FusionMode::SixD;
            break;
        case FusionMode::NineAxis:
            out.mode = handstudio::FusionMode::NineD;
            break;
        case FusionMode::Invalid:
            out.mode = handstudio::FusionMode::Invalid;
            break;
        }
        out.valid = pose.valid;
        out.stale = pose.sourceAllZero;
        out.restDetected = false;
        out.gyroBiasRadPerSec = QVector3D{};
        out.magneticHealth = pose.mode == FusionMode::NineAxis ? handstudio::MagneticHealth::Healthy
                                                               : handstudio::MagneticHealth::Unavailable;
        out.calibrationState = pose.calibrated ? handstudio::CalibrationState::Calibrated
                                               : handstudio::CalibrationState::Uncalibrated;
        out.confidence = pose.valid ? 1.0F : 0.0F;
    }
    return result;
}

void SixImuSolver::resetState(const QString &reason)
{
    for (MadgwickFilter &filter : filters_) {
        filter.setBeta(settings_.madgwickBeta);
        filter.reset();
    }
    zeroOrientations_.fill(QQuaternion(1.0f, 0.0f, 0.0f, 0.0f));
    calibrated_ = false;
    lastGroupMonotonicNs_ = 0;
    latestSnapshot_.reset();
    emit stateReset(reason);
}
