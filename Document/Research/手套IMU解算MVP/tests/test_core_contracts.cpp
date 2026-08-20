#include <QtTest>

#include "core/calibrated_types.h"
#include "core/hand_skeleton_frame.h"
#include "core/imu_frames.h"
#include "core/metatype_registration.h"

class CoreContractsTest final : public QObject {
    Q_OBJECT

private slots:
    void sensorConversions();
    void invalidSensorConversions();
    void defaultMathValues();
    void metaTypesAreRegistered();
};

void CoreContractsTest::sensorConversions()
{
    for (int index = 0; index < 6; ++index) {
        const auto sensorId = handstudio::sensorIdFromIndex(index);
        QVERIFY(sensorId.has_value());
        QCOMPARE(handstudio::sensorIndex(*sensorId), std::optional<int>(index));
        QCOMPARE(handstudio::sensorIdFromAddress(handstudio::sensorAddress(*sensorId)), sensorId);
    }
}

void CoreContractsTest::invalidSensorConversions()
{
    QVERIFY(!handstudio::sensorIdFromAddress(0x4F).has_value());
    QVERIFY(!handstudio::sensorIdFromAddress(0x56).has_value());
    QVERIFY(!handstudio::sensorIdFromIndex(-1).has_value());
    QVERIFY(!handstudio::sensorIdFromIndex(6).has_value());
    QVERIFY(!handstudio::sensorIndex(static_cast<handstudio::SensorId>(0x7F)).has_value());
}

void CoreContractsTest::defaultMathValues()
{
    const handstudio::FusedImuPose pose;
    QVERIFY(qFuzzyCompare(pose.worldOrientation.length(), 1.0F));
    const handstudio::FingerObservation observation;
    QVERIFY(qFuzzyCompare(observation.worldOrientation.length(), 1.0F));
    QVERIFY(qFuzzyCompare(observation.palmRelativeOrientation.length(), 1.0F));
    QVERIFY(qFuzzyCompare(observation.mountCorrectedOrientation.length(), 1.0F));
    const handstudio::HandBoneFrame bone;
    QCOMPARE(bone.localScale, QVector3D(1.0F, 1.0F, 1.0F));
    QVERIFY(bone.bindLocalMatrix.isIdentity());
    QVERIFY(bone.localMatrix.isIdentity());
    QVERIFY(bone.globalMatrix.isIdentity());
    QVERIFY(bone.skinMatrix.isIdentity());
}

void CoreContractsTest::metaTypesAreRegistered()
{
    handstudio::registerCoreMetaTypes();
    QVERIFY(QMetaType::fromType<handstudio::RawImuFrame>().isRegistered());
    QVERIFY(QMetaType::fromType<handstudio::SixImuSampleGroup>().isRegistered());
    QVERIFY(QMetaType::fromType<handstudio::CalibratedImuSample>().isRegistered());
    QVERIFY(QMetaType::fromType<handstudio::FusedImuPose>().isRegistered());
    QVERIFY(QMetaType::fromType<handstudio::HandObservationFrame>().isRegistered());
    QVERIFY(QMetaType::fromType<handstudio::HandSkeletonFrame>().isRegistered());
    QVERIFY(QMetaType::fromType<handstudio::Diagnostic>().isRegistered());
}

QTEST_APPLESS_MAIN(CoreContractsTest)
#include "test_core_contracts.moc"
