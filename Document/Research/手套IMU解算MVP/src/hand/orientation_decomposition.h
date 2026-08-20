#pragma once

#include <QQuaternion>
#include <QVector3D>

#include <optional>

namespace handstudio {

struct OrientationComponents {
    float flexionDegrees = 0.0F;
    float abductionDegrees = 0.0F;
    float twistDegrees = 0.0F;
};

std::optional<OrientationComponents> decomposeOrientation(
    const QQuaternion &orientation, const QVector3D &flexionAxis,
    const QVector3D &abductionAxis, const QVector3D &twistAxis);

}
