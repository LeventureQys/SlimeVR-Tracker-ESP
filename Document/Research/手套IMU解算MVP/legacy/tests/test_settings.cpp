#include <QtTest>

#include "settings_store.h"

#include <QDir>
#include <QUuid>

#include <limits>

namespace {
QString temporaryIniPath()
{
    const QString directoryPath = QDir::current().filePath(QStringLiteral("settings-test-data"));
    QDir().mkpath(directoryPath);
    return QDir(directoryPath).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".ini"));
}
}

class SettingsTest final : public QObject {
    Q_OBJECT

private slots:
    void C01_emptySettingsReturnDefaults();
    void C02_saveAndReloadNonDefaultSettings();
    void C03_damagedEntriesFallBackSafely();
    void C04_invalidSettingsAreRejectedWithoutSaving();
};

void SettingsTest::C01_emptySettingsReturnDefaults()
{
    const QString path = temporaryIniPath();
    QSettings settings(path, QSettings::IniFormat);
    SettingsStore store(settings);
    QCOMPARE(store.load(), SolverSettings::defaults());
}

void SettingsTest::C02_saveAndReloadNonDefaultSettings()
{
    const QString path = temporaryIniPath();
    SolverSettings expected;
    expected.accelerometerRangeG = 8.0;
    expected.gyroscopeRangeDps = 1000.0;
    expected.magnetometerDivisor = 240.0;
    expected.madgwickBeta = 0.25;
    expected.magnetometerEnabled = false;
    expected.magnetometerMinNorm = 0.5;
    expected.magnetometerMaxNorm = 5000.0;
    {
        QSettings settings(path, QSettings::IniFormat);
        SettingsStore store(settings);
        QString error;
        QVERIFY2(store.save(expected, &error), qPrintable(error));
    }
    QSettings reloadedSettings(path, QSettings::IniFormat);
    SettingsStore reloadedStore(reloadedSettings);
    QCOMPARE(reloadedStore.load(), expected);
}

void SettingsTest::C03_damagedEntriesFallBackSafely()
{
    const QString path = temporaryIniPath();
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("solver/accelerometerRangeG"), 8.0);
    settings.setValue(QStringLiteral("solver/gyroscopeRangeDps"), QStringLiteral("not-a-number"));
    settings.setValue(QStringLiteral("solver/magnetometerEnabled"), QStringLiteral("not-a-bool"));
    SettingsStore store(settings);
    SolverSettings loaded = store.load();
    QCOMPARE(loaded.accelerometerRangeG, 8.0);
    QCOMPARE(loaded.gyroscopeRangeDps, SolverSettings::defaults().gyroscopeRangeDps);
    QCOMPARE(loaded.magnetometerEnabled, SolverSettings::defaults().magnetometerEnabled);

    settings.setValue(QStringLiteral("solver/magnetometerMinNorm"), 20.0);
    settings.setValue(QStringLiteral("solver/magnetometerMaxNorm"), 10.0);
    QCOMPARE(store.load(), SolverSettings::defaults());
}

void SettingsTest::C04_invalidSettingsAreRejectedWithoutSaving()
{
    const QString path = temporaryIniPath();
    QSettings settings(path, QSettings::IniFormat);
    SettingsStore store(settings);
    const SolverSettings valid = SolverSettings::defaults();
    QVERIFY(store.save(valid));

    QList<SolverSettings> invalidValues;
    SolverSettings value = valid;
    value.accelerometerRangeG = std::numeric_limits<double>::quiet_NaN();
    invalidValues.append(value);
    value = valid;
    value.gyroscopeRangeDps = std::numeric_limits<double>::infinity();
    invalidValues.append(value);
    value = valid;
    value.accelerometerRangeG = 0.0;
    invalidValues.append(value);
    value = valid;
    value.gyroscopeRangeDps = 10000.1;
    invalidValues.append(value);
    value = valid;
    value.magnetometerDivisor = 0.0;
    invalidValues.append(value);
    value = valid;
    value.madgwickBeta = 2.1;
    invalidValues.append(value);
    value = valid;
    value.magnetometerMinNorm = 10.0;
    value.magnetometerMaxNorm = 10.0;
    invalidValues.append(value);

    for (const SolverSettings &invalid : invalidValues) {
        QString error;
        QVERIFY(!invalid.isValid(&error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!store.save(invalid, &error));
        QCOMPARE(store.load(), valid);
    }
}

QTEST_APPLESS_MAIN(SettingsTest)
#include "test_settings.moc"
