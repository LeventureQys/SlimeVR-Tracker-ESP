#include "hand/hand_observation_solver.h"

#include "fusion/quaternion_util.h"
#include "hand/mount_calibration.h"
#include "hand/orientation_decomposition.h"

#include <algorithm>
#include <utility>

namespace handstudio {
namespace {

constexpr std::array<SensorId, 5> FingerSensors{SensorId::Thumb, SensorId::Index,
                                                SensorId::Middle, SensorId::Ring,
                                                SensorId::Pinky};

const FusedImuPose *findPose(const std::array<FusedImuPose, 6> &poses, SensorId sensorId)
{
    for (const auto &pose : poses) {
        if (pose.sensorId == sensorId) {
            return &pose;
        }
    }
    return nullptr;
}

bool usable(const FusedImuPose *pose)
{
    return pose && pose->valid && !pose->stale && normalizedQuaternion(pose->worldOrientation);
}

}

HandObservationSolver::HandObservationSolver()
{
    for (std::size_t index = 0; index < configs_.size(); ++index) {
        configs_[index].sensorId = FingerSensors[index];
    }
}

HandObservationSolver::HandObservationSolver(std::array<FingerObservationConfig, 5> configs,
                                             HandSide handSide)
    : configs_(std::move(configs)), handSide_(handSide)
{
}

HandObservationFrame HandObservationSolver::solve(const std::array<FusedImuPose, 6> &poses)
{
    const FusedImuPose *wrist = findPose(poses, SensorId::Wrist);
    if (!usable(wrist)) {
        if (hasLastFrame_) {
            return lastFrame_;
        }
        HandObservationFrame invalid;
        invalid.handSide = handSide_;
        return invalid;
    }

    HandObservationFrame frame;
    frame.sequence = wrist->sequence;
    frame.timestampNs = wrist->timestampNs;
    frame.handSide = handSide_;
    const QQuaternion wristWorld = wrist->worldOrientation.normalized();
    frame.wristWorldOrientation = hasNeutral_
        ? (neutralWrist_.conjugated() * wristWorld).normalized()
        : wristWorld;

    for (std::size_t index = 0; index < configs_.size(); ++index) {
        const auto &config = configs_[index];
        FingerObservation &output = frame.fingers[index];
        output.sensorId = config.sensorId;
        const FusedImuPose *finger = findPose(poses, config.sensorId);
        if (!usable(finger) || finger->sequence != wrist->sequence) {
            continue;
        }
        const QQuaternion relative = wristWorld.conjugated()
                                     * finger->worldOrientation.normalized();
        const auto corrected = applyMountCalibration(relative, config.mountOrientation);
        if (!corrected) {
            continue;
        }
        const QQuaternion fromNeutral = hasNeutral_
            ? (neutralFinger_[index].conjugated() * *corrected).normalized()
            : *corrected;
        const auto components = decomposeOrientation(fromNeutral, config.flexionAxis,
                                                     config.abductionAxis, config.twistAxis);
        if (!components) {
            continue;
        }
        output.worldOrientation = finger->worldOrientation.normalized();
        output.palmRelativeOrientation = relative.normalized();
        output.mountCorrectedOrientation = *corrected;
        output.flexionDegrees = components->flexionDegrees;
        output.abductionDegrees = components->abductionDegrees;
        output.twistDegrees = components->twistDegrees;
        output.valid = true;
        output.confidence = std::clamp(std::min(wrist->confidence, finger->confidence), 0.0F, 1.0F);
    }
    lastFrame_ = frame;
    hasLastFrame_ = true;
    return frame;
}

void HandObservationSolver::reset()
{
    lastFrame_ = {};
    hasLastFrame_ = false;
}

bool HandObservationSolver::setNeutral(const std::array<FusedImuPose, 6> &poses)
{
    const FusedImuPose *wrist = findPose(poses, SensorId::Wrist);
    if (!usable(wrist)) {
        return false;
    }
    const QQuaternion wristWorld = wrist->worldOrientation.normalized();
    std::array<QQuaternion, 5> fingerNeutrals;
    for (std::size_t index = 0; index < configs_.size(); ++index) {
        const auto &config = configs_[index];
        const FusedImuPose *finger = findPose(poses, config.sensorId);
        if (!usable(finger) || finger->sequence != wrist->sequence) {
            return false;
        }
        const QQuaternion relative = wristWorld.conjugated() * finger->worldOrientation.normalized();
        const auto corrected = applyMountCalibration(relative, config.mountOrientation);
        if (!corrected) {
            return false;
        }
        fingerNeutrals[index] = corrected->normalized();
    }
    neutralWrist_ = wristWorld;
    neutralFinger_ = fingerNeutrals;
    hasNeutral_ = true;
    return true;
}

void HandObservationSolver::clearNeutral()
{
    neutralFinger_ = {};
    neutralWrist_ = {};
    hasNeutral_ = false;
}

bool HandObservationSolver::hasNeutral() const noexcept
{
    return hasNeutral_;
}

}
