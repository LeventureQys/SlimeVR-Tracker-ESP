#pragma once

#include "hand_skeleton_adapter.h"
#include "hand_skeleton_types.h"

#include <pose_solver.h>

#include <memory>

namespace HandSkeleton {

class Pipeline final {
public:
    Pipeline(handdemo::motion::SkeletonBinding binding, handdemo::motion::RigConfig config);

    HandSkeletonFrame process(const SixImuSnapshot &snapshot);
    void reset();

    const handdemo::motion::SkeletonBinding &binding() const;
    const handdemo::motion::RigConfig &config() const;

private:
    HandSkeletonFrame buildFrame(
        const SixImuSnapshot &snapshot,
        const handdemo::motion::ImuMappingResult &mapping) const;

    handdemo::motion::SkeletonBinding binding_;
    handdemo::motion::PoseSolver solver_;
    std::unique_ptr<handdemo::motion::ImuPoseMapper> mapper_;
};

} // namespace HandSkeleton
