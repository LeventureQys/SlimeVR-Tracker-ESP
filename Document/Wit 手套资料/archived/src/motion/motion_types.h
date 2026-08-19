#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <array>
#include <optional>

namespace handdemo::motion {

enum class ImuSlot { Palm = 0, Thumb, Index, Middle, Ring, Little };

struct ImuSample {
    ImuSlot slot{ImuSlot::Palm};
    QQuaternion orientation;
    qint64 timestampUsec{0};
    bool valid{false};
};

struct HandImuFrame {
    std::array<ImuSample, 6> samples;
};

class IHandPoseSource {
public:
    virtual ~IHandPoseSource() = default;
    virtual std::optional<HandImuFrame> poll() = 0;
};

struct BoneBinding {
    QString name;
    int parentIndex{-1};
    QMatrix4x4 bindLocal;
    QMatrix4x4 inverseBind;
};

struct SkeletonBinding {
    QVector<BoneBinding> bones;
    QMatrix4x4 globalInverse;
};

struct JointLimit {
    QVector3D minDegrees;
    QVector3D maxDegrees;
};

struct JointPose {
    QVector3D eulerDegrees;
};

struct SkeletonPose {
    QVector<JointPose> localPoses;
};

struct JointDiagnostic {
    bool constrained{false};
    QVector3D requestedDegrees;
    QVector3D appliedDegrees;
};

struct PoseResult {
    SkeletonPose pose;
    QVector<QMatrix4x4> globalMatrices;
    QVector<QMatrix4x4> skinMatrices;
    QVector<JointDiagnostic> joints;
    bool coupledApproximation{false};
};

struct PoseInputError {
    QString code;
    QString message;
    QString detail;
};

}
