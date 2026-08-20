#include "static_gyro_bias_estimator.h"

#include <cmath>

namespace handstudio {

bool StaticGyroBiasEstimator::Config::isValid(QString *reason) const
{
    if (minRestSamples < 1 || !(maxBiasRadPerSec > 0.0 && std::isfinite(maxBiasRadPerSec))) {
        if (reason) {
            *reason = QStringLiteral("零偏估计参数非法");
        }
        return false;
    }
    return rest.isValid(reason);
}

StaticGyroBiasEstimator::StaticGyroBiasEstimator(const Config &config)
    : config_(config)
    , restDetector_(config.rest)
{
}

void StaticGyroBiasEstimator::reset()
{
    restDetector_.reset();
    bias_ = QVector3D();
    restSamples_ = 0;
    initialized_ = false;
}

void StaticGyroBiasEstimator::setConfig(const Config &config)
{
    if (config.isValid()) {
        config_ = config;
        restDetector_.setConfig(config.rest);
    }
    reset();
}

StaticGyroBiasEstimator::Config StaticGyroBiasEstimator::config() const
{
    return config_;
}

void StaticGyroBiasEstimator::update(const QVector3D &acceleration, const QVector3D &gyroscope, double dtSeconds)
{
    restDetector_.update(acceleration, gyroscope, dtSeconds);
    if (!restDetector_.isRest()) {
        // Motion: freeze the current estimate, do not accumulate or reset.
        return;
    }

    if (!initialized_) {
        bias_ = gyroscope;
        initialized_ = true;
        restSamples_ = 1;
    } else {
        const qint64 next = restSamples_ + 1;
        const double weightNew = 1.0 / double(next);
        const double weightOld = double(restSamples_) / double(next);
        bias_ = bias_ * weightOld + gyroscope * weightNew;
        restSamples_ = next;
    }

    const double magnitude = std::sqrt(double(bias_.x()) * bias_.x() + double(bias_.y()) * bias_.y()
                                       + double(bias_.z()) * bias_.z());
    if (magnitude > config_.maxBiasRadPerSec) {
        bias_ = bias_ * (config_.maxBiasRadPerSec / magnitude);
    }
}

QVector3D StaticGyroBiasEstimator::bias() const
{
    return bias_;
}

bool StaticGyroBiasEstimator::isRest() const
{
    return restDetector_.isRest();
}

bool StaticGyroBiasEstimator::converged() const
{
    return restSamples_ >= config_.minRestSamples;
}

qint64 StaticGyroBiasEstimator::restSampleCount() const
{
    return restSamples_;
}

}
