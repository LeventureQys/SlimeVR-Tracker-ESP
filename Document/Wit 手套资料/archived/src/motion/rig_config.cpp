#include "rig_config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <cmath>

namespace handdemo::motion {
namespace {

const std::array<QString, 5> fingerNames{"thumb", "index", "middle", "ring", "little"};
const std::array<ImuSlot, 5> fingerSlots{ImuSlot::Thumb, ImuSlot::Index, ImuSlot::Middle,
                                        ImuSlot::Ring, ImuSlot::Little};

void addError(QVector<RigConfigError> &errors, const QString &code, const QString &path,
              const QString &message)
{
    errors.push_back({code, path, message});
}

std::optional<QVector3D> readVector(const QJsonValue &value, const QString &path,
                                    QVector<RigConfigError> &errors)
{
    if (!value.isArray() || value.toArray().size() != 3) {
        addError(errors, "invalid_vector", path, QStringLiteral("必须是三个有限数字组成的数组"));
        return std::nullopt;
    }
    const QJsonArray values = value.toArray();
    QVector3D result;
    for (int index = 0; index < 3; ++index) {
        if (!values[index].isDouble() || !std::isfinite(values[index].toDouble())) {
            addError(errors, "invalid_vector", path + QStringLiteral("[%1]").arg(index),
                     QStringLiteral("必须是有限数字"));
            return std::nullopt;
        }
        result[index] = static_cast<float>(values[index].toDouble());
    }
    return result;
}

std::optional<QQuaternion> readQuaternion(const QJsonValue &value, const QString &path,
                                          QVector<RigConfigError> &errors)
{
    if (!value.isArray() || value.toArray().size() != 4) {
        addError(errors, "invalid_quaternion", path, QStringLiteral("必须按 [w,x,y,z] 提供四个数字"));
        return std::nullopt;
    }
    const QJsonArray values = value.toArray();
    for (int index = 0; index < 4; ++index) {
        if (!values[index].isDouble() || !std::isfinite(values[index].toDouble())) {
            addError(errors, "invalid_quaternion", path + QStringLiteral("[%1]").arg(index),
                     QStringLiteral("必须是有限数字"));
            return std::nullopt;
        }
    }
    QQuaternion quaternion(static_cast<float>(values[0].toDouble()),
                           static_cast<float>(values[1].toDouble()),
                           static_cast<float>(values[2].toDouble()),
                           static_cast<float>(values[3].toDouble()));
    if (quaternion.lengthSquared() < 1.0e-12F) {
        addError(errors, "invalid_quaternion", path, QStringLiteral("四元数模长不能为零"));
        return std::nullopt;
    }
    return quaternion.normalized();
}

int boneIndex(const QVector<BoneBinding> &bones, const QString &name)
{
    for (int index = 0; index < bones.size(); ++index) {
        if (bones[index].name == name) {
            return index;
        }
    }
    return -1;
}

}

RigConfigLoadResult RigConfigLoader::load(const QByteArray &json, const QVector<BoneBinding> &bones)
{
    RigConfigLoadResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result.errors, "json_parse_error", "$", parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    RigConfig config;
    config.rootBone = root.value("rootBone").toString();
    config.palmBone = root.value("palmBone").toString();
    if (boneIndex(bones, config.rootBone) < 0) {
        addError(result.errors, "missing_bone", "$.rootBone", QStringLiteral("引用的骨骼不存在"));
    }
    if (boneIndex(bones, config.palmBone) < 0) {
        addError(result.errors, "missing_bone", "$.palmBone", QStringLiteral("引用的骨骼不存在"));
    }

    const QJsonObject joints = root.value("joints").toObject();
    for (auto iterator = joints.constBegin(); iterator != joints.constEnd(); ++iterator) {
        const QString path = QStringLiteral("$.joints.%1").arg(iterator.key());
        const QJsonObject object = iterator.value().toObject();
        JointConfig joint;
        joint.boneName = iterator.key();
        joint.displayName = object.value("displayName").toString(iterator.key());
        const auto flexionAxis = readVector(object.value("flexionAxis"), path + ".flexionAxis", result.errors);
        const auto abductionAxis = readVector(object.value("abductionAxis"), path + ".abductionAxis", result.errors);
        const auto minimum = readVector(object.value("minDegrees"), path + ".minDegrees", result.errors);
        const auto maximum = readVector(object.value("maxDegrees"), path + ".maxDegrees", result.errors);
        const auto locked = readVector(object.value("lockedAxes"), path + ".lockedAxes", result.errors);
        if (flexionAxis && flexionAxis->lengthSquared() < 1.0e-12F) {
            addError(result.errors, "invalid_axis", path + ".flexionAxis", QStringLiteral("轴向量不能为零"));
        }
        if (abductionAxis && abductionAxis->lengthSquared() < 1.0e-12F) {
            addError(result.errors, "invalid_axis", path + ".abductionAxis", QStringLiteral("轴向量不能为零"));
        }
        if (minimum && maximum) {
            for (int axis = 0; axis < 3; ++axis) {
                if ((*minimum)[axis] > (*maximum)[axis]) {
                    addError(result.errors, "reversed_limit",
                             path + QStringLiteral(".minDegrees[%1]").arg(axis),
                             QStringLiteral("下限不能大于上限"));
                }
            }
        }
        joint.editable = object.value("editable").toBool(false);
        joint.coupling = static_cast<float>(object.value("coupling").toDouble(1.0));
        if (!std::isfinite(joint.coupling) || joint.coupling < 0.0F) {
            addError(result.errors, "invalid_coupling", path + ".coupling", QStringLiteral("必须是非负有限数字"));
        }
        if (boneIndex(bones, joint.boneName) < 0) {
            addError(result.errors, "missing_bone", path, QStringLiteral("引用的骨骼不存在"));
        }
        if (flexionAxis && abductionAxis && minimum && maximum && locked) {
            joint.flexionAxis = flexionAxis->normalized();
            joint.abductionAxis = abductionAxis->normalized();
            joint.limits = {*minimum, *maximum};
            joint.lockedAxes = *locked;
            config.joints.insert(joint.boneName, joint);
        }
    }

    const QJsonObject fingers = root.value("fingers").toObject();
    QSet<QString> usedBones;
    for (int fingerIndex = 0; fingerIndex < 5; ++fingerIndex) {
        const QString name = fingerNames[fingerIndex];
        const QString path = QStringLiteral("$.fingers.%1").arg(name);
        const QJsonObject object = fingers.value(name).toObject();
        FingerConfig finger;
        finger.name = name;
        finger.slot = fingerSlots[fingerIndex];
        const QJsonArray chain = object.value("bones").toArray();
        if (chain.isEmpty()) {
            addError(result.errors, "empty_chain", path + ".bones", QStringLiteral("骨骼链不能为空"));
        }
        int previousIndex = -1;
        for (int chainIndex = 0; chainIndex < chain.size(); ++chainIndex) {
            const QString chainPath = path + QStringLiteral(".bones[%1]").arg(chainIndex);
            const QString nameValue = chain[chainIndex].toString();
            const int currentIndex = boneIndex(bones, nameValue);
            if (currentIndex < 0) {
                addError(result.errors, "missing_bone", chainPath, QStringLiteral("引用的骨骼不存在"));
            } else if (usedBones.contains(nameValue)) {
                addError(result.errors, "duplicate_bone", chainPath, QStringLiteral("骨骼已被其他链或本链使用"));
            } else if (previousIndex >= 0 && bones[currentIndex].parentIndex != previousIndex) {
                addError(result.errors, "discontinuous_chain", chainPath, QStringLiteral("骨骼不是前一骨骼的直接子节点"));
            }
            if (!config.joints.contains(nameValue)) {
                addError(result.errors, "missing_joint_config", chainPath, QStringLiteral("骨骼缺少 joints 配置"));
            }
            usedBones.insert(nameValue);
            finger.bones.push_back(nameValue);
            previousIndex = currentIndex;
        }
        const auto sensorAxis = readVector(object.value("sensorFlexionAxis"), path + ".sensorFlexionAxis", result.errors);
        if (sensorAxis && sensorAxis->lengthSquared() < 1.0e-12F) {
            addError(result.errors, "invalid_axis", path + ".sensorFlexionAxis", QStringLiteral("轴向量不能为零"));
        } else if (sensorAxis) {
            finger.sensorFlexionAxis = sensorAxis->normalized();
        }
        const auto sensorAbductionAxis = readVector(object.value("sensorAbductionAxis"),
                                                     path + ".sensorAbductionAxis", result.errors);
        if (sensorAbductionAxis && sensorAbductionAxis->lengthSquared() < 1.0e-12F) {
            addError(result.errors, "invalid_axis", path + ".sensorAbductionAxis",
                     QStringLiteral("轴向量不能为零"));
        } else if (sensorAbductionAxis) {
            finger.sensorAbductionAxis = sensorAbductionAxis->normalized();
        }
        finger.sensorMinDegrees = static_cast<float>(object.value("sensorMinDegrees").toDouble());
        finger.sensorMaxDegrees = static_cast<float>(object.value("sensorMaxDegrees").toDouble());
        if (!std::isfinite(finger.sensorMinDegrees) || !std::isfinite(finger.sensorMaxDegrees)
            || finger.sensorMinDegrees >= finger.sensorMaxDegrees) {
            addError(result.errors, "invalid_sensor_range", path + ".sensorMinDegrees",
                     QStringLiteral("传感器角度下限必须小于上限"));
        }
        finger.sensorAbductionMinDegrees = static_cast<float>(
            object.value("sensorAbductionMinDegrees").toDouble(-12.0));
        finger.sensorAbductionMaxDegrees = static_cast<float>(
            object.value("sensorAbductionMaxDegrees").toDouble(12.0));
        if (!std::isfinite(finger.sensorAbductionMinDegrees)
            || !std::isfinite(finger.sensorAbductionMaxDegrees)
            || finger.sensorAbductionMinDegrees >= finger.sensorAbductionMaxDegrees) {
            addError(result.errors, "invalid_sensor_range", path + ".sensorAbductionMinDegrees",
                     QStringLiteral("传感器张合角度下限必须小于上限"));
        }
        const auto correction = readQuaternion(object.value("sensorCorrection"), path + ".sensorCorrection", result.errors);
        if (correction) {
            finger.sensorCorrection = *correction;
        }
        config.fingers[fingerIndex] = finger;
    }

    if (result.errors.isEmpty()) {
        result.config = config;
    }
    return result;
}

}
