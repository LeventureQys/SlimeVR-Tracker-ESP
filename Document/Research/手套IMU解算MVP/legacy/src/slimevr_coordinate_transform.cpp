#include "slimevr_coordinate_transform.h"

#include <cmath>

namespace {
constexpr double MinimumNorm = 1.0e-9;
constexpr double MaximumNormError = 1.0e-3;

bool finite(const QQuaternion &q)
{
    return std::isfinite(double(q.scalar())) && std::isfinite(double(q.x()))
        && std::isfinite(double(q.y())) && std::isfinite(double(q.z()));
}

double norm(const QQuaternion &q)
{
    return std::sqrt(double(q.scalar()) * q.scalar() + double(q.x()) * q.x()
                     + double(q.y()) * q.y() + double(q.z()) * q.z());
}
} // namespace

std::optional<QQuaternion> sanitizeMountingRotation(const QQuaternion &value)
{
    if (!finite(value)) {
        return std::nullopt;
    }
    const double valueNorm = norm(value);
    if (valueNorm < MinimumNorm || std::abs(valueNorm - 1.0) > MaximumNormError) {
        return std::nullopt;
    }
    return value / float(valueNorm);
}

SlimeVrCoordinateTransform::SlimeVrCoordinateTransform(GloveSide side)
    : side_(side)
{
    mountings_.fill(QQuaternion(1.0F, 0.0F, 0.0F, 0.0F));
}

void SlimeVrCoordinateTransform::setGloveSide(GloveSide side)
{
    side_ = side;
}

GloveSide SlimeVrCoordinateTransform::gloveSide() const
{
    return side_;
}

bool SlimeVrCoordinateTransform::setMounting(SensorId id, const QQuaternion &mounting)
{
    const std::optional<QQuaternion> sanitized = sanitizeMountingRotation(mounting);
    if (!sanitized) {
        return false;
    }
    mountings_[size_t(sensorIndex(id))] = *sanitized;
    return true;
}

QQuaternion SlimeVrCoordinateTransform::mounting(SensorId id) const
{
    return mountings_[size_t(sensorIndex(id))];
}

void SlimeVrCoordinateTransform::setMountings(const std::array<QQuaternion, 6> &mountings)
{
    for (int index = 0; index < 6; ++index) {
        const std::optional<QQuaternion> sanitized = sanitizeMountingRotation(mountings[size_t(index)]);
        mountings_[size_t(index)] = sanitized ? *sanitized : QQuaternion(1.0F, 0.0F, 0.0F, 0.0F);
    }
}

void SlimeVrCoordinateTransform::resetAll()
{
    mountings_.fill(QQuaternion(1.0F, 0.0F, 0.0F, 0.0F));
}

QQuaternion SlimeVrCoordinateTransform::transform(SensorId id, const QQuaternion &source) const
{
    const QQuaternion &mounting = mountings_[size_t(sensorIndex(id))];
    return mounting.conjugated() * source * mounting;
}
