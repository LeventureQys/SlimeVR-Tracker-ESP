#pragma once

#include "core/calibrated_types.h"
#include "core/fusion_types.h"

namespace handstudio {

// Algorithm replacement boundary. Madgwick and VQF both implement this
// interface and produce the same FusedImuPose type. dt is computed by the
// caller from monotonic timestamps and is already fallback-corrected.
class IFusionFilter {
public:
    virtual ~IFusionFilter() = default;
    virtual void reset() = 0;
    virtual FusedImuPose update(const CalibratedImuSample &sample, double dtSeconds) = 0;
};

}
