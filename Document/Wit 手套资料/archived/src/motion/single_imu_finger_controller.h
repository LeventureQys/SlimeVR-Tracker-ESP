#pragma once

#include "pose_solver.h"

namespace handdemo::motion {

enum class SingleImuDriveState {
    Disconnected,
    ConnectedUnbound,
    BoundUncalibrated,
    Ready,
    Driving,
    Error
};

struct SingleImuMappingOutput {
    PoseResult pose;
    SingleImuDriveState state{SingleImuDriveState::Disconnected};
    int fingerIndex{-1};
    float flexionDegrees{0.0F};
    float abductionDegrees{0.0F};
    float curl{0.0F};
    bool frameApplied{false};
    QVector<PoseInputError> errors;
};

class SingleImuFingerController {
public:
    explicit SingleImuFingerController(const PoseSolver &solver);
    void setConnected(bool connected);
    void bindFinger(int fingerIndex);
    bool calibrate(const QQuaternion &rawOrientation, qint64 timestampUsec);
    bool setDriving(bool enabled);
    SingleImuMappingOutput update(const QQuaternion &rawOrientation, qint64 timestampUsec);
    void resetCalibration();
    SingleImuDriveState state() const;
    const SingleImuMappingOutput &output() const;

private:
    void resetToState(SingleImuDriveState state);
    static std::optional<QQuaternion> normalized(const QQuaternion &orientation);

    const PoseSolver &solver_;
    SingleImuMappingOutput output_;
    std::optional<QQuaternion> zeroOrientation_;
    qint64 lastTimestampUsec_{-1};
};

}
