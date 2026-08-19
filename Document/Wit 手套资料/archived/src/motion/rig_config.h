#pragma once

#include "motion_types.h"

#include <QHash>

namespace handdemo::motion {

struct RigConfigError {
    QString code;
    QString fieldPath;
    QString message;
};

struct JointConfig {
    QString boneName;
    QString displayName;
    QVector3D flexionAxis{0.0F, 0.0F, 1.0F};
    QVector3D abductionAxis{1.0F, 0.0F, 0.0F};
    JointLimit limits;
    QVector3D lockedAxes{0.0F, 0.0F, 1.0F};
    bool editable{false};
    float coupling{1.0F};
};

struct FingerConfig {
    QString name;
    ImuSlot slot{ImuSlot::Thumb};
    QVector<QString> bones;
    QVector3D sensorFlexionAxis{0.0F, 0.0F, 1.0F};
    QVector3D sensorAbductionAxis{1.0F, 0.0F, 0.0F};
    float sensorMinDegrees{0.0F};
    float sensorMaxDegrees{90.0F};
    float sensorAbductionMinDegrees{-12.0F};
    float sensorAbductionMaxDegrees{12.0F};
    QQuaternion sensorCorrection;
};

struct RigConfig {
    QString rootBone;
    QString palmBone;
    QHash<QString, JointConfig> joints;
    std::array<FingerConfig, 5> fingers;
};

struct RigConfigLoadResult {
    std::optional<RigConfig> config;
    QVector<RigConfigError> errors;
    explicit operator bool() const { return config.has_value(); }
};

class RigConfigLoader {
public:
    static RigConfigLoadResult load(const QByteArray &json, const QVector<BoneBinding> &bones);
};

}
