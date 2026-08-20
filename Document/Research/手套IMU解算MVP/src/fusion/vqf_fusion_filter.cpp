#include "vqf_fusion_filter.h"

#include "vqf/vqf.h"

#include <cmath>

namespace handstudio {

struct VqfFusionFilter::Impl {
    VQF vqf;
    bool restDetected = false;
    QVector3D biasEstimate;

    explicit Impl(double sampleRateHz)
        : vqf(float(1.0 / (sampleRateHz > 0.0 ? sampleRateHz : 200.0)))
    {
    }
};

namespace {

bool finiteVector(const QVector3D &value)
{
    return std::isfinite(double(value.x())) && std::isfinite(double(value.y()))
        && std::isfinite(double(value.z()));
}

double vectorNorm(const QVector3D &value)
{
    return std::sqrt(double(value.x()) * value.x() + double(value.y()) * value.y()
                     + double(value.z()) * value.z());
}

}

VqfFusionFilter::VqfFusionFilter(double sampleRateHz)
    : impl_(std::make_unique<Impl>(sampleRateHz))
{
}

VqfFusionFilter::~VqfFusionFilter() = default;

void VqfFusionFilter::reset()
{
    impl_->vqf.resetState();
    impl_->restDetected = false;
    impl_->biasEstimate = QVector3D();
}

bool VqfFusionFilter::isAvailable() const
{
    return true;
}

bool VqfFusionFilter::restDetected() const
{
    return impl_->restDetected;
}

QVector3D VqfFusionFilter::biasEstimateRadPerSec() const
{
    return impl_->biasEstimate;
}

FusedImuPose VqfFusionFilter::update(const CalibratedImuSample &sample, double dtSeconds)
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
    if (!finiteVector(sample.gyroscopeRadPerSec) || !finiteVector(sample.accelerationMps2)
        || !std::isfinite(dtSeconds) || dtSeconds <= 0.0 || dtSeconds > 0.1) {
        pose.valid = false;
        pose.mode = FusionMode::Invalid;
        return pose;
    }

    const float gyroscope[3] = {float(sample.gyroscopeRadPerSec.x()), float(sample.gyroscopeRadPerSec.y()),
                                float(sample.gyroscopeRadPerSec.z())};
    const float acceleration[3] = {float(sample.accelerationMps2.x()), float(sample.accelerationMps2.y()),
                                   float(sample.accelerationMps2.z())};

    impl_->vqf.updateGyr(gyroscope, float(dtSeconds));
    impl_->vqf.updateAcc(acceleration);

    const QVector3D &mag = sample.magneticMicroTesla;
    const bool magneticUsable = finiteVector(mag) && vectorNorm(mag) > 0.0f;

    if (magneticUsable) {
        const float magnetic[3] = {float(mag.x()), float(mag.y()), float(mag.z())};
        impl_->vqf.updateMag(magnetic);
        float quaternion[4];
        impl_->vqf.getQuat9D(quaternion);
        pose.worldOrientation = QQuaternion(quaternion[0], quaternion[1], quaternion[2], quaternion[3]);
        pose.mode = FusionMode::NineD;
    } else {
        float quaternion[4];
        impl_->vqf.getQuat6D(quaternion);
        pose.worldOrientation = QQuaternion(quaternion[0], quaternion[1], quaternion[2], quaternion[3]);
        pose.mode = FusionMode::SixD;
    }

    impl_->restDetected = impl_->vqf.getRestDetected();
    float bias[3] = {0.0f, 0.0f, 0.0f};
    impl_->vqf.getBiasEstimate(bias);
    impl_->biasEstimate = QVector3D(bias[0], bias[1], bias[2]);

    pose.valid = true;
    return pose;
}

}
