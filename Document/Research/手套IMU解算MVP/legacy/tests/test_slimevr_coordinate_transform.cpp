#include "slimevr_coordinate_transform.h"
#include "slimevr_pose_adapter.h"
#include "slimevr_settings_store.h"

#include <QDir>
#include <QUuid>
#include <QtTest>

#include <cmath>
#include <limits>

namespace {

QString temporaryIniPath()
{
    const QString directoryPath = QDir::current().filePath(QStringLiteral("settings-test-data"));
    QDir().mkpath(directoryPath);
    return QDir(directoryPath).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".ini"));
}

SixImuSnapshot makeSnapshot()
{
    SixImuSnapshot snapshot;
    snapshot.sequence = 1;
    snapshot.updatedMonotonicNs = 1000;
    for (int index = 0; index < 6; ++index) {
        SensorPose &pose = snapshot.poses[size_t(index)];
        pose.sensorId = static_cast<SensorId>(int(SensorId::Wrist) + index);
        pose.valid = true;
        pose.updatedMonotonicNs = 1000;
        pose.worldOrientation = QQuaternion(1.0F, 0.0F, 0.0F, 0.0F);
    }
    return snapshot;
}

bool closeEnough(const QQuaternion &expected, const QQuaternion &actual)
{
    const double dot = double(expected.scalar()) * actual.scalar() + double(expected.x()) * actual.x()
        + double(expected.y()) * actual.y() + double(expected.z()) * actual.z();
    return std::abs(std::abs(dot) - 1.0) < 1.0e-4;
}

} // namespace

class SlimeVrCoordinateTransformTest final : public QObject {
    Q_OBJECT

private slots:
    void C01_identityMountingPassesSourceThrough();
    void C02_mountingSandwichMatchesDocumentedFormula();
    void C03_gloveSideDoesNotChangeFormula();
    void C04_invalidMountingsRejected();
    void C05_perSensorIndependence();
    void C06_settingsStoreRoundTrip();
    void C07_damagedMountingFallsBackPerSensor();
    void C08_adapterAppliesMounting();
};

void SlimeVrCoordinateTransformTest::C01_identityMountingPassesSourceThrough()
{
    SlimeVrCoordinateTransform transform;
    const QQuaternion source = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 30.0F);
    QVERIFY(closeEnough(source, transform.transform(SensorId::Wrist, source)));
    QVERIFY(closeEnough(source, transform.transform(SensorId::Pinky, source)));
}

void SlimeVrCoordinateTransformTest::C02_mountingSandwichMatchesDocumentedFormula()
{
    SlimeVrCoordinateTransform transform;
    const QQuaternion mount = QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, 90.0F);
    const QQuaternion source = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 90.0F);
    QVERIFY(transform.setMounting(SensorId::Middle, mount));
    const QQuaternion expected = mount.conjugated() * source * mount;
    QVERIFY(closeEnough(expected, transform.transform(SensorId::Middle, source)));
    // Other sensors stay untouched.
    QVERIFY(closeEnough(source, transform.transform(SensorId::Index, source)));
}

void SlimeVrCoordinateTransformTest::C03_gloveSideDoesNotChangeFormula()
{
    SlimeVrCoordinateTransform left(GloveSide::Left);
    SlimeVrCoordinateTransform right(GloveSide::Right);
    const QQuaternion source = QQuaternion::fromAxisAndAngle(0.0F, 1.0F, 0.0F, 45.0F);
    QVERIFY(closeEnough(
        left.transform(SensorId::Thumb, source),
        right.transform(SensorId::Thumb, source)));
}

void SlimeVrCoordinateTransformTest::C04_invalidMountingsRejected()
{
    SlimeVrCoordinateTransform transform;
    QVERIFY(!transform.setMounting(
        SensorId::Thumb,
        QQuaternion(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F)));
    QVERIFY(!transform.setMounting(SensorId::Thumb, QQuaternion(0.0F, 0.0F, 0.0F, 0.0F)));
    QVERIFY(!transform.setMounting(SensorId::Thumb, QQuaternion(5.0F, 0.0F, 0.0F, 0.0F)));
    QVERIFY(transform.setMounting(
        SensorId::Thumb, QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 15.0F)));

    SlimeVrSettings settings = SlimeVrSettings::defaults();
    settings.mountings[2] = QQuaternion(0.0F, 0.0F, 0.0F, 0.0F);
    QString error;
    QVERIFY(!validateSlimeVrSettings(settings, &error));
    QVERIFY(!error.isEmpty());
}

void SlimeVrCoordinateTransformTest::C05_perSensorIndependence()
{
    SlimeVrCoordinateTransform transform;
    const QQuaternion mount = QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, 90.0F);
    QVERIFY(transform.setMounting(SensorId::Ring, mount));
    const QQuaternion source = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 30.0F);
    QVERIFY(closeEnough(source, transform.transform(SensorId::Pinky, source)));
    QVERIFY(!closeEnough(source, transform.transform(SensorId::Ring, source)));
}

void SlimeVrCoordinateTransformTest::C06_settingsStoreRoundTrip()
{
    const QString path = temporaryIniPath();
    SlimeVrSettings expected = SlimeVrSettings::defaults();
    expected.enabled = true;
    expected.discoveryMode = SlimeVrDiscoveryMode::FixedHost;
    expected.host = QStringLiteral("127.0.0.1");
    expected.sendRateHz = 80;
    expected.gloveSide = GloveSide::Right;
    expected.mountings[0] = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 10.0F);
    expected.mountings[3] = QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, 90.0F);
    {
        QSettings settings(path, QSettings::IniFormat);
        SlimeVrSettingsStore store(settings);
        QString error;
        QVERIFY2(store.save(expected, &error), qPrintable(error));
    }
    QSettings reloaded(path, QSettings::IniFormat);
    SlimeVrSettingsStore store(reloaded);
    const SlimeVrSettings actual = store.load();
    QCOMPARE(actual.enabled, expected.enabled);
    QCOMPARE(actual.sendRateHz, 80);
    QCOMPARE(actual.gloveSide, GloveSide::Right);
    for (int index = 0; index < 6; ++index) {
        QVERIFY(closeEnough(expected.mountings[size_t(index)], actual.mountings[size_t(index)]));
    }
    QCOMPARE(actual.deviceId.size(), 6);
}

void SlimeVrCoordinateTransformTest::C07_damagedMountingFallsBackPerSensor()
{
    const QString path = temporaryIniPath();
    {
        QSettings settings(path, QSettings::IniFormat);
        SlimeVrSettingsStore store(settings);
        SlimeVrSettings value = SlimeVrSettings::defaults();
        value.mountings[1] = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 30.0F);
        QVERIFY(store.save(value));
        settings.setValue(QStringLiteral("slimevr/mount2"), QStringLiteral("garbage"));
        settings.setValue(QStringLiteral("slimevr/mount3"), QStringLiteral("0,0,0,0"));
        settings.sync();
    }
    QSettings settings(path, QSettings::IniFormat);
    SlimeVrSettingsStore store(settings);
    const SlimeVrSettings loaded = store.load();
    const QQuaternion identity(1.0F, 0.0F, 0.0F, 0.0F);
    QVERIFY(closeEnough(identity, loaded.mountings[2]));
    QVERIFY(closeEnough(identity, loaded.mountings[3]));
    QVERIFY(closeEnough(
        QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 30.0F), loaded.mountings[1]));
}

void SlimeVrCoordinateTransformTest::C08_adapterAppliesMounting()
{
    SlimeVrPoseAdapter adapter(GloveSide::Left);
    const QQuaternion mount = QQuaternion::fromAxisAndAngle(1.0F, 0.0F, 0.0F, 90.0F);
    QVERIFY(adapter.setMounting(SensorId::Index, mount));

    SixImuSnapshot snapshot = makeSnapshot();
    snapshot.poses[2].worldOrientation = QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, 90.0F);
    const auto samples = adapter.adapt(snapshot);

    const QQuaternion expected = mount.conjugated()
        * snapshot.poses[2].worldOrientation * mount;
    QVERIFY(samples[2].valid);
    QVERIFY(closeEnough(expected, samples[2].orientation));
    // Untouched sensors keep the source orientation.
    QVERIFY(closeEnough(QQuaternion(1.0F, 0.0F, 0.0F, 0.0F), samples[0].orientation));
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    SlimeVrCoordinateTransformTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_slimevr_coordinate_transform.moc"
