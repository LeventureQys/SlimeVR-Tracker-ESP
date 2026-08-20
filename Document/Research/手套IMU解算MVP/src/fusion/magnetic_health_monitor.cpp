#include "magnetic_health_monitor.h"

#include <cmath>

namespace handstudio {

bool MagneticHealthMonitor::Config::isValid(QString *reason) const
{
    if (!(minNormMicroTesla > 0.0 && std::isfinite(minNormMicroTesla))
        || !(referenceNormMicroTesla >= 0.0 && std::isfinite(referenceNormMicroTesla))
        || !(toleranceRatio > 0.0 && toleranceRatio < 1.0 && std::isfinite(toleranceRatio))
        || !(referenceAdaptGain >= 0.0 && referenceAdaptGain <= 1.0 && std::isfinite(referenceAdaptGain))
        || disturbSamples < 1 || recoverSamples < 1 || healthySamples < 1
        || healthySamples < recoverSamples) {
        if (reason) {
            *reason = QStringLiteral("磁健康状态机参数非法");
        }
        return false;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

MagneticHealthMonitor::MagneticHealthMonitor(const Config &config)
    : config_(config)
    , referenceNorm_(config.referenceNormMicroTesla)
{
}

void MagneticHealthMonitor::reset()
{
    state_ = MagneticHealth::Unavailable;
    referenceNorm_ = config_.referenceNormMicroTesla;
    abnormalCount_ = 0;
    normalCount_ = 0;
}

void MagneticHealthMonitor::setConfig(const Config &config)
{
    if (config.isValid()) {
        config_ = config;
    }
    reset();
}

MagneticHealthMonitor::Config MagneticHealthMonitor::config() const
{
    return config_;
}

MagneticHealth MagneticHealthMonitor::state() const
{
    return state_;
}

int MagneticHealthMonitor::abnormalCount() const
{
    return abnormalCount_;
}

int MagneticHealthMonitor::normalCount() const
{
    return normalCount_;
}

MagneticHealth MagneticHealthMonitor::update(const QVector3D &magneticMicroTesla)
{
    if (!config_.enabled) {
        state_ = MagneticHealth::Unavailable;
        abnormalCount_ = 0;
        normalCount_ = 0;
        return state_;
    }

    const double norm = std::sqrt(double(magneticMicroTesla.x()) * magneticMicroTesla.x()
                                  + double(magneticMicroTesla.y()) * magneticMicroTesla.y()
                                  + double(magneticMicroTesla.z()) * magneticMicroTesla.z());
    const bool finite = std::isfinite(double(magneticMicroTesla.x()))
        && std::isfinite(double(magneticMicroTesla.y()))
        && std::isfinite(double(magneticMicroTesla.z()));
    if (!finite || norm < config_.minNormMicroTesla) {
        state_ = MagneticHealth::Unavailable;
        abnormalCount_ = 0;
        normalCount_ = 0;
        return state_;
    }

    if (referenceNorm_ <= 0.0) {
        referenceNorm_ = norm;
    }

    const bool abnormal = std::abs(norm - referenceNorm_) > config_.toleranceRatio * referenceNorm_;
    if (abnormal) {
        ++abnormalCount_;
        normalCount_ = 0;
    } else {
        ++normalCount_;
        abnormalCount_ = 0;
        referenceNorm_ += config_.referenceAdaptGain * (norm - referenceNorm_);
    }

    switch (state_) {
    case MagneticHealth::Unavailable:
        state_ = MagneticHealth::Recovering;
        break;
    case MagneticHealth::Healthy:
        if (abnormalCount_ >= config_.disturbSamples) {
            state_ = MagneticHealth::Disturbed;
        }
        break;
    case MagneticHealth::Disturbed:
        if (normalCount_ >= config_.recoverSamples) {
            state_ = MagneticHealth::Recovering;
        }
        break;
    case MagneticHealth::Recovering:
        if (abnormalCount_ >= 1) {
            state_ = MagneticHealth::Disturbed;
        } else if (normalCount_ >= config_.healthySamples) {
            state_ = MagneticHealth::Healthy;
        }
        break;
    }
    return state_;
}

}
