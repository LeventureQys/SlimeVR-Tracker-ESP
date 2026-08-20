#include "hand/mount_calibration.h"

#include "fusion/quaternion_util.h"

namespace handstudio {

std::optional<QQuaternion> applyMountCalibration(const QQuaternion &relativeOrientation,
                                                 const QQuaternion &mountOrientation)
{
    const auto relative = normalizedQuaternion(relativeOrientation);
    const auto mount = normalizedQuaternion(mountOrientation);
    if (!relative || !mount) {
        return std::nullopt;
    }
    return normalizedQuaternion((*mount) * (*relative) * mount->conjugated());
}

}
