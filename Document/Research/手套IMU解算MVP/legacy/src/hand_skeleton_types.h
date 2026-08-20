#pragma once

#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QVector>
#include <QVector3D>

#include <array>

enum class HandBoneSource {
    Estimated,
    Held,
    Invalid
};

struct HandBoneFrame {
    QString boneName;
    int parentIndex = -1;
    QVector3D bindTranslation;
    QQuaternion localRotation;
    QMatrix4x4 localMatrix;
    QMatrix4x4 globalMatrix;
    QMatrix4x4 skinMatrix;
    HandBoneSource source = HandBoneSource::Invalid;
    bool constrained = false;
    float confidence = 0.0F;
};

struct HandSkeletonDiagnostic {
    QString code;
    QString message;
    QString detail;
};

struct HandSkeletonFrame {
    quint8 sequence = 0;
    qint64 timestampNs = 0;
    QVector<HandBoneFrame> bones;
    std::array<bool, 5> fingerValid{false, false, false, false, false};
    QVector<HandSkeletonDiagnostic> diagnostics;
    bool frameApplied = false;
    bool coupledApproximation = true;
};
