#pragma once

#include "rig_config.h"

namespace handdemo::motion {

class PoseSolver {
public:
    PoseSolver(SkeletonBinding binding, RigConfig config);

    SkeletonPose bindPose() const;
    PoseResult solve(const SkeletonPose &requested, bool coupledApproximation = false) const;
    PoseResult applyJoint(const SkeletonPose &base, const QString &boneName,
                          const QVector3D &degrees) const;
    PoseResult applyFingerCurl(const SkeletonPose &base, int fingerIndex, float curl) const;
    PoseResult applyFingerPose(const SkeletonPose &base, int fingerIndex, float curl,
                               float abductionDegrees) const;
    float chainFlexionCapacity(int fingerIndex) const;

    const RigConfig &config() const { return config_; }
    const SkeletonBinding &binding() const { return binding_; }

private:
    int boneIndex(const QString &name) const;

    SkeletonBinding binding_;
    RigConfig config_;
};

}
