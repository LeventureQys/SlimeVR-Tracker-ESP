#include "rest_detector.h"

#include <cmath>

namespace handstudio {
namespace {

double norm(const QVector3D &value)
{
    return std::sqrt(double(value.x()) * value.x() + double(value.y()) * value.y()
                     + double(value.z()) * value.z());
}

bool finite(const QVector3D &value)
{
    return std::isfinite(double(value.x())) && std::isfinite(double(value.y()))
        && std::isfinite(double(value.z()));
}

}

bool RestDetector::Config::isValid(QString *reason) const
{
    if (!(gyroNormThresholdRadPerSec >= 0.0 && std::isfinite(gyroNormThresholdRadPerSec))
        || !(accelNormToleranceMps2 >= 0.0 && std::isfinite(accelNormToleranceMps2))
        || !(accelVarianceThresholdMps2Sq >= 0.0 && std::isfinite(accelVarianceThresholdMps2Sq))
        || !(minRestDurationSeconds >= 0.0 && std::isfinite(minRestDurationSeconds))
        || !(filterTauSeconds > 0.0 && std::isfinite(filterTauSeconds))
        || !(gravityMagnitudeMps2 > 0.0 && std::isfinite(gravityMagnitudeMps2))) {
        if (reason) {
            *reason = QStringLiteral("静止检测参数必须为合法的非负/正有限数");
        }
        return false;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

RestDetector::RestDetector(const Config &config)
    : config_(config)
{
}

void RestDetector::reset()
{
    accelMean_ = QVector3D();
    accelVariance_ = 0.0;
    restDuration_ = 0.0;
    rest_ = false;
    warm_ = false;
}

void RestDetector::setConfig(const Config &config)
{
    if (config.isValid()) {
        config_ = config;
    }
    reset();
}

RestDetector::Config RestDetector::config() const
{
    return config_;
}

bool RestDetector::isRest() const
{
    return rest_;
}

double RestDetector::restDurationSeconds() const
{
    return restDuration_;
}

void RestDetector::update(const QVector3D &acceleration, const QVector3D &gyroscope, double dtSeconds)
{
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0 || dtSeconds > 0.1) {
        restDuration_ = 0.0;
        rest_ = false;
        return;
    }
    if (!finite(acceleration) || !finite(gyroscope)) {
        restDuration_ = 0.0;
        rest_ = false;
        return;
    }

    const double alpha = 1.0 - std::exp(-dtSeconds / config_.filterTauSeconds);
    if (!warm_) {
        accelMean_ = acceleration;
        accelVariance_ = 0.0;
        warm_ = true;
    }

    const QVector3D deviation = acceleration - accelMean_;
    accelMean_ += (acceleration - accelMean_) * alpha;
    const double deviationSquared = double(deviation.x()) * deviation.x()
        + double(deviation.y()) * deviation.y() + double(deviation.z()) * deviation.z();
    accelVariance_ += alpha * (deviationSquared - accelVariance_);

    const double gyroNorm = norm(gyroscope);
    const double accelNorm = norm(acceleration);
    const bool accelStable = std::abs(accelNorm - config_.gravityMagnitudeMps2) <= config_.accelNormToleranceMps2
        && accelVariance_ <= config_.accelVarianceThresholdMps2Sq;
    const bool gyroStill = gyroNorm <= config_.gyroNormThresholdRadPerSec;

    if (accelStable && gyroStill) {
        restDuration_ += dtSeconds;
    } else {
        restDuration_ = 0.0;
    }
    rest_ = restDuration_ >= config_.minRestDurationSeconds;
}

}
