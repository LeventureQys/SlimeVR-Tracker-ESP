#include "madgwick_fusion_filter.h"

#include "madgwick_filter.h"

#include <cmath>
#include <optional>

namespace handstudio {
namespace {

FusionMode mapMode(::FusionMode mode)
{
    switch (mode) {
    case ::FusionMode::SixAxis:
        return FusionMode::SixD;
    case ::FusionMode::NineAxis:
        return FusionMode::NineD;
    case ::FusionMode::Invalid:
        return FusionMode::Invalid;
    }
    return FusionMode::Invalid;
}

Vector3d toVector(const QVector3D &value)
{
    return {double(value.x()), double(value.y()), double(value.z())};
}

}

struct MadgwickFusionFilter::Impl {
    MadgwickFilter core;
    explicit Impl(double beta)
        : core(beta)
    {
    }
};

MadgwickFusionFilter::MadgwickFusionFilter(double beta)
    : impl_(std::make_unique<Impl>(beta))
{
}

MadgwickFusionFilter::~MadgwickFusionFilter() = default;

void MadgwickFusionFilter::reset()
{
    impl_->core.reset();
}

void MadgwickFusionFilter::setBeta(double beta)
{
    impl_->core.setBeta(beta);
}

FusedImuPose MadgwickFusionFilter::update(const CalibratedImuSample &sample, double dtSeconds)
{
    FusedImuPose pose;
    pose.sensorId = sample.sensorId;
    pose.sequence = sample.sequence;
    pose.timestampNs = sample.timestampNs;
    pose.calibrationState = sample.calibrationState;

    if (!sample.valid) {
        pose.valid = false;
        pose.mode = FusionMode::Invalid;
        return pose;
    }

    const Vector3d gyroscope = toVector(sample.gyroscopeRadPerSec);
    const Vector3d acceleration = toVector(sample.accelerationMps2);
    std::optional<Vector3d> magnetic;
    const QVector3D &mag = sample.magneticMicroTesla;
    if (std::isfinite(double(mag.x())) && std::isfinite(double(mag.y())) && std::isfinite(double(mag.z()))) {
        magnetic = Vector3d{double(mag.x()), double(mag.y()), double(mag.z())};
    }

    if (!impl_->core.update(gyroscope, acceleration, magnetic, dtSeconds)) {
        pose.valid = false;
        pose.mode = FusionMode::Invalid;
        return pose;
    }

    pose.worldOrientation = impl_->core.quaternion();
    pose.mode = mapMode(impl_->core.mode());
    pose.valid = true;
    return pose;
}

}
