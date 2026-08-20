#pragma once

#include "solver_settings.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const SolverSettings &settings, QWidget *parent = nullptr);
    SolverSettings editedSettings() const;

private slots:
    void restoreDefaults();
    void validateAndAccept();

private:
    void populate(const SolverSettings &settings);

    QDoubleSpinBox *accelerometerRangeSpinBox_ = nullptr;
    QDoubleSpinBox *gyroscopeRangeSpinBox_ = nullptr;
    QDoubleSpinBox *magnetometerDivisorSpinBox_ = nullptr;
    QDoubleSpinBox *madgwickBetaSpinBox_ = nullptr;
    QCheckBox *magnetometerEnabledCheckBox_ = nullptr;
    QDoubleSpinBox *magnetometerMinNormSpinBox_ = nullptr;
    QDoubleSpinBox *magnetometerMaxNormSpinBox_ = nullptr;
    QLabel *settingsErrorLabel_ = nullptr;
    SolverSettings editedSettings_;
};
