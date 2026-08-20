#include "sensor_panel.h"

#include <QDateTime>
#include <QGridLayout>
#include <QLabel>

namespace {
QString fusionModeText(FusionMode mode)
{
    switch (mode) {
    case FusionMode::SixAxis: return QStringLiteral("SixAxis");
    case FusionMode::NineAxis: return QStringLiteral("NineAxis");
    case FusionMode::Invalid: return QStringLiteral("Invalid");
    }
    return QStringLiteral("Invalid");
}

QLabel *makeValueLabel(QWidget *parent)
{
    auto *label = new QLabel(QStringLiteral("--"), parent);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}
}

SensorPanel::SensorPanel(SensorId sensorId, QWidget *parent)
    : QGroupBox(parent)
    , sensorId_(sensorId)
{
    const quint8 address = static_cast<quint8>(sensorId_);
    setObjectName(QStringLiteral("sensorPanel_%1").arg(address, 2, 16, QLatin1Char('0')));
    setTitle(QStringLiteral("%1 · 0x%2")
                 .arg(sensorDisplayName(sensorId_))
                 .arg(address, 2, 16, QLatin1Char('0')).toUpper());

    auto *layout = new QGridLayout(this);
    const QStringList axisNames{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    const QStringList quaternionNames{QStringLiteral("W"), QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    const QStringList eulerNames{QStringLiteral("Roll"), QStringLiteral("Pitch"), QStringLiteral("Yaw")};

    int row = 0;
    layout->addWidget(new QLabel(QStringLiteral("Raw"), this), row, 0);
    for (int column = 0; column < 3; ++column) {
        layout->addWidget(new QLabel(axisNames[column], this), row, column + 1);
    }

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("加速度"), this), row, 0);
    for (int column = 0; column < 3; ++column) {
        accelerationLabels_[column] = makeValueLabel(this);
        layout->addWidget(accelerationLabels_[column], row, column + 1);
    }

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("角速度"), this), row, 0);
    for (int column = 0; column < 3; ++column) {
        gyroscopeLabels_[column] = makeValueLabel(this);
        layout->addWidget(gyroscopeLabels_[column], row, column + 1);
    }

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("磁场"), this), row, 0);
    for (int column = 0; column < 3; ++column) {
        magnetometerLabels_[column] = makeValueLabel(this);
        layout->addWidget(magnetometerLabels_[column], row, column + 1);
    }

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("相对四元数"), this), row, 0);
    for (int column = 0; column < 4; ++column) {
        quaternionLabels_[column] = makeValueLabel(this);
        quaternionLabels_[column]->setToolTip(quaternionNames[column]);
        layout->addWidget(quaternionLabels_[column], row, column + 1);
    }

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("欧拉角"), this), row, 0);
    for (int column = 0; column < 3; ++column) {
        eulerLabels_[column] = makeValueLabel(this);
        eulerLabels_[column]->setToolTip(eulerNames[column]);
        layout->addWidget(eulerLabels_[column], row, column + 1);
    }

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("SEQ"), this), row, 0);
    sequenceLabel_ = makeValueLabel(this);
    layout->addWidget(sequenceLabel_, row, 1);
    layout->addWidget(new QLabel(QStringLiteral("融合"), this), row, 2);
    fusionModeLabel_ = makeValueLabel(this);
    layout->addWidget(fusionModeLabel_, row, 3, 1, 2);

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("更新时间"), this), row, 0);
    updatedLabel_ = makeValueLabel(this);
    layout->addWidget(updatedLabel_, row, 1, 1, 4);

    ++row;
    layout->addWidget(new QLabel(QStringLiteral("状态"), this), row, 0);
    statusLabel_ = makeValueLabel(this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_, row, 1, 1, 4);

    resetDisplay();
}

void SensorPanel::display(const ImuFrame &frame, const SensorPose &pose)
{
    setAxes(accelerationLabels_, frame.acceleration);
    setAxes(gyroscopeLabels_, frame.gyroscope);
    setAxes(magnetometerLabels_, frame.magnetometer);

    const QQuaternion quaternion = pose.relativeOrientation;
    quaternionLabels_[0]->setText(QString::number(quaternion.scalar(), 'f', 4));
    quaternionLabels_[1]->setText(QString::number(quaternion.x(), 'f', 4));
    quaternionLabels_[2]->setText(QString::number(quaternion.y(), 'f', 4));
    quaternionLabels_[3]->setText(QString::number(quaternion.z(), 'f', 4));
    eulerLabels_[0]->setText(QString::number(pose.relativeEuler.rollDegrees, 'f', 2));
    eulerLabels_[1]->setText(QString::number(pose.relativeEuler.pitchDegrees, 'f', 2));
    eulerLabels_[2]->setText(QString::number(pose.relativeEuler.yawDegrees, 'f', 2));
    sequenceLabel_->setText(QString::number(frame.sequence));
    updatedLabel_->setText(QStringLiteral("%1 ms").arg(pose.updatedMonotonicNs / 1000000));
    fusionModeLabel_->setText(fusionModeText(pose.mode));

    QStringList states;
    states << (pose.valid ? QStringLiteral("有效") : QStringLiteral("无效"));
    states << (frame.allZero || pose.sourceAllZero ? QStringLiteral("全零") : QStringLiteral("非全零"));
    states << (pose.calibrated ? QStringLiteral("已标定") : QStringLiteral("未标定"));
    if (!pose.status.isEmpty()) {
        states << pose.status;
    }
    statusLabel_->setText(states.join(QStringLiteral(" · ")));
}

void SensorPanel::resetDisplay()
{
    const auto resetLabels = [](const auto &labels) {
        for (QLabel *label : labels) {
            label->setText(QStringLiteral("--"));
        }
    };
    resetLabels(accelerationLabels_);
    resetLabels(gyroscopeLabels_);
    resetLabels(magnetometerLabels_);
    resetLabels(quaternionLabels_);
    resetLabels(eulerLabels_);
    sequenceLabel_->setText(QStringLiteral("--"));
    updatedLabel_->setText(QStringLiteral("--"));
    fusionModeLabel_->setText(QStringLiteral("Invalid"));
    statusLabel_->setText(QStringLiteral("无数据 · 未标定"));
}

void SensorPanel::setAxes(const std::array<QLabel *, 3> &labels, const RawAxes &axes)
{
    labels[0]->setText(QString::number(axes.x));
    labels[1]->setText(QString::number(axes.y));
    labels[2]->setText(QString::number(axes.z));
}
