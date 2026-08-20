#pragma once

#include <QQuaternion>

#include <optional>

namespace handstudio {

std::optional<QQuaternion> applyMountCalibration(const QQuaternion &relativeOrientation,
                                                 const QQuaternion &mountOrientation);

}
