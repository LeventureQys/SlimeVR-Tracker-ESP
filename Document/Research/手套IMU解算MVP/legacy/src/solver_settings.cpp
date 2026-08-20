#include "solver_settings.h"

#include <cmath>

namespace {
bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage) {
        *errorMessage = message;
    }
    return false;
}
}

SolverSettings SolverSettings::defaults()
{
    return SolverSettings{};
}

bool SolverSettings::isValid(QString *errorMessage) const
{
    if (!std::isfinite(accelerometerRangeG) || !std::isfinite(gyroscopeRangeDps)
        || !std::isfinite(magnetometerDivisor) || !std::isfinite(madgwickBeta)
        || !std::isfinite(magnetometerMinNorm) || !std::isfinite(magnetometerMaxNorm)) {
        return fail(errorMessage, QStringLiteral("所有数值参数必须为有限数"));
    }
    if (!(accelerometerRangeG > 0.0 && accelerometerRangeG <= 128.0)) {
        return fail(errorMessage, QStringLiteral("加速度量程必须在 (0, 128] g"));
    }
    if (!(gyroscopeRangeDps > 0.0 && gyroscopeRangeDps <= 10000.0)) {
        return fail(errorMessage, QStringLiteral("陀螺仪量程必须在 (0, 10000] °/s"));
    }
    if (!(magnetometerDivisor > 0.0 && magnetometerDivisor <= 1.0e9)) {
        return fail(errorMessage, QStringLiteral("磁场除数必须在 (0, 1e9]"));
    }
    if (!(madgwickBeta > 0.0 && madgwickBeta <= 2.0)) {
        return fail(errorMessage, QStringLiteral("Madgwick beta 必须在 (0, 2]"));
    }
    if (!(magnetometerMinNorm >= 0.0 && magnetometerMinNorm < 1.0e12)) {
        return fail(errorMessage, QStringLiteral("磁场最小模长必须在 [0, 1e12)"));
    }
    if (!(magnetometerMaxNorm > 0.0 && magnetometerMaxNorm <= 1.0e12)) {
        return fail(errorMessage, QStringLiteral("磁场最大模长必须在 (0, 1e12]"));
    }
    if (!(magnetometerMinNorm < magnetometerMaxNorm)) {
        return fail(errorMessage, QStringLiteral("磁场最小模长必须小于最大模长"));
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool SolverSettings::operator==(const SolverSettings &other) const
{
    return accelerometerRangeG == other.accelerometerRangeG
        && gyroscopeRangeDps == other.gyroscopeRangeDps
        && magnetometerDivisor == other.magnetometerDivisor
        && madgwickBeta == other.madgwickBeta
        && magnetometerEnabled == other.magnetometerEnabled
        && magnetometerMinNorm == other.magnetometerMinNorm
        && magnetometerMaxNorm == other.magnetometerMaxNorm;
}
