#include "slimevr_pose_adapter.h"

#include "slimevr_sensor_mapping.h"

#include <cmath>

namespace {

constexpr double MinimumQuaternionNorm = 1.0e-12;
constexpr double MaximumNormError = 1.0e-4;

bool finiteQuaternion(const QQuaternion &q)
{
    return std::isfinite(double(q.scalar())) && std::isfinite(double(q.x()))
        && std::isfinite(double(q.y())) && std::isfinite(double(q.z()));
}

std::optional<QQuaternion> normalized(const QQuaternion &q)
{
    const double squaredNorm = double(q.scalar()) * q.scalar() + double(q.x()) * q.x()
        + double(q.y()) * q.y() + double(q.z()) * q.z();
    if (!std::isfinite(squaredNorm) || squaredNorm < MinimumQuaternionNorm * MinimumQuaternionNorm) {
        return std::nullopt;
    }
    const double norm = std::sqrt(squaredNorm);
    if (std::abs(norm - 1.0) > MaximumNormError) {
        return std::nullopt;
    }
    return q;
}

} // namespace

SlimeVrPoseAdapter::SlimeVrPoseAdapter(GloveSide side)
    : side_(side)
    , transform_(side)
{
}

void SlimeVrPoseAdapter::setGloveSide(GloveSide side)
{
    side_ = side;
    transform_.setGloveSide(side);
}

GloveSide SlimeVrPoseAdapter::gloveSide() const
{
    return side_;
}

bool SlimeVrPoseAdapter::setMounting(SensorId id, const QQuaternion &mounting)
{
    return transform_.setMounting(id, mounting);
}

void SlimeVrPoseAdapter::setMountings(const std::array<QQuaternion, 6> &mountings)
{
    transform_.setMountings(mountings);
}

QQuaternion SlimeVrPoseAdapter::mounting(SensorId id) const
{
    return transform_.mounting(id);
}

std::array<SlimeVrPoseSample, 6> SlimeVrPoseAdapter::adapt(const SixImuSnapshot &snapshot) const
{
    const auto descriptors = SlimeVrSensorMapping::descriptors(side_);
    std::array<SlimeVrPoseSample, 6> samples{};
    for (int index = 0; index < 6; ++index) {
        const SlimeVrSensorDescriptor &descriptor = descriptors[size_t(index)];
        const SensorPose &pose = snapshot.poses[size_t(index)];
        SlimeVrPoseSample &sample = samples[size_t(index)];
        sample.sensorId = descriptor.sensorId;
        sample.updatedMonotonicNs = pose.updatedMonotonicNs;
        if (!pose.valid || !finiteQuaternion(pose.worldOrientation)) {
            continue;
        }
        const QQuaternion nodeOrientation = transform_.transform(descriptor.sourceId, pose.worldOrientation);
        const std::optional<QQuaternion> valid = normalized(nodeOrientation);
        if (!valid) {
            continue;
        }
        sample.orientation = *valid;
        sample.valid = true;
    }
    return samples;
}
