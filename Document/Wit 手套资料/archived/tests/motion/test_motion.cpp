#include "imu_pose.h"
#include "pose_solver.h"
#include "rig_config.h"
#include "single_imu_finger_controller.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace handdemo::motion;

namespace {

SkeletonBinding syntheticBinding()
{
    SkeletonBinding binding;
    binding.globalInverse.setToIdentity();
    const QStringList names{"root", "thumb", "index", "middle", "ring", "little"};
    for (int index = 0; index < names.size(); ++index) {
        BoneBinding bone;
        bone.name = names[index];
        bone.parentIndex = index == 0 ? -1 : 0;
        bone.bindLocal.setToIdentity();
        if (index > 0) {
            bone.bindLocal.translate(static_cast<float>(index), 0.0F, 0.0F);
        }
        QMatrix4x4 globalBind = bone.bindLocal;
        bone.inverseBind = globalBind.inverted();
        binding.bones.push_back(bone);
    }
    return binding;
}

QJsonObject jointObject(float maximum = 90.0F)
{
    return {{"displayName", "joint"}, {"flexionAxis", QJsonArray{1, 0, 0}},
            {"abductionAxis", QJsonArray{0, 1, 0}}, {"minDegrees", QJsonArray{0, -10, 0}},
            {"maxDegrees", QJsonArray{maximum, 10, 0}}, {"lockedAxes", QJsonArray{0, 0, 1}},
            {"editable", true}, {"coupling", 1.0}};
}

QByteArray validJson()
{
    QJsonObject joints;
    QJsonObject fingers;
    const QStringList names{"thumb", "index", "middle", "ring", "little"};
    for (const QString &name : names) {
        joints.insert(name, jointObject());
        fingers.insert(name, QJsonObject{{"bones", QJsonArray{name}},
                                         {"sensorFlexionAxis", QJsonArray{1, 0, 0}},
                                         {"sensorAbductionAxis", QJsonArray{0, 1, 0}},
                                         {"sensorMinDegrees", 0}, {"sensorMaxDegrees", 90},
                                         {"sensorAbductionMinDegrees", -10},
                                         {"sensorAbductionMaxDegrees", 10},
                                         {"sensorCorrection", QJsonArray{1, 0, 0, 0}}});
    }
    return QJsonDocument(QJsonObject{{"rootBone", "root"}, {"palmBone", "root"},
                                     {"joints", joints}, {"fingers", fingers}}).toJson();
}

RigConfig validConfig(const SkeletonBinding &binding)
{
    const RigConfigLoadResult loaded = RigConfigLoader::load(validJson(), binding.bones);
    Q_ASSERT(loaded);
    return *loaded.config;
}

bool hasError(const RigConfigLoadResult &result, const QString &code, const QString &pathPart)
{
    for (const RigConfigError &error : result.errors) {
        if (error.code == code && error.fieldPath.contains(pathPart)) {
            return true;
        }
    }
    return false;
}

HandImuFrame frame(qint64 timestamp, float indexDegrees = 0.0F)
{
    HandImuFrame result;
    for (int index = 0; index < 6; ++index) {
        result.samples[index] = {static_cast<ImuSlot>(index), QQuaternion(), timestamp, true};
    }
    result.samples[static_cast<int>(ImuSlot::Index)].orientation =
        QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, indexDegrees);
    return result;
}

}

class MotionTest : public QObject {
    Q_OBJECT

private slots:
    void configAcceptsValidMapping();
    void configRejectsMissingAndDuplicateBones();
    void configRejectsDiscontinuousChainAndReversedLimit();
    void limitsClampAndLockAxes();
    void couplingMapsCurlAndClampsAgain();
    void couplingUsesConfiguredFlexionAxis();
    void fingerPoseAppliesAbductionOnlyToBase();
    void hierarchyPreservesBindTranslationAndIdentitySkin();
    void rotatedJointPreservesChildBoneLength();
    void quaternionRelativeAndNormalization();
    void invalidAndOutOfOrderInputHoldsPose();
    void deterministicSourceProducesSixtyHertzFrames();
    void singleImuRequiresBindingAndCalibration();
    void singleImuCalibratesArbitraryOrientation();
    void singleImuRoutesOnlySelectedFinger();
};

void MotionTest::configAcceptsValidMapping()
{
    const RigConfigLoadResult result = RigConfigLoader::load(validJson(), syntheticBinding().bones);
    QVERIFY(result);
    QCOMPARE(result.config->fingers[1].slot, ImuSlot::Index);
    QCOMPARE(result.config->fingers[1].bones.front(), QString("index"));
}

void MotionTest::configRejectsMissingAndDuplicateBones()
{
    const SkeletonBinding binding = syntheticBinding();
    QJsonObject root = QJsonDocument::fromJson(validJson()).object();
    QJsonObject fingers = root["fingers"].toObject();
    QJsonObject thumb = fingers["thumb"].toObject();
    thumb["bones"] = QJsonArray{"absent"};
    fingers["thumb"] = thumb;
    QJsonObject index = fingers["index"].toObject();
    index["bones"] = QJsonArray{"middle"};
    fingers["index"] = index;
    root["fingers"] = fingers;
    const RigConfigLoadResult result = RigConfigLoader::load(QJsonDocument(root).toJson(), binding.bones);
    QVERIFY(!result);
    QVERIFY(hasError(result, "missing_bone", "$.fingers.thumb.bones[0]"));
    QVERIFY(hasError(result, "duplicate_bone", "$.fingers.middle.bones[0]"));
}

void MotionTest::configRejectsDiscontinuousChainAndReversedLimit()
{
    SkeletonBinding binding = syntheticBinding();
    BoneBinding child;
    child.name = "thumbChild";
    child.parentIndex = 1;
    child.bindLocal.setToIdentity();
    child.inverseBind.setToIdentity();
    binding.bones.push_back(child);

    QJsonObject root = QJsonDocument::fromJson(validJson()).object();
    QJsonObject joints = root["joints"].toObject();
    joints["thumbChild"] = jointObject();
    QJsonObject thumbJoint = joints["thumb"].toObject();
    thumbJoint["minDegrees"] = QJsonArray{100, -10, 0};
    joints["thumb"] = thumbJoint;
    root["joints"] = joints;
    QJsonObject fingers = root["fingers"].toObject();
    QJsonObject thumb = fingers["thumb"].toObject();
    thumb["bones"] = QJsonArray{"thumb", "index"};
    fingers["thumb"] = thumb;
    root["fingers"] = fingers;
    const RigConfigLoadResult result = RigConfigLoader::load(QJsonDocument(root).toJson(), binding.bones);
    QVERIFY(hasError(result, "reversed_limit", "$.joints.thumb.minDegrees[0]"));
    QVERIFY(hasError(result, "discontinuous_chain", "$.fingers.thumb.bones[1]"));
}

void MotionTest::limitsClampAndLockAxes()
{
    const SkeletonBinding binding = syntheticBinding();
    PoseSolver solver(binding, validConfig(binding));
    const PoseResult result = solver.applyJoint(solver.bindPose(), "index", {120.0F, -20.0F, 30.0F});
    QCOMPARE(result.pose.localPoses[2].eulerDegrees, QVector3D(90.0F, -10.0F, 0.0F));
    QVERIFY(result.joints[2].constrained);

    const PoseResult inside = solver.applyJoint(solver.bindPose(), "index", {30.0F, 5.0F, 0.0F});
    QCOMPARE(inside.pose.localPoses[2].eulerDegrees, QVector3D(30.0F, 5.0F, 0.0F));
    QVERIFY(!inside.joints[2].constrained);
}

void MotionTest::couplingMapsCurlAndClampsAgain()
{
    const SkeletonBinding binding = syntheticBinding();
    RigConfig config = validConfig(binding);
    config.joints["index"].coupling = 0.5F;
    PoseSolver solver(binding, config);
    QCOMPARE(solver.applyFingerCurl(solver.bindPose(), 1, 0.0F).pose.localPoses[2].eulerDegrees.x(), 0.0F);
    QCOMPARE(solver.applyFingerCurl(solver.bindPose(), 1, 0.5F).pose.localPoses[2].eulerDegrees.x(), 22.5F);
    const PoseResult maximum = solver.applyFingerCurl(solver.bindPose(), 1, 3.0F);
    QCOMPARE(maximum.pose.localPoses[2].eulerDegrees.x(), 45.0F);
    QVERIFY(maximum.coupledApproximation);
}

void MotionTest::couplingUsesConfiguredFlexionAxis()
{
    const SkeletonBinding binding = syntheticBinding();
    RigConfig config = validConfig(binding);
    JointConfig &joint = config.joints["index"];
    joint.flexionAxis = QVector3D(0.0F, 0.0F, 1.0F);
    joint.limits.minDegrees = QVector3D(-10.0F, 0.0F, 0.0F);
    joint.limits.maxDegrees = QVector3D(10.0F, 0.0F, 80.0F);
    joint.lockedAxes = QVector3D(0.0F, 1.0F, 0.0F);
    joint.coupling = 0.5F;
    PoseSolver solver(binding, config);
    const PoseResult result = solver.applyFingerCurl(solver.bindPose(), 1, 1.0F);
    QCOMPARE(result.pose.localPoses[2].eulerDegrees, QVector3D(0.0F, 0.0F, 40.0F));
}

void MotionTest::fingerPoseAppliesAbductionOnlyToBase()
{
    SkeletonBinding binding = syntheticBinding();
    BoneBinding distal;
    distal.name = "indexDistal";
    distal.parentIndex = 2;
    distal.bindLocal.setToIdentity();
    distal.bindLocal.translate(0.0F, 1.0F, 0.0F);
    distal.inverseBind.setToIdentity();
    binding.bones.push_back(distal);
    RigConfig config = validConfig(binding);
    config.joints["index"].flexionAxis = QVector3D(0.0F, 0.0F, 1.0F);
    config.joints["index"].abductionAxis = QVector3D(1.0F, 0.0F, 0.0F);
    config.joints["index"].limits = {{-12.0F, 0.0F, 0.0F}, {12.0F, 0.0F, 90.0F}};
    config.joints["index"].lockedAxes = QVector3D(0.0F, 1.0F, 0.0F);
    JointConfig distalConfig = config.joints["index"];
    distalConfig.boneName = "indexDistal";
    distalConfig.limits = {{0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 80.0F}};
    distalConfig.lockedAxes = QVector3D(1.0F, 1.0F, 0.0F);
    config.joints.insert("indexDistal", distalConfig);
    config.fingers[1].bones = {"index", "indexDistal"};
    PoseSolver solver(binding, config);
    const PoseResult result = solver.applyFingerPose(solver.bindPose(), 1, 0.5F, 8.0F);
    QCOMPARE(result.pose.localPoses[2].eulerDegrees, QVector3D(8.0F, 0.0F, 45.0F));
    QCOMPARE(result.pose.localPoses[6].eulerDegrees, QVector3D(0.0F, 0.0F, 40.0F));
}

void MotionTest::hierarchyPreservesBindTranslationAndIdentitySkin()
{
    const SkeletonBinding binding = syntheticBinding();
    PoseSolver solver(binding, validConfig(binding));
    const PoseResult bind = solver.solve(solver.bindPose());
    for (const QMatrix4x4 &matrix : bind.skinMatrices) {
        QVERIFY(qFuzzyCompare(matrix, QMatrix4x4()));
    }
    const QVector3D before = bind.globalMatrices[2].column(3).toVector3D();
    const PoseResult rotated = solver.applyJoint(solver.bindPose(), "index", {45.0F, 0.0F, 0.0F});
    QCOMPARE(rotated.globalMatrices[2].column(3).toVector3D(), before);
}

void MotionTest::rotatedJointPreservesChildBoneLength()
{
    SkeletonBinding binding;
    binding.globalInverse.setToIdentity();
    BoneBinding root;
    root.name = "root";
    root.parentIndex = -1;
    root.bindLocal.setToIdentity();
    root.bindLocal.rotate(35.0F, 0.0F, 0.0F, 1.0F);
    root.inverseBind = root.bindLocal.inverted();
    binding.bones.push_back(root);
    BoneBinding joint;
    joint.name = "thumb";
    joint.parentIndex = 0;
    joint.bindLocal.setToIdentity();
    joint.bindLocal.translate(0.0F, 2.0F, 0.0F);
    joint.bindLocal.rotate(20.0F, 0.0F, 1.0F, 0.0F);
    joint.inverseBind = (root.bindLocal * joint.bindLocal).inverted();
    binding.bones.push_back(joint);
    const QStringList remaining{"index", "middle", "ring", "little"};
    for (const QString &name : remaining) {
        BoneBinding bone;
        bone.name = name;
        bone.parentIndex = 0;
        bone.bindLocal.setToIdentity();
        bone.bindLocal.translate(1.0F, 0.0F, 0.0F);
        bone.inverseBind = (root.bindLocal * bone.bindLocal).inverted();
        binding.bones.push_back(bone);
    }
    PoseSolver solver(binding, validConfig(binding));
    const PoseResult bind = solver.solve(solver.bindPose());
    const PoseResult rotated = solver.applyJoint(solver.bindPose(), "thumb", {60.0F, 0.0F, 0.0F});
    const QVector3D bindRoot = bind.globalMatrices[0].column(3).toVector3D();
    const QVector3D bindJoint = bind.globalMatrices[1].column(3).toVector3D();
    const QVector3D rotatedRoot = rotated.globalMatrices[0].column(3).toVector3D();
    const QVector3D rotatedJoint = rotated.globalMatrices[1].column(3).toVector3D();
    QVERIFY(qAbs((bindJoint - bindRoot).length() - (rotatedJoint - rotatedRoot).length()) < 1.0e-5F);
    QVERIFY(qIsFinite(rotatedJoint.x()));
    QVERIFY(qIsFinite(rotatedJoint.y()));
    QVERIFY(qIsFinite(rotatedJoint.z()));
}

void MotionTest::quaternionRelativeAndNormalization()
{
    const SkeletonBinding binding = syntheticBinding();
    PoseSolver solver(binding, validConfig(binding));
    ImuPoseMapper mapper(solver);
    HandImuFrame input = frame(100, 45.0F);
    input.samples[0].orientation = QQuaternion::fromAxisAndAngle(0.0F, 1.0F, 0.0F, 20.0F);
    input.samples[2].orientation = input.samples[0].orientation
                                   * QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, 45.0F);
    input.samples[2].orientation *= 3.0F;
    const ImuMappingResult result = mapper.update(input);
    QVERIFY(result.frameApplied);
    QVERIFY(qAbs(result.pose.pose.localPoses[2].eulerDegrees.x() - 45.0F) < 0.01F);

    input = frame(200);
    input.samples[0].orientation = QQuaternion::fromAxisAndAngle(0.0F, 1.0F, 0.0F, 20.0F);
    input.samples[2].orientation = input.samples[0].orientation;
    const ImuMappingResult identity = mapper.update(input);
    QVERIFY(qAbs(identity.pose.pose.localPoses[2].eulerDegrees.x()) < 0.01F);
}

void MotionTest::invalidAndOutOfOrderInputHoldsPose()
{
    const SkeletonBinding binding = syntheticBinding();
    PoseSolver solver(binding, validConfig(binding));
    ImuPoseMapper mapper(solver);
    const ImuMappingResult initial = mapper.update(frame(100, 60.0F));
    const float held = initial.pose.pose.localPoses[2].eulerDegrees.x();

    HandImuFrame invalidFinger = frame(200, 0.0F);
    invalidFinger.samples[2].orientation = QQuaternion(0, 0, 0, 0);
    const ImuMappingResult fingerResult = mapper.update(invalidFinger);
    QCOMPARE(fingerResult.pose.pose.localPoses[2].eulerDegrees.x(), held);
    QVERIFY(!fingerResult.errors.isEmpty());

    HandImuFrame invalidPalm = frame(300, 0.0F);
    invalidPalm.samples[0].valid = false;
    const ImuMappingResult palmResult = mapper.update(invalidPalm);
    QVERIFY(!palmResult.frameApplied);
    QCOMPARE(palmResult.pose.pose.localPoses[2].eulerDegrees.x(), held);

    const ImuMappingResult old = mapper.update(frame(150, 0.0F));
    QVERIFY(!old.frameApplied);
    QCOMPARE(old.errors.front().code, QString("timestamp_regression"));
    QCOMPARE(old.pose.pose.localPoses[2].eulerDegrees.x(), held);
}

void MotionTest::deterministicSourceProducesSixtyHertzFrames()
{
    DeterministicHandPoseSource first(500);
    DeterministicHandPoseSource second(500);
    const HandImuFrame firstFrame = *first.poll();
    const HandImuFrame secondFrame = *first.poll();
    const HandImuFrame repeatedFrame = *second.poll();
    QCOMPARE(firstFrame.samples[0].timestampUsec, 500);
    QCOMPARE(secondFrame.samples[0].timestampUsec - firstFrame.samples[0].timestampUsec, 16667);
    QCOMPARE(firstFrame.samples[4].orientation, repeatedFrame.samples[4].orientation);
    first.setValid(ImuSlot::Ring, false);
    QVERIFY(!first.poll()->samples[static_cast<int>(ImuSlot::Ring)].valid);
}

void MotionTest::singleImuRequiresBindingAndCalibration()
{
    const SkeletonBinding binding = syntheticBinding();
    PoseSolver solver(binding, validConfig(binding));
    SingleImuFingerController controller(solver);
    QVERIFY(!controller.setDriving(true));
    controller.setConnected(true);
    controller.bindFinger(1);
    QVERIFY(!controller.setDriving(true));
    QVERIFY(controller.calibrate(QQuaternion(), 100));
    QVERIFY(controller.setDriving(true));
    QCOMPARE(controller.state(), SingleImuDriveState::Driving);
    controller.bindFinger(2);
    QCOMPARE(controller.state(), SingleImuDriveState::BoundUncalibrated);
}

void MotionTest::singleImuCalibratesArbitraryOrientation()
{
    const SkeletonBinding binding = syntheticBinding();
    PoseSolver solver(binding, validConfig(binding));
    SingleImuFingerController controller(solver);
    controller.setConnected(true);
    controller.bindFinger(1);
    const QQuaternion zero = QQuaternion::fromEulerAngles(25.0F, -30.0F, 70.0F);
    QVERIFY(controller.calibrate(zero, 100));
    QVERIFY(controller.setDriving(true));
    const SingleImuMappingOutput output = controller.update(zero, 101);
    QVERIFY(output.frameApplied);
    QVERIFY(qAbs(output.curl) < 0.001F);
    QVERIFY(qAbs(output.pose.pose.localPoses[2].eulerDegrees.x()) < 0.001F);
}

void MotionTest::singleImuRoutesOnlySelectedFinger()
{
    const SkeletonBinding binding = syntheticBinding();
    PoseSolver solver(binding, validConfig(binding));
    for (int finger = 0; finger < 5; ++finger) {
        SingleImuFingerController controller(solver);
        controller.setConnected(true);
        controller.bindFinger(finger);
        QVERIFY(controller.calibrate(QQuaternion(), 100));
        QVERIFY(controller.setDriving(true));
        const auto output = controller.update(QQuaternion::fromAxisAndAngle(1, 0, 0, 45), 101);
        QVERIFY(output.frameApplied);
        for (int index = 0; index < 5; ++index) {
            const float angle = output.pose.pose.localPoses[index + 1].eulerDegrees.x();
            if (index == finger) QVERIFY(angle > 0.0F); else QCOMPARE(angle, 0.0F);
        }
    }
}

QTEST_APPLESS_MAIN(MotionTest)
#include "test_motion.moc"
