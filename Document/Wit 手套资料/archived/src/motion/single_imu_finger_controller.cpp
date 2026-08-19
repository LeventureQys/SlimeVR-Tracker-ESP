#include "single_imu_finger_controller.h"

#include "imu_pose.h"

#include <algorithm>

namespace handdemo::motion {

SingleImuFingerController::SingleImuFingerController(const PoseSolver &solver)
    : solver_(solver)
{
    output_.pose = solver_.solve(solver_.bindPose(), true);
}

void SingleImuFingerController::setConnected(bool connected)
{
    if (!connected) {
        output_.fingerIndex = -1;
        resetToState(SingleImuDriveState::Disconnected);
        return;
    }
    if (output_.state == SingleImuDriveState::Disconnected || output_.state == SingleImuDriveState::Error) {
        resetToState(SingleImuDriveState::ConnectedUnbound);
    }
}

void SingleImuFingerController::bindFinger(int fingerIndex)
{
    if (output_.state == SingleImuDriveState::Disconnected) {
        output_.errors = {{"not_connected", QStringLiteral("IMU 尚未连接"), QStringLiteral("bindFinger")}};
        return;
    }
    if (fingerIndex < 0 || fingerIndex >= static_cast<int>(solver_.config().fingers.size())) {
        output_.fingerIndex = -1;
        resetToState(SingleImuDriveState::ConnectedUnbound);
        output_.errors = {{"invalid_finger", QStringLiteral("绑定手指无效"), QString::number(fingerIndex)}};
        return;
    }
    output_.fingerIndex = fingerIndex;
    resetToState(SingleImuDriveState::BoundUncalibrated);
}

bool SingleImuFingerController::calibrate(const QQuaternion &rawOrientation, qint64 timestampUsec)
{
    if (output_.fingerIndex < 0 || output_.state == SingleImuDriveState::Disconnected) {
        output_.errors = {{"not_bound", QStringLiteral("请先连接 IMU 并选择手指"), QStringLiteral("calibrate")}};
        return false;
    }
    const auto orientation = normalized(rawOrientation);
    if (!orientation) {
        output_.errors = {{"invalid_orientation", QStringLiteral("IMU 姿态无效，无法校准"), QStringLiteral("zero quaternion")}};
        return false;
    }
    zeroOrientation_ = orientation;
    lastTimestampUsec_ = timestampUsec - 1;
    output_.state = SingleImuDriveState::Ready;
    output_.pose = solver_.solve(solver_.bindPose(), true);
    output_.flexionDegrees = 0.0F;
    output_.abductionDegrees = 0.0F;
    output_.curl = 0.0F;
    output_.frameApplied = false;
    output_.errors.clear();
    return true;
}

bool SingleImuFingerController::setDriving(bool enabled)
{
    if (!enabled) {
        if (zeroOrientation_) {
            output_.state = SingleImuDriveState::Ready;
        }
        return true;
    }
    if (output_.state != SingleImuDriveState::Ready || !zeroOrientation_) {
        output_.errors = {{"not_calibrated", QStringLiteral("请先完成临时校准"), QStringLiteral("setDriving")}};
        return false;
    }
    output_.state = SingleImuDriveState::Driving;
    output_.errors.clear();
    return true;
}

SingleImuMappingOutput SingleImuFingerController::update(const QQuaternion &rawOrientation, qint64 timestampUsec)
{
    output_.frameApplied = false;
    output_.errors.clear();
    if (output_.state != SingleImuDriveState::Driving || !zeroOrientation_ || output_.fingerIndex < 0) {
        return output_;
    }
    if (timestampUsec <= lastTimestampUsec_) {
        output_.errors = {{"timestamp_regression", QStringLiteral("IMU 时间戳未递增"),
                           QStringLiteral("current=%1 previous=%2").arg(timestampUsec).arg(lastTimestampUsec_)}};
        return output_;
    }
    const auto orientation = normalized(rawOrientation);
    if (!orientation) {
        output_.errors = {{"invalid_orientation", QStringLiteral("IMU 姿态无效"), QStringLiteral("update")}};
        return output_;
    }
    const FingerConfig &finger = solver_.config().fingers[output_.fingerIndex];
    const QQuaternion relative = zeroOrientation_->conjugated() * *orientation;
    const QQuaternion corrected = (finger.sensorCorrection * relative
                                    * finger.sensorCorrection.conjugated()).normalized();
    const float flexion = ImuPoseMapper::signedTwistDegrees(corrected, finger.sensorFlexionAxis);
    const QQuaternion flexionRotation = QQuaternion::fromAxisAndAngle(finger.sensorFlexionAxis, flexion);
    const QQuaternion remaining = (corrected * flexionRotation.conjugated()).normalized();
    const float safeAbductionMax = finger.sensorAbductionMaxDegrees * 0.7F;
    const float safeAbductionMin = finger.sensorAbductionMinDegrees * 0.7F;
    const float abduction = std::clamp(ImuPoseMapper::signedTwistDegrees(remaining, finger.sensorAbductionAxis),
                                       safeAbductionMin, safeAbductionMax);
    const float capacity = solver_.chainFlexionCapacity(output_.fingerIndex);
    const float effectiveFlexion = std::max(0.0F, flexion - finger.sensorMinDegrees);
    const float curl = capacity > 1.0e-6F
        ? std::clamp(effectiveFlexion / capacity, 0.0F, 1.0F) : 0.0F;
    output_.pose = solver_.applyFingerPose(solver_.bindPose(), output_.fingerIndex, curl, abduction);
    output_.flexionDegrees = flexion;
    output_.abductionDegrees = abduction;
    output_.curl = curl;
    output_.frameApplied = true;
    lastTimestampUsec_ = timestampUsec;
    return output_;
}

void SingleImuFingerController::resetCalibration()
{
    if (output_.state == SingleImuDriveState::Disconnected) {
        return;
    }
    resetToState(output_.fingerIndex >= 0 ? SingleImuDriveState::BoundUncalibrated
                                          : SingleImuDriveState::ConnectedUnbound);
}

SingleImuDriveState SingleImuFingerController::state() const
{
    return output_.state;
}

const SingleImuMappingOutput &SingleImuFingerController::output() const
{
    return output_;
}

void SingleImuFingerController::resetToState(SingleImuDriveState state)
{
    zeroOrientation_.reset();
    lastTimestampUsec_ = -1;
    output_.state = state;
    output_.pose = solver_.solve(solver_.bindPose(), true);
    output_.flexionDegrees = 0.0F;
    output_.abductionDegrees = 0.0F;
    output_.curl = 0.0F;
    output_.frameApplied = false;
    output_.errors.clear();
}

std::optional<QQuaternion> SingleImuFingerController::normalized(const QQuaternion &orientation)
{
    if (!qIsFinite(orientation.scalar()) || !qIsFinite(orientation.x()) || !qIsFinite(orientation.y())
        || !qIsFinite(orientation.z()) || orientation.lengthSquared() < 1.0e-12F) {
        return std::nullopt;
    }
    return orientation.normalized();
}

}
