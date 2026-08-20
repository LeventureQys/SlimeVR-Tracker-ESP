#include "pose_guard.h"

#include <cmath>

namespace handstudio {

bool PoseGuard::Config::isValid(QString *reason) const
{
    if (!(maxNormError >= 0.0 && maxNormError < 0.5 && std::isfinite(maxNormError))) {
        if (reason) {
            *reason = QStringLiteral("姿态保护范数误差阈值必须在 [0, 0.5)");
        }
        return false;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

PoseGuard::PoseGuard(const Config &config)
    : config_(config)
{
}

void PoseGuard::reset()
{
    hasValid_ = false;
    lastPose_ = QQuaternion();
}

PoseGuard::Config PoseGuard::config() const
{
    return config_;
}

bool PoseGuard::hasValidPose() const
{
    return hasValid_;
}

QQuaternion PoseGuard::lastPose() const
{
    return lastPose_;
}

PoseGuardResult PoseGuard::holdLast() const
{
    PoseGuardResult result;
    result.valid = hasValid_;
    result.held = hasValid_;
    result.orientation = hasValid_ ? lastPose_ : QQuaternion();
    return result;
}

PoseGuardResult PoseGuard::protect(const QQuaternion &candidate)
{
    if (!isFiniteQuaternion(candidate)) {
        return holdLast();
    }
    const double norm = quaternionNorm(candidate);
    if (norm <= 0.0 || !std::isfinite(norm)) {
        return holdLast();
    }
    if (std::abs(norm - 1.0) > config_.maxNormError) {
        return holdLast();
    }

    QQuaternion adjusted = candidate;
    if (std::abs(norm - 1.0) > 1.0e-9) {
        const auto normalized = normalizedQuaternion(candidate);
        if (!normalized) {
            return holdLast();
        }
        adjusted = *normalized;
    }
    if (hasValid_ && quaternionDot(lastPose_, adjusted) < 0.0) {
        adjusted = negateQuaternion(adjusted);
    }

    lastPose_ = adjusted;
    hasValid_ = true;

    PoseGuardResult result;
    result.orientation = adjusted;
    result.valid = true;
    result.held = false;
    return result;
}

}
