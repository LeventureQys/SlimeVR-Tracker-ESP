#include "settings_store.h"

namespace {
const QString AccelerometerRangeKey = QStringLiteral("solver/accelerometerRangeG");
const QString GyroscopeRangeKey = QStringLiteral("solver/gyroscopeRangeDps");
const QString MagnetometerDivisorKey = QStringLiteral("solver/magnetometerDivisor");
const QString MadgwickBetaKey = QStringLiteral("solver/madgwickBeta");
const QString MagnetometerEnabledKey = QStringLiteral("solver/magnetometerEnabled");
const QString MagnetometerMinNormKey = QStringLiteral("solver/magnetometerMinNorm");
const QString MagnetometerMaxNormKey = QStringLiteral("solver/magnetometerMaxNorm");

double loadDouble(const QSettings &settings, const QString &key, double fallback)
{
    if (!settings.contains(key)) {
        return fallback;
    }
    bool ok = false;
    const double value = settings.value(key).toDouble(&ok);
    return ok ? value : fallback;
}

bool loadBool(const QSettings &settings, const QString &key, bool fallback)
{
    if (!settings.contains(key)) {
        return fallback;
    }
    const QVariant value = settings.value(key);
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) {
        return true;
    }
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) {
        return false;
    }
    return fallback;
}
}

SettingsStore::SettingsStore(QSettings &settings)
    : settings_(settings)
{
}

SolverSettings SettingsStore::load() const
{
    const SolverSettings defaults = SolverSettings::defaults();
    SolverSettings result = defaults;
    result.accelerometerRangeG = loadDouble(settings_, AccelerometerRangeKey, defaults.accelerometerRangeG);
    result.gyroscopeRangeDps = loadDouble(settings_, GyroscopeRangeKey, defaults.gyroscopeRangeDps);
    result.magnetometerDivisor = loadDouble(settings_, MagnetometerDivisorKey, defaults.magnetometerDivisor);
    result.madgwickBeta = loadDouble(settings_, MadgwickBetaKey, defaults.madgwickBeta);
    result.magnetometerEnabled = loadBool(settings_, MagnetometerEnabledKey, defaults.magnetometerEnabled);
    result.magnetometerMinNorm = loadDouble(settings_, MagnetometerMinNormKey, defaults.magnetometerMinNorm);
    result.magnetometerMaxNorm = loadDouble(settings_, MagnetometerMaxNormKey, defaults.magnetometerMaxNorm);
    return result.isValid() ? result : defaults;
}

bool SettingsStore::save(const SolverSettings &value, QString *errorMessage)
{
    if (!value.isValid(errorMessage)) {
        return false;
    }
    settings_.setValue(AccelerometerRangeKey, value.accelerometerRangeG);
    settings_.setValue(GyroscopeRangeKey, value.gyroscopeRangeDps);
    settings_.setValue(MagnetometerDivisorKey, value.magnetometerDivisor);
    settings_.setValue(MadgwickBetaKey, value.madgwickBeta);
    settings_.setValue(MagnetometerEnabledKey, value.magnetometerEnabled);
    settings_.setValue(MagnetometerMinNormKey, value.magnetometerMinNorm);
    settings_.setValue(MagnetometerMaxNormKey, value.magnetometerMaxNorm);
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("设置写入失败，QSettings 状态码：%1").arg(int(settings_.status()));
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void SettingsStore::clear()
{
    settings_.remove(QStringLiteral("solver"));
    settings_.sync();
}
