#pragma once

#include "core/hand_observation_types.h"
#include "core/hand_skeleton_frame.h"
#include "model/model_data.h"

#include <QHash>
#include <QStringList>

#include <array>
#include <optional>

namespace handstudio {

struct JointMotionConfig {
    QString name;
    QString parentName;
    QVector3D flexionAxis{1.0F, 0.0F, 0.0F};
    QVector3D abductionAxis{0.0F, 1.0F, 0.0F};
    QVector3D twistAxis{0.0F, 0.0F, 1.0F};
    QVector3D minimumDegrees{-180.0F, -180.0F, -180.0F};
    QVector3D maximumDegrees{180.0F, 180.0F, 180.0F};
    QVector3D lockedAxes;
    QVector3D coupling{1.0F, 1.0F, 1.0F};
    int fingerIndex = -1;
};

struct MissingFrameConfig {
    qint64 heldDurationNs = 150000000;
    qint64 returnDurationNs = 350000000;
    qint64 recoveredDurationNs = 200000000;
    float heldConfidencePerSecond = 0.8F;
    float recoveryDegreesPerSecond = 360.0F;
};

struct HandRigConfig {
    int schemaVersion = 1;
    QString skeletonId;
    HandSide handSide = HandSide::Left;
    QString rootName;
    QVector<JointMotionConfig> joints;
    std::array<QQuaternion, 5> mountOrientations{};
    MissingFrameConfig missingFrames;
};

struct HandRigLoadResult {
    std::optional<HandRigConfig> config;
    QStringList errors;
};

HandRigLoadResult loadHandRigConfig(const QByteArray &json, const RiggedModel &model);

class KinematicSkeleton {
public:
    KinematicSkeleton(RiggedModel model, HandRigConfig config);

    HandSkeletonFrame solve(const HandObservationFrame &observation);
    bool isValid() const noexcept;
    QString errorString() const;

private:
    struct FingerState {
        QVector3D degrees;
        qint64 lastValidTimestampNs = 0;
        qint64 recoveredUntilNs = 0;
        float confidence = 0.0F;
        bool hasPose = false;
        bool wasHeld = false;
    };

    RiggedModel model_;
    HandRigConfig config_;
    QHash<QString, int> jointIndexByName_;
    QVector<int> parentIndices_;
    std::array<FingerState, 5> fingerStates_{};
    HandSkeletonFrame lastFrame_;
    bool valid_ = false;
    QString error_;
};

}
