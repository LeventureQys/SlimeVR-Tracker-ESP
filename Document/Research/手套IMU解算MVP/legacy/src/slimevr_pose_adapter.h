#pragma once

#include "slimevr_coordinate_transform.h"
#include "slimevr_settings.h"
#include "six_imu_solver.h"

#include <QQuaternion>

#include <array>

struct SlimeVrPoseSample {
    quint8 sensorId = 0;
    QQuaternion orientation; // normalized node orientation, x/y/z/w on wire
    qint64 updatedMonotonicNs = 0;
    bool valid = false;
};

// Converts a SixImuSnapshot into six network-ready samples. Uses the fused
// worldOrientation: SlimeVR Server applies its own mounting calibration, so
// the tool's display zero is intentionally NOT baked into the stream. The
// per-sensor fixed mounting rotation (S2.3) is applied through the
// SlimeVrCoordinateTransform stage.
class SlimeVrPoseAdapter final {
public:
    explicit SlimeVrPoseAdapter(GloveSide side = GloveSide::Left);

    void setGloveSide(GloveSide side);
    GloveSide gloveSide() const;

    bool setMounting(SensorId id, const QQuaternion &mounting);
    void setMountings(const std::array<QQuaternion, 6> &mountings);
    QQuaternion mounting(SensorId id) const;

    std::array<SlimeVrPoseSample, 6> adapt(const SixImuSnapshot &snapshot) const;

private:
    GloveSide side_ = GloveSide::Left;
    SlimeVrCoordinateTransform transform_;
};
