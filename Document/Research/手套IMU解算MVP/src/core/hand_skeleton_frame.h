#pragma once

#include "diagnostic.h"
#include "hand_observation_types.h"

#include <QMatrix4x4>
#include <QMetaType>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector3D>

namespace handstudio {

enum class BoneSource { Measured, Estimated, Held, Recovered, Invalid };

struct HandBoneFrame {
    QString boneName;
    int parentIndex = -1;
    QMatrix4x4 bindLocalMatrix;
    QVector3D localTranslation;
    QQuaternion localRotation;
    QVector3D localScale{1.0F, 1.0F, 1.0F};
    QMatrix4x4 localMatrix;
    QMatrix4x4 globalMatrix;
    QMatrix4x4 skinMatrix;
    bool valid = false;
    float confidence = 0.0F;
    BoneSource source = BoneSource::Invalid;
};

struct HandSkeletonFrame {
    quint8 sequence = 0;
    qint64 timestampNs = 0;
    HandSide handSide = HandSide::Left;
    QString skeletonId;
    QMatrix4x4 rootTransform;
    QVector<HandBoneFrame> bones;
    bool coupledApproximation = false;
    QVector<Diagnostic> diagnostics;
};

}

Q_DECLARE_METATYPE(handstudio::BoneSource)
Q_DECLARE_METATYPE(handstudio::HandBoneFrame)
Q_DECLARE_METATYPE(handstudio::HandSkeletonFrame)
