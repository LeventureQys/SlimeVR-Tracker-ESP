#include "skeleton/kinematic_skeleton.h"

#include "fusion/quaternion_util.h"
#include "model/standard_joints.h"
#include "skeleton/skin_palette_mapper.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace handstudio {
namespace {

bool finite(float value) { return std::isfinite(double(value)); }
bool finiteVector(const QVector3D &value) { return finite(value.x()) && finite(value.y()) && finite(value.z()); }

std::optional<QVector3D> vector3(const QJsonValue &value)
{
    if (!value.isArray() || value.toArray().size() != 3) return std::nullopt;
    const QJsonArray array = value.toArray();
    QVector3D result(float(array[0].toDouble()), float(array[1].toDouble()), float(array[2].toDouble()));
    return finiteVector(result) ? std::optional<QVector3D>(result) : std::nullopt;
}

std::optional<QQuaternion> quaternion(const QJsonValue &value)
{
    if (!value.isArray() || value.toArray().size() != 4) return std::nullopt;
    const QJsonArray array = value.toArray();
    return normalizedQuaternion(QQuaternion(float(array[0].toDouble()), float(array[1].toDouble()),
                                            float(array[2].toDouble()), float(array[3].toDouble())));
}

QQuaternion jointRotation(const JointMotionConfig &joint, const QVector3D &degrees)
{
    return (QQuaternion::fromAxisAndAngle(joint.flexionAxis, degrees.x())
            * QQuaternion::fromAxisAndAngle(joint.abductionAxis, degrees.y())
            * QQuaternion::fromAxisAndAngle(joint.twistAxis, degrees.z())).normalized();
}

QVector3D clampedDegrees(const JointMotionConfig &joint, const QVector3D &requested)
{
    QVector3D result;
    for (int axis = 0; axis < 3; ++axis) {
        const float value = joint.lockedAxes[axis] >= 0.5F ? 0.0F
            : std::clamp(requested[axis] * joint.coupling[axis], joint.minimumDegrees[axis],
                         joint.maximumDegrees[axis]);
        result[axis] = value;
    }
    return result;
}

}

HandRigLoadResult loadHandRigConfig(const QByteArray &json, const RiggedModel &model)
{
    HandRigLoadResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errors.append(QStringLiteral("invalid JSON root"));
        return result;
    }
    const QJsonObject root = document.object();
    HandRigConfig config;
    config.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
    config.skeletonId = root.value(QStringLiteral("skeletonId")).toString();
    config.rootName = root.value(QStringLiteral("rootName")).toString();
    config.handSide = root.value(QStringLiteral("handSide")).toString() == QStringLiteral("left")
                          ? HandSide::Left : HandSide::Right;
    if (config.schemaVersion != 1 || config.skeletonId.isEmpty() || config.rootName.isEmpty()) {
        result.errors.append(QStringLiteral("missing schemaVersion, skeletonId or rootName"));
    }

    QSet<QString> modelNames;
    for (const auto &bone : model.bones) {
        if (bone.name.isEmpty() || modelNames.contains(bone.name)) {
            result.errors.append(QStringLiteral("model has empty or duplicate bone name"));
        }
        modelNames.insert(bone.name);
    }

    const QJsonArray joints = root.value(QStringLiteral("joints")).toArray();
    QSet<QString> jointNames;
    for (int index = 0; index < joints.size(); ++index) {
        const QJsonObject object = joints[index].toObject();
        JointMotionConfig joint;
        joint.name = object.value(QStringLiteral("name")).toString();
        joint.parentName = object.value(QStringLiteral("parentName")).toString();
        joint.fingerIndex = object.value(QStringLiteral("fingerIndex")).toInt(-1);
        const auto flexion = vector3(object.value(QStringLiteral("flexionAxis")));
        const auto abduction = vector3(object.value(QStringLiteral("abductionAxis")));
        const auto twist = vector3(object.value(QStringLiteral("twistAxis")));
        const auto minimum = vector3(object.value(QStringLiteral("minimumDegrees")));
        const auto maximum = vector3(object.value(QStringLiteral("maximumDegrees")));
        const auto locked = vector3(object.value(QStringLiteral("lockedAxes")));
        const auto coupling = vector3(object.value(QStringLiteral("coupling")));
        if (joint.name.isEmpty() || jointNames.contains(joint.name) || !modelNames.contains(joint.name)
            || !flexion || !abduction || !twist || !minimum || !maximum || !locked || !coupling
            || flexion->lengthSquared() < 1.0e-8F || abduction->lengthSquared() < 1.0e-8F
            || twist->lengthSquared() < 1.0e-8F) {
            result.errors.append(QStringLiteral("invalid joint at index %1").arg(index));
            continue;
        }
        joint.flexionAxis = flexion->normalized();
        joint.abductionAxis = abduction->normalized();
        joint.twistAxis = twist->normalized();
        joint.minimumDegrees = *minimum;
        joint.maximumDegrees = *maximum;
        joint.lockedAxes = *locked;
        joint.coupling = *coupling;
        for (int axis = 0; axis < 3; ++axis) {
            if (joint.minimumDegrees[axis] > joint.maximumDegrees[axis] || joint.coupling[axis] < 0.0F) {
                result.errors.append(QStringLiteral("invalid limit or coupling for %1").arg(joint.name));
            }
        }
        jointNames.insert(joint.name);
        config.joints.append(joint);
    }
    if (jointNames.size() != standardJointNames().size()) {
        result.errors.append(QStringLiteral("configuration must define all 25 standard joints"));
    }
    for (const QString &name : standardJointNames()) {
        if (!jointNames.contains(name)) result.errors.append(QStringLiteral("missing joint: %1").arg(name));
    }
    QSet<QString> seen;
    for (const auto &joint : config.joints) {
        if (!joint.parentName.isEmpty() && !seen.contains(joint.parentName)) {
            result.errors.append(QStringLiteral("parent chain is not continuous for %1").arg(joint.name));
        }
        seen.insert(joint.name);
    }

    const QJsonArray mounts = root.value(QStringLiteral("mountOrientations")).toArray();
    if (mounts.size() != 5) result.errors.append(QStringLiteral("five mount orientations required"));
    for (int index = 0; index < std::min(5, int(mounts.size())); ++index) {
        const auto value = quaternion(mounts[index]);
        if (!value) result.errors.append(QStringLiteral("invalid mount quaternion at %1").arg(index));
        else config.mountOrientations[std::size_t(index)] = *value;
    }
    const QJsonObject missing = root.value(QStringLiteral("missingFrames")).toObject();
    config.missingFrames.heldDurationNs = qint64(missing.value(QStringLiteral("heldDurationMs")).toDouble(150.0) * 1000000.0);
    config.missingFrames.returnDurationNs = qint64(missing.value(QStringLiteral("returnDurationMs")).toDouble(350.0) * 1000000.0);
    config.missingFrames.recoveredDurationNs = qint64(missing.value(QStringLiteral("recoveredDurationMs")).toDouble(200.0) * 1000000.0);
    config.missingFrames.heldConfidencePerSecond = float(missing.value(QStringLiteral("heldConfidencePerSecond")).toDouble(0.8));
    config.missingFrames.recoveryDegreesPerSecond = float(missing.value(QStringLiteral("recoveryDegreesPerSecond")).toDouble(360.0));
    if (config.missingFrames.heldDurationNs < 0 || config.missingFrames.returnDurationNs <= 0
        || config.missingFrames.recoveredDurationNs < 0
        || !finite(config.missingFrames.heldConfidencePerSecond)
        || !finite(config.missingFrames.recoveryDegreesPerSecond)
        || config.missingFrames.recoveryDegreesPerSecond <= 0.0F) {
        result.errors.append(QStringLiteral("invalid missingFrames parameters"));
    }
    if (result.errors.isEmpty()) result.config = config;
    return result;
}

KinematicSkeleton::KinematicSkeleton(RiggedModel model, HandRigConfig config)
    : model_(std::move(model)), config_(std::move(config))
{
    for (int index = 0; index < config_.joints.size(); ++index) {
        jointIndexByName_.insert(config_.joints[index].name, index);
        parentIndices_.append(config_.joints[index].parentName.isEmpty()
                                  ? -1 : jointIndexByName_.value(config_.joints[index].parentName, -2));
        if (parentIndices_.last() == -2) {
            error_ = QStringLiteral("invalid parent chain");
            return;
        }
    }
    valid_ = config_.joints.size() == model_.bones.size();
    if (!valid_) error_ = QStringLiteral("joint/model count mismatch");
}

bool KinematicSkeleton::isValid() const noexcept { return valid_; }
QString KinematicSkeleton::errorString() const { return error_; }

HandSkeletonFrame KinematicSkeleton::solve(const HandObservationFrame &observation)
{
    if (!valid_) return {};
    if (observation.wristWorldOrientation.isNull() || !normalizedQuaternion(observation.wristWorldOrientation)) {
        if (!lastFrame_.bones.isEmpty()) {
            lastFrame_.diagnostics.append({DiagnosticSeverity::Error, QStringLiteral("wrist_invalid"),
                                           QStringLiteral("wrist invalid; whole hand frozen"), {},
                                           observation.timestampNs});
        }
        return lastFrame_;
    }
    const qint64 timestamp = observation.timestampNs;
    for (int finger = 0; finger < 5; ++finger) {
        FingerState &state = fingerStates_[std::size_t(finger)];
        const FingerObservation &input = observation.fingers[std::size_t(finger)];
        if (input.valid && finite(input.flexionDegrees) && finite(input.abductionDegrees)
            && finite(input.twistDegrees)) {
            QVector3D target(input.flexionDegrees, input.abductionDegrees, input.twistDegrees);
            if (state.wasHeld && state.hasPose && timestamp > state.lastValidTimestampNs) {
                const float maximumStep = config_.missingFrames.recoveryDegreesPerSecond
                    * float(timestamp - state.lastValidTimestampNs) / 1.0e9F;
                for (int axis = 0; axis < 3; ++axis) {
                    state.degrees[axis] += std::clamp(target[axis] - state.degrees[axis], -maximumStep, maximumStep);
                }
                state.recoveredUntilNs = timestamp + config_.missingFrames.recoveredDurationNs;
            } else state.degrees = target;
            state.lastValidTimestampNs = timestamp;
            state.confidence = std::clamp(input.confidence, 0.0F, 1.0F);
            state.hasPose = true;
            state.wasHeld = false;
        } else if (state.hasPose) {
            const qint64 elapsed = std::max<qint64>(0, timestamp - state.lastValidTimestampNs);
            state.wasHeld = true;
            state.confidence = std::max(0.0F, state.confidence
                - config_.missingFrames.heldConfidencePerSecond * float(elapsed) / 1.0e9F);
            if (elapsed > config_.missingFrames.heldDurationNs) {
                const float progress = std::clamp(float(elapsed - config_.missingFrames.heldDurationNs)
                    / float(config_.missingFrames.returnDurationNs), 0.0F, 1.0F);
                state.degrees *= (1.0F - progress);
            }
        }
    }

    HandSkeletonFrame frame;
    frame.sequence = observation.sequence;
    frame.timestampNs = timestamp;
    frame.handSide = config_.handSide;
    frame.skeletonId = config_.skeletonId;
    frame.rootTransform = computeDisplayRootTransform(model_);
    frame.coupledApproximation = true;
    frame.bones.resize(config_.joints.size());
    for (int index = 0; index < config_.joints.size(); ++index) {
        const JointMotionConfig &joint = config_.joints[index];
        const int paletteIndex = model_.boneIndexByName.value(joint.name, -1);
        HandBoneFrame &bone = frame.bones[index];
        bone.boneName = joint.name;
        bone.parentIndex = parentIndices_[index];
        bone.bindLocalMatrix = model_.bones[paletteIndex].bindLocal;
        bone.localTranslation = model_.bones[paletteIndex].bindTranslation;
        bone.localScale = model_.bones[paletteIndex].bindScale;
        QVector3D degrees;
        BoneSource source = BoneSource::Estimated;
        float confidence = 1.0F;
        if (joint.fingerIndex >= 0) {
            const FingerState &state = fingerStates_[std::size_t(joint.fingerIndex)];
            degrees = clampedDegrees(joint, state.degrees);
            confidence = state.confidence;
            source = state.wasHeld ? BoneSource::Held
                : timestamp <= state.recoveredUntilNs ? BoneSource::Recovered : BoneSource::Estimated;
        }
        bone.localRotation = jointRotation(joint, degrees);
        bone.localMatrix = model_.bones[paletteIndex].bindLocal * quaternionToMatrix(bone.localRotation);
        QMatrix4x4 accumulatedDelta = quaternionToMatrix(bone.localRotation);
        for (int parentIndex = bone.parentIndex; parentIndex >= 0;
             parentIndex = frame.bones[parentIndex].parentIndex) {
            accumulatedDelta = quaternionToMatrix(frame.bones[parentIndex].localRotation) * accumulatedDelta;
        }
        bone.globalMatrix = frame.rootTransform * model_.bones[paletteIndex].bindWorld * accumulatedDelta;
        bone.valid = finiteVector(bone.globalMatrix.column(3).toVector3D());
        bone.confidence = confidence;
        bone.source = bone.valid ? source : BoneSource::Invalid;
    }
    QStringList errors;
    if (!applySkinPalette(model_, frame, &errors)) {
        frame.diagnostics.append({DiagnosticSeverity::Error, QStringLiteral("palette_mapping"),
                                  QStringLiteral("skin palette mapping failed"), errors.join(';'), timestamp});
    }
    lastFrame_ = frame;
    return frame;
}

}

