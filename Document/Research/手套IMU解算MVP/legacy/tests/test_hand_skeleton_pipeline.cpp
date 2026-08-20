#include "hand_skeleton_pipeline.h"

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
            bone.bindLocal.translate(float(index), 0.0F, 0.0F);
        }
        const QMatrix4x4 globalBind = bone.parentIndex < 0
            ? bone.bindLocal
            : binding.bones[0].bindLocal * bone.bindLocal;
        bone.inverseBind = globalBind.inverted();
        binding.bones.push_back(bone);
    }
    return binding;
}

RigConfig syntheticConfig()
{
    RigConfig config;
    config.rootBone = "root";
    config.palmBone = "root";
    const std::array<QString, 5> names{"thumb", "index", "middle", "ring", "little"};
    for (int index = 0; index < 5; ++index) {
        JointConfig joint;
        joint.boneName = names[size_t(index)];
        joint.displayName = joint.boneName;
        joint.flexionAxis = QVector3D(1.0F, 0.0F, 0.0F);
        joint.abductionAxis = QVector3D(0.0F, 1.0F, 0.0F);
        joint.limits = {{0.0F, -10.0F, 0.0F}, {90.0F, 10.0F, 0.0F}};
        joint.lockedAxes = QVector3D(0.0F, 0.0F, 1.0F);
        joint.editable = true;
        joint.coupling = 1.0F;
        config.joints.insert(joint.boneName, joint);

        FingerConfig finger;
        finger.name = names[size_t(index)];
        finger.slot = static_cast<ImuSlot>(index + 1);
        finger.bones = {names[size_t(index)]};
        finger.sensorFlexionAxis = QVector3D(1.0F, 0.0F, 0.0F);
        finger.sensorAbductionAxis = QVector3D(0.0F, 1.0F, 0.0F);
        finger.sensorMinDegrees = 0.0F;
        finger.sensorMaxDegrees = 90.0F;
        finger.sensorAbductionMinDegrees = -10.0F;
        finger.sensorAbductionMaxDegrees = 10.0F;
        finger.sensorCorrection = QQuaternion();
        config.fingers[size_t(index)] = finger;
    }
    return config;
}

SixImuSnapshot snapshot(qint64 timestampNs, float indexFlexion = 0.0F)
{
    SixImuSnapshot result;
    result.sequence = quint8(timestampNs / 1'000'000);
    result.updatedMonotonicNs = timestampNs;
    const QQuaternion palm = QQuaternion::fromAxisAndAngle(0.0F, 1.0F, 0.0F, 25.0F);
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        SensorPose &pose = result.poses[size_t(index)];
        pose.sensorId = static_cast<SensorId>(static_cast<quint8>(SensorId::Wrist) + index);
        pose.worldOrientation = palm;
        pose.valid = true;
        pose.updatedMonotonicNs = timestampNs;
    }
    result.poses[2].worldOrientation = palm
        * QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, indexFlexion);
    return result;
}

int boneIndex(const HandSkeletonFrame &frame, const QString &name)
{
    for (int index = 0; index < frame.bones.size(); ++index) {
        if (frame.bones[index].boneName == name) {
            return index;
        }
    }
    return -1;
}

} // namespace

class HandSkeletonPipelineTest final : public QObject {
    Q_OBJECT

private slots:
    void producesStableBoneContract();
    void removesPalmWorldRotation();
    void invalidFingerHoldsPreviousPose();
    void timestampRegressionHoldsWholeFrame();
    void resetAcceptsEarlierTimestamp();
};

void HandSkeletonPipelineTest::producesStableBoneContract()
{
    HandSkeleton::Pipeline pipeline(syntheticBinding(), syntheticConfig());
    const HandSkeletonFrame frame = pipeline.process(snapshot(1'000'000, 45.0F));
    QVERIFY(frame.frameApplied);
    QVERIFY(frame.coupledApproximation);
    QCOMPARE(frame.bones.size(), 6);
    QCOMPARE(frame.bones[0].parentIndex, -1);
    QCOMPARE(frame.bones[2].parentIndex, 0);
    QCOMPARE(frame.bones[2].source, HandBoneSource::Estimated);
    QCOMPARE(frame.bones[2].confidence, 1.0F);
    QVERIFY(qAbs(frame.bones[2].localRotation.lengthSquared() - 1.0F) < 1.0e-6F);
    QCOMPARE(frame.bones[2].bindTranslation, QVector3D(2.0F, 0.0F, 0.0F));
}

void HandSkeletonPipelineTest::removesPalmWorldRotation()
{
    HandSkeleton::Pipeline pipeline(syntheticBinding(), syntheticConfig());
    const HandSkeletonFrame frame = pipeline.process(snapshot(2'000'000, 45.0F));
    const int index = boneIndex(frame, "index");
    QVERIFY(index >= 0);
    const QVector3D euler = frame.bones[index].localRotation.toEulerAngles();
    QVERIFY(qAbs(euler.x() - 45.0F) < 0.05F);
    QVERIFY(qAbs(euler.y()) < 0.05F);
}

void HandSkeletonPipelineTest::invalidFingerHoldsPreviousPose()
{
    HandSkeleton::Pipeline pipeline(syntheticBinding(), syntheticConfig());
    const HandSkeletonFrame first = pipeline.process(snapshot(3'000'000, 60.0F));
    SixImuSnapshot invalid = snapshot(4'000'000, 0.0F);
    invalid.poses[2].valid = false;
    const HandSkeletonFrame held = pipeline.process(invalid);
    const int index = boneIndex(held, "index");
    QVERIFY(held.frameApplied);
    QVERIFY(!held.fingerValid[1]);
    QVERIFY(!held.diagnostics.isEmpty());
    QCOMPARE(held.bones[index].localRotation, first.bones[index].localRotation);
}

void HandSkeletonPipelineTest::timestampRegressionHoldsWholeFrame()
{
    HandSkeleton::Pipeline pipeline(syntheticBinding(), syntheticConfig());
    const HandSkeletonFrame first = pipeline.process(snapshot(6'000'000, 30.0F));
    const HandSkeletonFrame old = pipeline.process(snapshot(5'000'000, 80.0F));
    QVERIFY(!old.frameApplied);
    QCOMPARE(old.bones[2].source, HandBoneSource::Held);
    QCOMPARE(old.bones[2].localRotation, first.bones[2].localRotation);
    QCOMPARE(old.diagnostics.front().code, QString("timestamp_regression"));
}

void HandSkeletonPipelineTest::resetAcceptsEarlierTimestamp()
{
    HandSkeleton::Pipeline pipeline(syntheticBinding(), syntheticConfig());
    pipeline.process(snapshot(9'000'000, 30.0F));
    pipeline.reset();
    const HandSkeletonFrame afterReset = pipeline.process(snapshot(1'000'000, 10.0F));
    QVERIFY(afterReset.frameApplied);
    QVERIFY(afterReset.diagnostics.isEmpty());
}

QTEST_APPLESS_MAIN(HandSkeletonPipelineTest)
#include "test_hand_skeleton_pipeline.moc"
