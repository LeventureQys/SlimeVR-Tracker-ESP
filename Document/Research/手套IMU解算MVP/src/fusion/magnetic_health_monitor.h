#pragma once

#include "core/fusion_types.h"

#include <QString>
#include <QVector3D>

namespace handstudio {

// Hysteresis state machine for magnetometer health:
// Unavailable / Healthy / Disturbed / Recovering. Entering Disturbed requires
// consecutive abnormal samples; recovery requires consecutive normal samples.
// Disturbed drives fusion down to SixD; Recovering limits heading correction.
class MagneticHealthMonitor {
public:
    struct Config {
        bool enabled = true;
        double minNormMicroTesla = 1.0;
        double referenceNormMicroTesla = 0.0;  // 0 = learn from first valid sample
        double toleranceRatio = 0.3;
        double referenceAdaptGain = 0.05;
        int disturbSamples = 5;
        int recoverSamples = 10;
        int healthySamples = 20;

        bool isValid(QString *reason = nullptr) const;
    };

    explicit MagneticHealthMonitor(const Config &config = {});
    void reset();
    void setConfig(const Config &config);
    Config config() const;

    MagneticHealth update(const QVector3D &magneticMicroTesla);
    MagneticHealth state() const;
    int abnormalCount() const;
    int normalCount() const;

private:
    Config config_;
    MagneticHealth state_ = MagneticHealth::Unavailable;
    double referenceNorm_ = 0.0;
    int abnormalCount_ = 0;
    int normalCount_ = 0;
};

}
