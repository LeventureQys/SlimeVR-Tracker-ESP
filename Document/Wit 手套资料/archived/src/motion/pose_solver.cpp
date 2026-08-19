#include "pose_solver.h"

#include <QtMath>

#include <algorithm>

namespace handdemo::motion {

namespace {

float flexionEndpointDegrees(const JointConfig &joint)
{
    int dominantAxis = 0;
    for (int axis = 1; axis < 3; ++axis) {
        if (qAbs(joint.flexionAxis[axis]) > qAbs(joint.flexionAxis[dominantAxis])) {
            dominantAxis = axis;
        }
    }
    const float component = joint.flexionAxis[dominantAxis];
    if (qAbs(component) < 1.0e-6F) {
        return 0.0F;
    }
    return component > 0.0F ? joint.limits.maxDegrees[dominantAxis] / component
                            : joint.limits.minDegrees[dominantAxis] / component;
}

}

PoseSolver::PoseSolver(SkeletonBinding binding, RigConfig config)
    : binding_(std::move(binding)), config_(std::move(config))
{
}

SkeletonPose PoseSolver::bindPose() const
{
    SkeletonPose pose;
    pose.localPoses.resize(binding_.bones.size());
    return pose;
}

int PoseSolver::boneIndex(const QString &name) const
{
    for (int index = 0; index < binding_.bones.size(); ++index) {
        if (binding_.bones[index].name == name) {
            return index;
        }
    }
    return -1;
}

PoseResult PoseSolver::solve(const SkeletonPose &requested, bool coupledApproximation) const
{
    PoseResult result;
    result.pose = bindPose();
    result.joints.resize(binding_.bones.size());
    result.globalMatrices.resize(binding_.bones.size());
    result.skinMatrices.resize(binding_.bones.size());
    result.coupledApproximation = coupledApproximation;

    for (int index = 0; index < binding_.bones.size(); ++index) {
        QVector3D requestedDegrees;
        if (index < requested.localPoses.size()) {
            requestedDegrees = requested.localPoses[index].eulerDegrees;
        }
        QVector3D appliedDegrees;
        const auto configIterator = config_.joints.constFind(binding_.bones[index].name);
        if (configIterator != config_.joints.constEnd()) {
            for (int axis = 0; axis < 3; ++axis) {
                if (configIterator->lockedAxes[axis] >= 0.5F) {
                    appliedDegrees[axis] = 0.0F;
                } else {
                    appliedDegrees[axis] = std::clamp(requestedDegrees[axis],
                                                       configIterator->limits.minDegrees[axis],
                                                       configIterator->limits.maxDegrees[axis]);
                }
            }
        }
        result.pose.localPoses[index].eulerDegrees = appliedDegrees;
        result.joints[index] = {requestedDegrees != appliedDegrees, requestedDegrees, appliedDegrees};

        QMatrix4x4 local = binding_.bones[index].bindLocal;
        local.rotate(QQuaternion::fromEulerAngles(appliedDegrees));
        const int parentIndex = binding_.bones[index].parentIndex;
        result.globalMatrices[index] = parentIndex >= 0 ? result.globalMatrices[parentIndex] * local : local;
        result.skinMatrices[index] = binding_.globalInverse * result.globalMatrices[index]
                                     * binding_.bones[index].inverseBind;
    }
    return result;
}

PoseResult PoseSolver::applyJoint(const SkeletonPose &base, const QString &boneName,
                                  const QVector3D &degrees) const
{
    SkeletonPose requested = base;
    if (requested.localPoses.size() != binding_.bones.size()) {
        requested = bindPose();
    }
    const int index = boneIndex(boneName);
    if (index >= 0) {
        requested.localPoses[index].eulerDegrees = degrees;
    }
    return solve(requested);
}

PoseResult PoseSolver::applyFingerCurl(const SkeletonPose &base, int fingerIndex, float curl) const
{
    return applyFingerPose(base, fingerIndex, curl, 0.0F);
}

float PoseSolver::chainFlexionCapacity(int fingerIndex) const
{
    if (fingerIndex < 0 || fingerIndex >= static_cast<int>(config_.fingers.size())) {
        return 0.0F;
    }
    float capacity = 0.0F;
    for (const QString &boneName : config_.fingers[fingerIndex].bones) {
        const JointConfig &joint = config_.joints[boneName];
        capacity += joint.coupling * flexionEndpointDegrees(joint);
    }
    return capacity;
}

PoseResult PoseSolver::applyFingerPose(const SkeletonPose &base, int fingerIndex, float curl,
                                       float abductionDegrees) const
{
    SkeletonPose requested = base;
    if (requested.localPoses.size() != binding_.bones.size()) {
        requested = bindPose();
    }
    if (fingerIndex < 0 || fingerIndex >= static_cast<int>(config_.fingers.size())) {
        return solve(requested, true);
    }
    const float constrainedCurl = std::clamp(curl, 0.0F, 1.0F);
    bool firstJoint = true;
    for (const QString &boneName : config_.fingers[fingerIndex].bones) {
        const int index = boneIndex(boneName);
        const JointConfig &joint = config_.joints[boneName];
        const float flexionDegrees = constrainedCurl * joint.coupling
                                     * flexionEndpointDegrees(joint);
        requested.localPoses[index].eulerDegrees = joint.flexionAxis * flexionDegrees;
        if (firstJoint) {
            requested.localPoses[index].eulerDegrees += joint.abductionAxis * abductionDegrees;
        }
        firstJoint = false;
    }
    return solve(requested, true);
}

}
