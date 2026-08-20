#pragma once

#include "imu_types.h"
#include "slimevr_settings.h"

#include <QQuaternion>

#include <array>
#include <optional>

// Pure quaternion coordinate stage between the fused source orientation and
// the SlimeVR node frame. See 坐标系设计.md for the full derivation.
//
//   q_source : rotation from the source reference frame to the current
//              source frame (active convention, right-handed; Madgwick output).
//   q_mount  : fixed rotation from the SlimeVR node frame to the sensor frame
//              (the physical mounting orientation).
//   q_total  = conj(q_mount) * q_source * q_mount   (Hamilton products).
//
// With an identity mounting, q_total == q_source. Left/right hand selection
// does not change the quaternion formula: hand mirroring is carried by the
// server-side body part model, not by negating axes here.
class SlimeVrCoordinateTransform final {
public:
    explicit SlimeVrCoordinateTransform(GloveSide side = GloveSide::Left);

    void setGloveSide(GloveSide side);
    GloveSide gloveSide() const;

    bool setMounting(SensorId id, const QQuaternion &mounting);
    QQuaternion mounting(SensorId id) const;
    void setMountings(const std::array<QQuaternion, 6> &mountings);
    void resetAll();

    QQuaternion transform(SensorId id, const QQuaternion &source) const;

private:
    GloveSide side_ = GloveSide::Left;
    std::array<QQuaternion, 6> mountings_;
};

// Returns the quaternion when it is finite and unit-length (within 1e-3),
// otherwise nullopt. Used for settings input and API preconditions.
std::optional<QQuaternion> sanitizeMountingRotation(const QQuaternion &value);
