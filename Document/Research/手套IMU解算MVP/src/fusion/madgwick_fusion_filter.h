#pragma once

#include "fusion/ifusion_filter.h"

#include <memory>

namespace handstudio {

// Wraps the legacy non-QObject Madgwick math core behind IFusionFilter. The
// legacy core type is kept out of this header via pimpl so it does not leak
// into callers.
class MadgwickFusionFilter final : public IFusionFilter {
public:
    explicit MadgwickFusionFilter(double beta = 0.10);
    ~MadgwickFusionFilter() override;

    void reset() override;
    FusedImuPose update(const CalibratedImuSample &sample, double dtSeconds) override;

    void setBeta(double beta);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
