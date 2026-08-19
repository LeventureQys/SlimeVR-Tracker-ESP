#pragma once

#include "pose_solver.h"

namespace handdemo::motion {

struct ImuMappingResult {
    PoseResult pose;
    QVector<PoseInputError> errors;
    std::array<bool, 5> fingerValid{false, false, false, false, false};
    bool frameApplied{false};
};

class ImuPoseMapper {
public:
    explicit ImuPoseMapper(const PoseSolver &solver);
    ImuMappingResult update(const HandImuFrame &frame);
    void reset();

    static std::optional<QQuaternion> normalizedOrientation(const ImuSample &sample);
    static float signedTwistDegrees(const QQuaternion &orientation, const QVector3D &axis);

private:
    const PoseSolver &solver_;
    SkeletonPose pose_;
    qint64 lastTimestampUsec_{-1};
    std::array<bool, 5> hasFingerPose_{false, false, false, false, false};
};

class DeterministicHandPoseSource final : public IHandPoseSource {
public:
    explicit DeterministicHandPoseSource(qint64 startTimestampUsec = 0);
    std::optional<HandImuFrame> poll() override;
    void setValid(ImuSlot slot, bool valid);
    void reset(qint64 startTimestampUsec = 0);

private:
    qint64 timestampUsec_{0};
    quint64 frameIndex_{0};
    std::array<bool, 6> valid_{true, true, true, true, true, true};
};

}
