#include "settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox *createSpinBox(const QString &objectName, double minimum, double maximum,
                              int decimals, QWidget *parent)
{
    auto *spinBox = new QDoubleSpinBox(parent);
    spinBox->setObjectName(objectName);
    spinBox->setRange(minimum, maximum);
    spinBox->setDecimals(decimals);
    spinBox->setKeyboardTracking(false);
    return spinBox;
}
}

SettingsDialog::SettingsDialog(const SolverSettings &settings, QWidget *parent)
    : QDialog(parent)
    , editedSettings_(settings)
{
    setWindowTitle(QStringLiteral("姿态参数"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *formLayout = new QFormLayout;

    accelerometerRangeSpinBox_ = createSpinBox(QStringLiteral("accelerometerRangeSpinBox"),
                                                0.000001, 128.0, 6, this);
    gyroscopeRangeSpinBox_ = createSpinBox(QStringLiteral("gyroscopeRangeSpinBox"),
                                            0.000001, 10000.0, 6, this);
    magnetometerDivisorSpinBox_ = createSpinBox(QStringLiteral("magnetometerDivisorSpinBox"),
                                                 0.000001, 1.0e9, 6, this);
    madgwickBetaSpinBox_ = createSpinBox(QStringLiteral("madgwickBetaSpinBox"),
                                          0.000001, 2.0, 6, this);
    magnetometerEnabledCheckBox_ = new QCheckBox(QStringLiteral("启用磁场融合"), this);
    magnetometerEnabledCheckBox_->setObjectName(QStringLiteral("magnetometerEnabledCheckBox"));
    magnetometerMinNormSpinBox_ = createSpinBox(QStringLiteral("magnetometerMinNormSpinBox"),
                                                 0.0, 1.0e12, 6, this);
    magnetometerMaxNormSpinBox_ = createSpinBox(QStringLiteral("magnetometerMaxNormSpinBox"),
                                                 0.000001, 1.0e12, 6, this);

    formLayout->addRow(QStringLiteral("加速度量程 (g)"), accelerometerRangeSpinBox_);
    formLayout->addRow(QStringLiteral("陀螺仪量程 (°/s)"), gyroscopeRangeSpinBox_);
    formLayout->addRow(QStringLiteral("磁场除数"), magnetometerDivisorSpinBox_);
    formLayout->addRow(QStringLiteral("Madgwick beta"), madgwickBetaSpinBox_);
    formLayout->addRow(QString(), magnetometerEnabledCheckBox_);
    formLayout->addRow(QStringLiteral("磁场最小模长"), magnetometerMinNormSpinBox_);
    formLayout->addRow(QStringLiteral("磁场最大模长"), magnetometerMaxNormSpinBox_);
    layout->addLayout(formLayout);

    settingsErrorLabel_ = new QLabel(this);
    settingsErrorLabel_->setObjectName(QStringLiteral("settingsErrorLabel"));
    settingsErrorLabel_->setStyleSheet(QStringLiteral("color: #b00020;"));
    settingsErrorLabel_->setWordWrap(true);
    settingsErrorLabel_->hide();
    layout->addWidget(settingsErrorLabel_);

    auto *buttonBox = new QDialogButtonBox(this);
    auto *restoreButton = buttonBox->addButton(QStringLiteral("恢复默认值"), QDialogButtonBox::ResetRole);
    restoreButton->setObjectName(QStringLiteral("restoreDefaultsButton"));
    auto *okButton = buttonBox->addButton(QDialogButtonBox::Ok);
    okButton->setObjectName(QStringLiteral("settingsOkButton"));
    okButton->setText(QStringLiteral("确定"));
    auto *cancelButton = buttonBox->addButton(QDialogButtonBox::Cancel);
    cancelButton->setObjectName(QStringLiteral("settingsCancelButton"));
    cancelButton->setText(QStringLiteral("取消"));
    layout->addWidget(buttonBox);

    connect(restoreButton, &QPushButton::clicked, this, &SettingsDialog::restoreDefaults);
    connect(okButton, &QPushButton::clicked, this, &SettingsDialog::validateAndAccept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    populate(settings);
}

SolverSettings SettingsDialog::editedSettings() const
{
    return editedSettings_;
}

void SettingsDialog::restoreDefaults()
{
    populate(SolverSettings::defaults());
    settingsErrorLabel_->clear();
    settingsErrorLabel_->hide();
}

void SettingsDialog::validateAndAccept()
{
    SolverSettings candidate;
    candidate.accelerometerRangeG = accelerometerRangeSpinBox_->value();
    candidate.gyroscopeRangeDps = gyroscopeRangeSpinBox_->value();
    candidate.magnetometerDivisor = magnetometerDivisorSpinBox_->value();
    candidate.madgwickBeta = madgwickBetaSpinBox_->value();
    candidate.magnetometerEnabled = magnetometerEnabledCheckBox_->isChecked();
    candidate.magnetometerMinNorm = magnetometerMinNormSpinBox_->value();
    candidate.magnetometerMaxNorm = magnetometerMaxNormSpinBox_->value();

    QString errorMessage;
    if (!candidate.isValid(&errorMessage)) {
        settingsErrorLabel_->setText(errorMessage);
        settingsErrorLabel_->show();
        return;
    }

    editedSettings_ = candidate;
    accept();
}

void SettingsDialog::populate(const SolverSettings &settings)
{
    accelerometerRangeSpinBox_->setValue(settings.accelerometerRangeG);
    gyroscopeRangeSpinBox_->setValue(settings.gyroscopeRangeDps);
    magnetometerDivisorSpinBox_->setValue(settings.magnetometerDivisor);
    madgwickBetaSpinBox_->setValue(settings.madgwickBeta);
    magnetometerEnabledCheckBox_->setChecked(settings.magnetometerEnabled);
    magnetometerMinNormSpinBox_->setValue(settings.magnetometerMinNorm);
    magnetometerMaxNormSpinBox_->setValue(settings.magnetometerMaxNorm);
}
