#pragma once

#include "fusion/ifusion_filter.h"

#include <QVector3D>

#include <memory>

namespace handstudio {

// Wraps the vendored VQF algorithm (MIT, Daniel Laidig) behind IFusionFilter.
// The VQF internal types are confined to the pimpl and never leak into this
// public header or the core contract.
class VqfFusionFilter final : public IFusionFilter {
public:
    explicit VqfFusionFilter(double sampleRateHz = 200.0);
    ~VqfFusionFilter() override;

    void reset() override;
    FusedImuPose update(const CalibratedImuSample &sample, double dtSeconds) override;

    bool isAvailable() const;
    bool restDetected() const;
    QVector3D biasEstimateRadPerSec() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
