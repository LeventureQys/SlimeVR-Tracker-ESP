#pragma once

#include "core/fusion_types.h"
#include "core/hand_observation_types.h"

#include <QVector3D>

#include <array>

namespace handstudio {

struct FingerObservationConfig {
    SensorId sensorId = SensorId::Thumb;
    QQuaternion mountOrientation;
    QVector3D flexionAxis{1.0F, 0.0F, 0.0F};
    QVector3D abductionAxis{0.0F, 1.0F, 0.0F};
    QVector3D twistAxis{0.0F, 0.0F, 1.0F};
};

class HandObservationSolver {
public:
    HandObservationSolver();
    explicit HandObservationSolver(std::array<FingerObservationConfig, 5> configs,
                                   HandSide handSide = HandSide::Left);

    HandObservationFrame solve(const std::array<FusedImuPose, 6> &poses);
    void reset();

    // 调零（中立姿态校准）：把当前六路姿态记录为中立参考。此后 solve() 输出
    // 相对中立位的屈伸/张合/扭转与相对中立位的手腕姿态；再次调用可重新调零。
    // 要求六路全部有效且同 sequence，否则返回 false 且不改变现状。
    bool setNeutral(const std::array<FusedImuPose, 6> &poses);
    void clearNeutral();
    bool hasNeutral() const noexcept;

private:
    std::array<FingerObservationConfig, 5> configs_;
    HandSide handSide_ = HandSide::Left;
    HandObservationFrame lastFrame_;
    bool hasLastFrame_ = false;
    std::array<QQuaternion, 5> neutralFinger_{};
    QQuaternion neutralWrist_;
    bool hasNeutral_ = false;
};

}
