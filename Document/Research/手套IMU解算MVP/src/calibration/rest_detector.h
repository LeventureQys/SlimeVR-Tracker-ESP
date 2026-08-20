#pragma once

#include <QString>
#include <QVector3D>

namespace handstudio {

// Stationary detection: accelerometer stability + angular velocity threshold.
// Rest is declared only after the combined condition holds for a minimum
// duration, which makes the detector robust against short transients.
class RestDetector {
public:
    struct Config {
        double gyroNormThresholdRadPerSec = 0.05;
        double accelNormToleranceMps2 = 0.20;
        double accelVarianceThresholdMps2Sq = 0.06;
        double minRestDurationSeconds = 0.5;
        double filterTauSeconds = 0.5;
        double gravityMagnitudeMps2 = 9.80665;

        bool isValid(QString *reason = nullptr) const;
    };

    explicit RestDetector(const Config &config = {});
    void reset();
    void update(const QVector3D &accelerationMps2, const QVector3D &gyroscopeRadPerSec, double dtSeconds);
    bool isRest() const;
    double restDurationSeconds() const;
    void setConfig(const Config &config);
    Config config() const;

private:
    Config config_;
    QVector3D accelMean_;
    double accelVariance_ = 0.0;
    double restDuration_ = 0.0;
    bool rest_ = false;
    bool warm_ = false;
};

}
