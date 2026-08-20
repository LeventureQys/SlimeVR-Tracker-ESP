#pragma once

#include "rest_detector.h"

#include <QString>
#include <QVector3D>

#include <QtGlobal>

namespace handstudio {

// Startup stationary gyroscope bias estimator. Accumulates gyroscope samples
// only while the sensor is at rest; during motion the current estimate is
// frozen (no reset, no update).
class StaticGyroBiasEstimator {
public:
    struct Config {
        int minRestSamples = 50;
        double maxBiasRadPerSec = 0.5;
        RestDetector::Config rest;

        bool isValid(QString *reason = nullptr) const;
    };

    explicit StaticGyroBiasEstimator(const Config &config = {});
    void reset();
    void update(const QVector3D &accelerationMps2, const QVector3D &gyroscopeRadPerSec, double dtSeconds);
    QVector3D bias() const;
    bool isRest() const;
    bool converged() const;
    qint64 restSampleCount() const;
    void setConfig(const Config &config);
    Config config() const;

private:
    Config config_;
    RestDetector restDetector_;
    QVector3D bias_;
    qint64 restSamples_ = 0;
    bool initialized_ = false;
};

}
