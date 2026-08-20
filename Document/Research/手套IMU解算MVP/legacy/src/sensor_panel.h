#pragma once

#include "imu_types.h"
#include "six_imu_solver.h"

#include <QGroupBox>

#include <array>

class QLabel;

class SensorPanel final : public QGroupBox {
    Q_OBJECT

public:
    explicit SensorPanel(SensorId sensorId, QWidget *parent = nullptr);
    void display(const ImuFrame &frame, const SensorPose &pose);
    void resetDisplay();

private:
    void setAxes(const std::array<QLabel *, 3> &labels, const RawAxes &axes);

    SensorId sensorId_;
    std::array<QLabel *, 3> accelerationLabels_{};
    std::array<QLabel *, 3> gyroscopeLabels_{};
    std::array<QLabel *, 3> magnetometerLabels_{};
    std::array<QLabel *, 4> quaternionLabels_{};
    std::array<QLabel *, 3> eulerLabels_{};
    QLabel *sequenceLabel_ = nullptr;
    QLabel *updatedLabel_ = nullptr;
    QLabel *fusionModeLabel_ = nullptr;
    QLabel *statusLabel_ = nullptr;
};
