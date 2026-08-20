#include <QtTest>

#include "main_window.h"
#include "sensor_panel.h"
#include "settings_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QTableWidget>

namespace {
QString settingsPath()
{
    return QDir::current().filePath(QStringLiteral("build/test_ui_settings.ini"));
}

void removeSettingsFile()
{
    QFile::remove(settingsPath());
}

QString combinedLabelText(const QWidget *widget)
{
    QStringList texts;
    const QList<QLabel *> labels = widget->findChildren<QLabel *>();
    for (const QLabel *label : labels) {
        texts.append(label->text());
    }
    return texts.join(QLatin1Char('|'));
}

SolverSettings nonDefaultSettings()
{
    SolverSettings settings = SolverSettings::defaults();
    settings.accelerometerRangeG = 8.0;
    settings.gyroscopeRangeDps = 1000.0;
    settings.magnetometerDivisor = 240.0;
    settings.madgwickBeta = 0.25;
    settings.magnetometerEnabled = false;
    settings.magnetometerMinNorm = 0.5;
    settings.magnetometerMaxNorm = 5000.0;
    return settings;
}
}

class MainWindowTest final : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void C01_mainControlsHaveObjectNames();
    void C02_allSixSensorPanelsExist();
    void C03_demoModeDisablesSerialControlsAndRestoresThem();
    void C04_demoPipelineUpdatesSequenceAndSensorDisplay();
    void C05_calibrationRequiresSixValidDemoPoses();
    void C06_settingsDialogCancelKeepsOriginalSettings();
    void C07_settingsDialogRestoreThenCancelKeepsOriginalSettings();
    void C08_settingsDialogRestoreThenAcceptReturnsDefaults();
    void C09_settingsDialogRejectsInvalidNormRange();
    void C10_injectedSettingsConstructVisibleWindow();
};

void MainWindowTest::init()
{
    removeSettingsFile();
    QDir().mkpath(QDir::current().filePath(QStringLiteral("build")));
}

void MainWindowTest::cleanup()
{
    removeSettingsFile();
}

void MainWindowTest::C01_mainControlsHaveObjectNames()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    MainWindow window(&settings);

    QVERIFY(window.findChild<QComboBox *>(QStringLiteral("portComboBox")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("refreshPortsButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("openPortButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("closePortButton")));
    QVERIFY(window.findChild<QCheckBox *>(QStringLiteral("demoModeCheckBox")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("settingsButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("calibrateZeroButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("clearCalibrationButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("resetStatisticsButton")));
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("statusLabel")));
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("latestSequenceLabel")));
    QVERIFY(window.findChild<QTableWidget *>(QStringLiteral("statisticsTableWidget")));
    QVERIFY(window.findChild<QScrollArea *>(QStringLiteral("sensorScrollArea")));
}

void MainWindowTest::C02_allSixSensorPanelsExist()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    MainWindow window(&settings);

    for (int address = 0x50; address <= 0x55; ++address) {
        const QString objectName = QStringLiteral("sensorPanel_%1")
                                       .arg(address, 2, 16, QLatin1Char('0'));
        QVERIFY2(window.findChild<SensorPanel *>(objectName), qPrintable(objectName));
    }
}

void MainWindowTest::C03_demoModeDisablesSerialControlsAndRestoresThem()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    MainWindow window(&settings);
    auto *portComboBox = window.findChild<QComboBox *>(QStringLiteral("portComboBox"));
    auto *refreshButton = window.findChild<QPushButton *>(QStringLiteral("refreshPortsButton"));
    auto *openButton = window.findChild<QPushButton *>(QStringLiteral("openPortButton"));

    QVERIFY(portComboBox);
    QVERIFY(refreshButton);
    QVERIFY(openButton);
    const bool initialOpenEnabled = openButton->isEnabled();

    window.setDemoModeEnabled(true);
    QVERIFY(!portComboBox->isEnabled());
    QVERIFY(!refreshButton->isEnabled());
    QVERIFY(!openButton->isEnabled());

    window.setDemoModeEnabled(false);
    QVERIFY(portComboBox->isEnabled());
    QVERIFY(refreshButton->isEnabled());
    QCOMPARE(openButton->isEnabled(), initialOpenEnabled);
}

void MainWindowTest::C04_demoPipelineUpdatesSequenceAndSensorDisplay()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    MainWindow window(&settings);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    auto *latestSequenceLabel = window.findChild<QLabel *>(QStringLiteral("latestSequenceLabel"));
    auto *panel = window.findChild<SensorPanel *>(QStringLiteral("sensorPanel_50"));
    QVERIFY(latestSequenceLabel);
    QVERIFY(panel);
    QCOMPARE(latestSequenceLabel->text(), QStringLiteral("最新组 SEQ: --"));

    window.setDemoModeEnabled(true);
    QTest::qWait(300);

    QVERIFY2(latestSequenceLabel->text() != QStringLiteral("最新组 SEQ: --"),
             qPrintable(latestSequenceLabel->text()));
    const QString panelText = combinedLabelText(panel);
    QVERIFY2(panelText.contains(QStringLiteral("NineAxis")) || panelText.contains(QStringLiteral("SixAxis")),
             qPrintable(panelText));
    QVERIFY2(panelText.contains(QStringLiteral("有效")), qPrintable(panelText));
    QVERIFY2(!panelText.contains(QStringLiteral("--|--|--|--|--|--|--|--|--")),
             qPrintable(panelText));
}

void MainWindowTest::C05_calibrationRequiresSixValidDemoPoses()
{
    QSettings settings(settingsPath(), QSettings::IniFormat);
    MainWindow window(&settings);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *calibrateButton = window.findChild<QPushButton *>(QStringLiteral("calibrateZeroButton"));
    QVERIFY(calibrateButton);
    QVERIFY(!calibrateButton->isEnabled());

    window.setDemoModeEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(calibrateButton->isEnabled(), 1000);
}

void MainWindowTest::C06_settingsDialogCancelKeepsOriginalSettings()
{
    const SolverSettings original = nonDefaultSettings();
    SettingsDialog dialog(original);
    auto *accelerometer = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("accelerometerRangeSpinBox"));
    auto *cancelButton = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancelButton"));
    QVERIFY(accelerometer);
    QVERIFY(cancelButton);

    accelerometer->setValue(32.0);
    QTest::mouseClick(cancelButton, Qt::LeftButton);

    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QCOMPARE(dialog.editedSettings(), original);
}

void MainWindowTest::C07_settingsDialogRestoreThenCancelKeepsOriginalSettings()
{
    const SolverSettings original = nonDefaultSettings();
    SettingsDialog dialog(original);
    auto *restoreButton = dialog.findChild<QPushButton *>(QStringLiteral("restoreDefaultsButton"));
    auto *cancelButton = dialog.findChild<QPushButton *>(QStringLiteral("settingsCancelButton"));
    QVERIFY(restoreButton);
    QVERIFY(cancelButton);

    QTest::mouseClick(restoreButton, Qt::LeftButton);
    QTest::mouseClick(cancelButton, Qt::LeftButton);

    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QCOMPARE(dialog.editedSettings(), original);
}

void MainWindowTest::C08_settingsDialogRestoreThenAcceptReturnsDefaults()
{
    SettingsDialog dialog(nonDefaultSettings());
    auto *restoreButton = dialog.findChild<QPushButton *>(QStringLiteral("restoreDefaultsButton"));
    auto *okButton = dialog.findChild<QPushButton *>(QStringLiteral("settingsOkButton"));
    QVERIFY(restoreButton);
    QVERIFY(okButton);

    QTest::mouseClick(restoreButton, Qt::LeftButton);
    QTest::mouseClick(okButton, Qt::LeftButton);

    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.editedSettings(), SolverSettings::defaults());
}

void MainWindowTest::C09_settingsDialogRejectsInvalidNormRange()
{
    const SolverSettings original = nonDefaultSettings();
    SettingsDialog dialog(original);
    auto *minimum = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("magnetometerMinNormSpinBox"));
    auto *maximum = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("magnetometerMaxNormSpinBox"));
    auto *okButton = dialog.findChild<QPushButton *>(QStringLiteral("settingsOkButton"));
    auto *errorLabel = dialog.findChild<QLabel *>(QStringLiteral("settingsErrorLabel"));
    QVERIFY(minimum);
    QVERIFY(maximum);
    QVERIFY(okButton);
    QVERIFY(errorLabel);

    minimum->setValue(100.0);
    maximum->setValue(100.0);
    QTest::mouseClick(okButton, Qt::LeftButton);

    QVERIFY(dialog.result() != QDialog::Accepted);
    QVERIFY(!errorLabel->text().isEmpty());
    QVERIFY(errorLabel->isVisibleTo(&dialog));
    QCOMPARE(dialog.editedSettings(), original);
}

void MainWindowTest::C10_injectedSettingsConstructVisibleWindow()
{
    const SolverSettings injected = nonDefaultSettings();
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("solver/accelerometerRangeG"), injected.accelerometerRangeG);
    settings.setValue(QStringLiteral("solver/gyroscopeRangeDps"), injected.gyroscopeRangeDps);
    settings.setValue(QStringLiteral("solver/magnetometerDivisor"), injected.magnetometerDivisor);
    settings.setValue(QStringLiteral("solver/madgwickBeta"), injected.madgwickBeta);
    settings.setValue(QStringLiteral("solver/magnetometerEnabled"), injected.magnetometerEnabled);
    settings.setValue(QStringLiteral("solver/magnetometerMinNorm"), injected.magnetometerMinNorm);
    settings.setValue(QStringLiteral("solver/magnetometerMaxNorm"), injected.magnetometerMaxNorm);
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);

    MainWindow window(&settings);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.isVisible());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("statusLabel")));
}

QTEST_MAIN(MainWindowTest)
#include "test_main_window.moc"
