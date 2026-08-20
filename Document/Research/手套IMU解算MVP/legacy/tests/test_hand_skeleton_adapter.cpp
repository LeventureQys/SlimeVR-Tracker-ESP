#include "hand_skeleton_adapter.h"

#include <QtTest>

#include <limits>

using namespace handdemo::motion;

namespace {

SixImuSnapshot validSnapshot()
{
    SixImuSnapshot snapshot;
    snapshot.sequence = 42;
    snapshot.updatedMonotonicNs = 5'000'000;
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        SensorPose &pose = snapshot.poses[size_t(index)];
        pose.sensorId = static_cast<SensorId>(static_cast<quint8>(SensorId::Wrist) + index);
        pose.worldOrientation = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, float(index * 10));
        pose.valid = true;
        pose.updatedMonotonicNs = snapshot.updatedMonotonicNs;
    }
    return snapshot;
}

} // namespace

class HandSkeletonAdapterTest final : public QObject {
    Q_OBJECT

private slots:
    void mapsSixSensorsInStableOrder();
    void normalizesValidOrientations();
    void rejectsInvalidOrientations();
    void fallsBackToSnapshotTimestamp();
};

void HandSkeletonAdapterTest::mapsSixSensorsInStableOrder()
{
    const HandImuFrame frame = HandSkeleton::SixImuSnapshotAdapter::adapt(validSnapshot());
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        QCOMPARE(frame.samples[size_t(index)].slot, static_cast<ImuSlot>(index));
        QVERIFY(frame.samples[size_t(index)].valid);
        QCOMPARE(frame.samples[size_t(index)].timestampUsec, 5000);
    }
}

void HandSkeletonAdapterTest::normalizesValidOrientations()
{
    SixImuSnapshot snapshot = validSnapshot();
    snapshot.poses[2].worldOrientation *= 3.0F;
    const HandImuFrame frame = HandSkeleton::SixImuSnapshotAdapter::adapt(snapshot);
    QVERIFY(frame.samples[2].valid);
    QVERIFY(qAbs(frame.samples[2].orientation.lengthSquared() - 1.0F) < 1.0e-6F);
}

void HandSkeletonAdapterTest::rejectsInvalidOrientations()
{
    SixImuSnapshot snapshot = validSnapshot();
    snapshot.poses[1].valid = false;
    snapshot.poses[2].worldOrientation = QQuaternion(0.0F, 0.0F, 0.0F, 0.0F);
    snapshot.poses[3].worldOrientation = QQuaternion(
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F);
    const HandImuFrame frame = HandSkeleton::SixImuSnapshotAdapter::adapt(snapshot);
    QVERIFY(!frame.samples[1].valid);
    QVERIFY(!frame.samples[2].valid);
    QVERIFY(!frame.samples[3].valid);
    QCOMPARE(frame.samples[1].orientation.lengthSquared(), 0.0F);
}

void HandSkeletonAdapterTest::fallsBackToSnapshotTimestamp()
{
    SixImuSnapshot snapshot = validSnapshot();
    snapshot.updatedMonotonicNs = 7'500'000;
    snapshot.poses[4].updatedMonotonicNs = 0;
    const HandImuFrame frame = HandSkeleton::SixImuSnapshotAdapter::adapt(snapshot);
    QCOMPARE(frame.samples[4].timestampUsec, 7500);
}

QTEST_APPLESS_MAIN(HandSkeletonAdapterTest)
#include "test_hand_skeleton_adapter.moc"
