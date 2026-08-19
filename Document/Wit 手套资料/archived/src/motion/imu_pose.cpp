#include "imu_pose.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace handdemo::motion {

namespace {

QQuaternion projectedTwist(const QQuaternion &orientation, const QVector3D &axis)
{
    const QVector3D normalizedAxis = axis.normalized();
    const QVector3D vectorPart(orientation.x(), orientation.y(), orientation.z());
    const QVector3D projection = normalizedAxis * QVector3D::dotProduct(vectorPart, normalizedAxis);
    QQuaternion twist(orientation.scalar(), projection.x(), projection.y(), projection.z());
    return twist.lengthSquared() < 1.0e-12F ? QQuaternion() : twist.normalized();
}

}

ImuPoseMapper::ImuPoseMapper(const PoseSolver &solver)
    : solver_(solver), pose_(solver.bindPose())
{
}

void ImuPoseMapper::reset()
{
    pose_ = solver_.bindPose();
    lastTimestampUsec_ = -1;
    hasFingerPose_.fill(false);
}

std::optional<QQuaternion> ImuPoseMapper::normalizedOrientation(const ImuSample &sample)
{
    if (!sample.valid || sample.orientation.lengthSquared() < 1.0e-12F) {
        return std::nullopt;
    }
    return sample.orientation.normalized();
}

float ImuPoseMapper::signedTwistDegrees(const QQuaternion &orientation, const QVector3D &axis)
{
    const QVector3D normalizedAxis = axis.normalized();
    const QVector3D vectorPart(orientation.x(), orientation.y(), orientation.z());
    const QVector3D projection = normalizedAxis * QVector3D::dotProduct(vectorPart, normalizedAxis);
    QQuaternion twist(orientation.scalar(), projection.x(), projection.y(), projection.z());
    if (twist.lengthSquared() < 1.0e-12F) {
        return 0.0F;
    }
    twist.normalize();
    const float vectorLength = QVector3D(twist.x(), twist.y(), twist.z()).length();
    const float radians = 2.0F * std::atan2(vectorLength, std::abs(twist.scalar()));
    const float sign = QVector3D::dotProduct(QVector3D(twist.x(), twist.y(), twist.z()), normalizedAxis) < 0.0F
                           ? -1.0F : 1.0F;
    return qRadiansToDegrees(radians) * sign;
}

ImuMappingResult ImuPoseMapper::update(const HandImuFrame &frame)
{
    ImuMappingResult result;
    result.fingerValid = hasFingerPose_;
    const ImuSample &palmSample = frame.samples[static_cast<int>(ImuSlot::Palm)];
    if (palmSample.timestampUsec <= lastTimestampUsec_) {
        result.errors.push_back({"timestamp_regression", QStringLiteral("IMU 时间戳倒退"),
                                 QStringLiteral("slot=Palm timestamp=%1 previous=%2")
                                     .arg(palmSample.timestampUsec).arg(lastTimestampUsec_)});
        result.pose = solver_.solve(pose_, true);
        return result;
    }

    const auto palm = normalizedOrientation(palmSample);
    if (!palm) {
        result.errors.push_back({"invalid_orientation", QStringLiteral("掌心 IMU 姿态无效"),
                                 QStringLiteral("slot=Palm")});
        result.pose = solver_.solve(pose_, true);
        return result;
    }

    lastTimestampUsec_ = palmSample.timestampUsec;
    for (int fingerIndex = 0; fingerIndex < 5; ++fingerIndex) {
        const FingerConfig &finger = solver_.config().fingers[fingerIndex];
        const ImuSample &sample = frame.samples[static_cast<int>(finger.slot)];
        const auto fingerOrientation = normalizedOrientation(sample);
        if (!fingerOrientation) {
            result.errors.push_back({"invalid_orientation", QStringLiteral("手指 IMU 姿态无效"),
                                     QStringLiteral("slot=%1").arg(finger.name)});
            continue;
        }
        const QQuaternion relative = palm->conjugated() * *fingerOrientation;
        const QQuaternion corrected = finger.sensorCorrection * relative
                                      * finger.sensorCorrection.conjugated();
        const QQuaternion normalizedCorrected = corrected.normalized();
        const QQuaternion flexionTwist = projectedTwist(normalizedCorrected, finger.sensorFlexionAxis);
        const float degrees = signedTwistDegrees(flexionTwist, finger.sensorFlexionAxis);
        const QQuaternion remaining = normalizedCorrected * flexionTwist.conjugated();
        const float abductionDegrees = std::clamp(
            signedTwistDegrees(remaining.normalized(), finger.sensorAbductionAxis),
            finger.sensorAbductionMinDegrees, finger.sensorAbductionMaxDegrees);
        const float curl = std::clamp((degrees - finger.sensorMinDegrees)
                                         / (finger.sensorMaxDegrees - finger.sensorMinDegrees),
                                     0.0F, 1.0F);
        pose_ = solver_.applyFingerPose(pose_, fingerIndex, curl, abductionDegrees).pose;
        hasFingerPose_[fingerIndex] = true;
        result.fingerValid[fingerIndex] = true;
    }
    result.frameApplied = true;
    result.pose = solver_.solve(pose_, true);
    return result;
}

DeterministicHandPoseSource::DeterministicHandPoseSource(qint64 startTimestampUsec)
    : timestampUsec_(startTimestampUsec)
{
}

std::optional<HandImuFrame> DeterministicHandPoseSource::poll()
{
    HandImuFrame frame;
    const float phase = static_cast<float>(frameIndex_ % 120U) / 119.0F;
    const float triangle = phase <= 0.5F ? phase * 2.0F : (1.0F - phase) * 2.0F;
    for (int slotIndex = 0; slotIndex < 6; ++slotIndex) {
        const ImuSlot slot = static_cast<ImuSlot>(slotIndex);
        const float angle = slot == ImuSlot::Palm ? 0.0F : triangle * (35.0F + slotIndex * 8.0F);
        frame.samples[slotIndex] = {slot, QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, angle),
                                    timestampUsec_, valid_[slotIndex]};
    }
    ++frameIndex_;
    timestampUsec_ += 16667;
    return frame;
}

void DeterministicHandPoseSource::setValid(ImuSlot slot, bool valid)
{
    valid_[static_cast<int>(slot)] = valid;
}

void DeterministicHandPoseSource::reset(qint64 startTimestampUsec)
{
    timestampUsec_ = startTimestampUsec;
    frameIndex_ = 0;
    valid_.fill(true);
}

}
