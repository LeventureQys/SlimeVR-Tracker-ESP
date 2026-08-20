#include "hand_skeleton_adapter.h"

#include <cmath>

namespace {

bool finiteQuaternion(const QQuaternion &orientation)
{
    return std::isfinite(double(orientation.scalar()))
        && std::isfinite(double(orientation.x()))
        && std::isfinite(double(orientation.y()))
        && std::isfinite(double(orientation.z()));
}

handdemo::motion::ImuSlot slotForIndex(int index)
{
    return static_cast<handdemo::motion::ImuSlot>(index);
}

} // namespace

namespace HandSkeleton {

handdemo::motion::HandImuFrame SixImuSnapshotAdapter::adapt(const SixImuSnapshot &snapshot)
{
    handdemo::motion::HandImuFrame frame;
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        const SensorPose &pose = snapshot.poses[size_t(index)];
        handdemo::motion::ImuSample &sample = frame.samples[size_t(index)];
        sample.slot = slotForIndex(index);
        sample.timestampUsec = pose.updatedMonotonicNs > 0
            ? pose.updatedMonotonicNs / 1000
            : snapshot.updatedMonotonicNs / 1000;
        sample.valid = pose.valid && finiteQuaternion(pose.worldOrientation)
            && pose.worldOrientation.lengthSquared() >= 1.0e-12F;
        sample.orientation = sample.valid
            ? pose.worldOrientation.normalized()
            : QQuaternion(0.0F, 0.0F, 0.0F, 0.0F);
    }
    return frame;
}

} // namespace HandSkeleton
