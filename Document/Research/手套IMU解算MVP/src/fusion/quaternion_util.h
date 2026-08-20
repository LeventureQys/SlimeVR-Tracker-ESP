#pragma once

#include <QQuaternion>

#include <cmath>
#include <optional>

namespace handstudio {

inline constexpr double QuaternionPi = 3.14159265358979323846;

inline double quaternionNormSquared(const QQuaternion &q)
{
    return double(q.scalar()) * q.scalar() + double(q.x()) * q.x() + double(q.y()) * q.y()
        + double(q.z()) * q.z();
}

inline double quaternionNorm(const QQuaternion &q)
{
    return std::sqrt(quaternionNormSquared(q));
}

inline bool isFiniteQuaternion(const QQuaternion &q)
{
    return std::isfinite(double(q.scalar())) && std::isfinite(double(q.x()))
        && std::isfinite(double(q.y())) && std::isfinite(double(q.z()));
}

inline double quaternionNormError(const QQuaternion &q)
{
    return std::abs(quaternionNorm(q) - 1.0);
}

inline std::optional<QQuaternion> normalizedQuaternion(const QQuaternion &q)
{
    const double squared = quaternionNormSquared(q);
    if (!std::isfinite(squared) || squared <= 0.0) {
        return std::nullopt;
    }
    const double reciprocal = 1.0 / std::sqrt(squared);
    return QQuaternion(float(q.scalar() * reciprocal), float(q.x() * reciprocal),
                       float(q.y() * reciprocal), float(q.z() * reciprocal));
}

inline double quaternionDot(const QQuaternion &a, const QQuaternion &b)
{
    return double(a.scalar()) * b.scalar() + double(a.x()) * b.x() + double(a.y()) * b.y()
        + double(a.z()) * b.z();
}

inline QQuaternion negateQuaternion(const QQuaternion &q)
{
    return QQuaternion(-q.scalar(), -q.x(), -q.y(), -q.z());
}

// ZYX-convention yaw (rotation about the world +Z axis), in radians.
inline double yawZyxRadians(const QQuaternion &q)
{
    const double w = double(q.scalar());
    const double x = double(q.x());
    const double y = double(q.y());
    const double z = double(q.z());
    return std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z));
}

inline double wrapToPi(double angle)
{
    angle = std::fmod(angle + QuaternionPi, 2.0 * QuaternionPi);
    if (angle < 0.0) {
        angle += 2.0 * QuaternionPi;
    }
    return angle - QuaternionPi;
}

}
