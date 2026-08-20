#include "hand/orientation_decomposition.h"

#include "fusion/quaternion_util.h"

#include <QtMath>

#include <cmath>

namespace handstudio {
namespace {

std::optional<float> signedAngleDegrees(const QQuaternion &orientation, const QVector3D &axis)
{
    if (!isFiniteQuaternion(orientation) || !std::isfinite(double(axis.x()))
        || !std::isfinite(double(axis.y())) || !std::isfinite(double(axis.z()))
        || axis.lengthSquared() < 1.0e-8F) {
        return std::nullopt;
    }
    const QVector3D unitAxis = axis.normalized();
    const QVector3D vectorPart(orientation.x(), orientation.y(), orientation.z());
    return qRadiansToDegrees(2.0F * std::atan2(QVector3D::dotProduct(vectorPart, unitAxis),
                                               orientation.scalar()));
}

}

std::optional<OrientationComponents> decomposeOrientation(
    const QQuaternion &orientation, const QVector3D &flexionAxis,
    const QVector3D &abductionAxis, const QVector3D &twistAxis)
{
    const auto normalized = normalizedQuaternion(orientation);
    if (!normalized) {
        return std::nullopt;
    }
    const auto flexion = signedAngleDegrees(*normalized, flexionAxis);
    const auto abduction = signedAngleDegrees(*normalized, abductionAxis);
    const auto twist = signedAngleDegrees(*normalized, twistAxis);
    if (!flexion || !abduction || !twist) {
        return std::nullopt;
    }
    OrientationComponents result{*flexion, *abduction, *twist};
    if (!std::isfinite(result.flexionDegrees) || !std::isfinite(result.abductionDegrees)
        || !std::isfinite(result.twistDegrees)) {
        return std::nullopt;
    }
    return result;
}

}
