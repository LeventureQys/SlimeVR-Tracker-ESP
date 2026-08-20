#pragma once

#include "six_imu_solver.h"

#include <imu_pose.h>

namespace HandSkeleton {

class SixImuSnapshotAdapter final {
public:
    static handdemo::motion::HandImuFrame adapt(const SixImuSnapshot &snapshot);
};

} // namespace HandSkeleton
