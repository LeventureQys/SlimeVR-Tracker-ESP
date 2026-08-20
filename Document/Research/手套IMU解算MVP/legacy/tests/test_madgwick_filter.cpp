#include <QtTest>

#include "six_imu_solver.h"

#include <cmath>
#include <limits>

namespace {
constexpr double Pi = 3.14159265358979323846;
const std::array<quint8, 6> Addresses = {0x50, 0x51, 0x52, 0x53, 0x54, 0x55};

double quaternionNorm(const QQuaternion &value)
{
    return std::sqrt(double(value.scalar()) * value.scalar() + double(value.x()) * value.x()
                     + double(value.y()) * value.y() + double(value.z()) * value.z());
}

double angularDistance(const QQuaternion &expected, const QQuaternion &actual)
{
    const double dot = double(expected.scalar()) * actual.scalar() + double(expected.x()) * actual.x()
        + double(expected.y()) * actual.y() + double(expected.z()) * actual.z();
    const double normalizedDot = dot / (quaternionNorm(expected) * quaternionNorm(actual));
    return 2.0 * std::acos(std::clamp(std::abs(normalizedDot), 0.0, 1.0));
}

ImuSampleGroup makeGroup(quint8 sequence, qint64 monotonicNs, qint16 accelZ = 2048,
                         qint16 gyroZ = 0, qint16 magX = 120)
{
    ImuSampleGroup group;
    group.sequence = sequence;
    group.complete = true;
    group.presentMask = 0x3f;
    group.emittedMonotonicNs = monotonicNs;
    for (int index = 0; index < 6; ++index) {
        ImuFrame frame;
        frame.address = Addresses[size_t(index)];
        frame.sensorId = static_cast<SensorId>(frame.address);
        frame.sequence = sequence;
        frame.acceleration.z = accelZ;
        frame.gyroscope.z = gyroZ;
        frame.magnetometer.x = magX;
        frame.allZero = false;
        frame.receivedMonotonicNs = monotonicNs;
        group.frames[size_t(index)] = frame;
    }
    return group;
}

bool finiteQuaternion(const QQuaternion &value)
{
    return std::isfinite(value.scalar()) && std::isfinite(value.x())
        && std::isfinite(value.y()) && std::isfinite(value.z());
}
}

class MadgwickFilterTest final : public QObject {
    Q_OBJECT

private slots:
    void A01_defaultRawConversionReference();
    void A02_stationaryInputStaysNormalized();
    void A03_constantAxisRotationHasCorrectDirection();
    void A04_validMagneticUsesNineAxis();
    void A05_invalidMagneticFallsBackAndRecovers();
    void A06_invalidDtDoesNotModifyFilter();
    void A07_hardFailureRollsBackAllSensors();
    void A08_identicalInputsProduceEquivalentOrientations();
    void A09_zeroCalibrationProducesIdentityRelativePose();
    void A10_failedCalibrationKeepsExistingZero();
    void A11_applySettingsResetsFusionAndCalibration();
    void A12_longSequenceRemainsFiniteAndNormalized();
};

void MadgwickFilterTest::A01_defaultRawConversionReference()
{
    const SolverSettings settings = SolverSettings::defaults();
    QCOMPARE(16384.0 / 32768.0 * settings.accelerometerRangeG, 8.0);
    QCOMPARE(16384.0 / 32768.0 * settings.gyroscopeRangeDps, 1000.0);
    QCOMPARE(120.0 / settings.magnetometerDivisor, 1.0);
    const double gyroRad = 16384.0 / 32768.0 * settings.gyroscopeRangeDps * Pi / 180.0;
    QVERIFY(std::abs(gyroRad - 1000.0 * Pi / 180.0) < 1.0e-6);
}

void MadgwickFilterTest::A02_stationaryInputStaysNormalized()
{
    MadgwickFilter filter;
    for (int index = 0; index < 1000; ++index) {
        QVERIFY(filter.update({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, std::nullopt, 0.005));
    }
    QVERIFY(finiteQuaternion(filter.quaternion()));
    QVERIFY(std::abs(quaternionNorm(filter.quaternion()) - 1.0) <= 1.0e-9);
}

void MadgwickFilterTest::A03_constantAxisRotationHasCorrectDirection()
{
    MadgwickFilter filter(0.0);
    for (int index = 0; index < 100; ++index) {
        QVERIFY(filter.update({0.0, 0.0, Pi / 2.0}, {0.0, 0.0, 1.0}, std::nullopt, 0.005));
    }
    const QQuaternion expected = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, 45.0f);
    QVERIFY(filter.quaternion().z() > 0.0f);
    QVERIFY(angularDistance(expected, filter.quaternion()) < 1.0e-3);
}

void MadgwickFilterTest::A04_validMagneticUsesNineAxis()
{
    MadgwickFilter filter;
    QVERIFY(filter.update({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, Vector3d{1.0, 0.0, 0.0}, 0.005));
    QCOMPARE(filter.mode(), FusionMode::NineAxis);
}

void MadgwickFilterTest::A05_invalidMagneticFallsBackAndRecovers()
{
    MadgwickFilter filter;
    QVERIFY(filter.update({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}, Vector3d{0.0, 0.0, 0.0}, 0.005));
    QCOMPARE(filter.mode(), FusionMode::SixAxis);
    QVERIFY(filter.update({0.0, 0.0, 0.0}, {0.0, 0.0, 1.0},
                          Vector3d{std::numeric_limits<double>::infinity(), 0.0, 0.0}, 0.005));
    QCOMPARE(filter.mode(), FusionMode::SixAxis);

    SixImuSolver solver;
    SolverSettings settings = solver.settings();
    settings.magnetometerMaxNorm = 0.5;
    solver.applySettings(settings);
    solver.processCompleteGroup(makeGroup(1, 1'000'000, 2048, 0, 120));
    QVERIFY(solver.latestSnapshot());
    QCOMPARE(solver.latestSnapshot()->poses[0].mode, FusionMode::SixAxis);
    solver.processCompleteGroup(makeGroup(2, 6'000'000, 2048, 0, 30));
    QCOMPARE(solver.latestSnapshot()->poses[0].mode, FusionMode::NineAxis);
}

void MadgwickFilterTest::A06_invalidDtDoesNotModifyFilter()
{
    const QList<double> invalidValues = {0.0, -0.005, 0.100001,
                                         std::numeric_limits<double>::quiet_NaN(),
                                         std::numeric_limits<double>::infinity()};
    for (double dt : invalidValues) {
        MadgwickFilter filter;
        const QQuaternion before = filter.quaternion();
        QCOMPARE(filter.mode(), FusionMode::Invalid);
        QVERIFY(!filter.update({0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}, std::nullopt, dt));
        QVERIFY(angularDistance(before, filter.quaternion()) < 1.0e-6);
        QCOMPARE(filter.mode(), FusionMode::Invalid);
    }
}

void MadgwickFilterTest::A07_hardFailureRollsBackAllSensors()
{
    SixImuSolver solver;
    solver.processCompleteGroup(makeGroup(1, 1'000'000, 2048, 200, 120));
    const SixImuSnapshot before = *solver.latestSnapshot();
    ImuSampleGroup invalid = makeGroup(2, 6'000'000, 2048, 300, 120);
    invalid.frames[3]->acceleration = {};
    invalid.frames[3]->allZero = false;
    solver.processCompleteGroup(invalid);
    const SixImuSnapshot after = *solver.latestSnapshot();
    QCOMPARE(after.sequence, before.sequence);
    for (int index = 0; index < 6; ++index) {
        QVERIFY(angularDistance(before.poses[size_t(index)].worldOrientation,
                                after.poses[size_t(index)].worldOrientation) < 1.0e-6);
    }
}

void MadgwickFilterTest::A08_identicalInputsProduceEquivalentOrientations()
{
    SixImuSolver solver;
    solver.processCompleteGroup(makeGroup(1, 1'000'000, 2048, 500, 120));
    const SixImuSnapshot snapshot = *solver.latestSnapshot();
    for (int index = 1; index < 6; ++index) {
        QVERIFY(angularDistance(snapshot.poses[0].worldOrientation,
                                snapshot.poses[size_t(index)].worldOrientation) < 1.0e-6);
    }
}

void MadgwickFilterTest::A09_zeroCalibrationProducesIdentityRelativePose()
{
    SixImuSolver solver;
    solver.processCompleteGroup(makeGroup(1, 1'000'000, 2048, 500, 120));
    QString error;
    QVERIFY2(solver.calibrateZero(&error), qPrintable(error));
    const SixImuSnapshot snapshot = *solver.latestSnapshot();
    const QQuaternion identity(1.0f, 0.0f, 0.0f, 0.0f);
    for (const SensorPose &pose : snapshot.poses) {
        QVERIFY(pose.calibrated);
        QVERIFY(angularDistance(identity, pose.relativeOrientation) < 1.0e-6);
    }
}

void MadgwickFilterTest::A10_failedCalibrationKeepsExistingZero()
{
    SixImuSolver solver;
    solver.processCompleteGroup(makeGroup(1, 1'000'000, 2048, 500, 120));
    QVERIFY(solver.calibrateZero());
    ImuSampleGroup group = makeGroup(2, 6'000'000, 2048, 500, 120);
    group.frames[4]->allZero = true;
    group.frames[4]->acceleration = {};
    group.frames[4]->gyroscope = {};
    group.frames[4]->magnetometer = {};
    solver.processCompleteGroup(group);
    const SixImuSnapshot beforeFailure = *solver.latestSnapshot();
    QString error;
    QVERIFY(!solver.calibrateZero(&error));
    QVERIFY(!error.isEmpty());
    const SixImuSnapshot afterFailure = *solver.latestSnapshot();
    QVERIFY(afterFailure.poses[0].calibrated);
    QVERIFY(angularDistance(beforeFailure.poses[0].relativeOrientation,
                            afterFailure.poses[0].relativeOrientation) < 1.0e-6);
}

void MadgwickFilterTest::A11_applySettingsResetsFusionAndCalibration()
{
    SixImuSolver solver;
    solver.processCompleteGroup(makeGroup(1, 1'000'000, 2048, 500, 120));
    QVERIFY(solver.calibrateZero());
    SolverSettings settings = solver.settings();
    settings.madgwickBeta = 0.2;
    solver.applySettings(settings);
    QCOMPARE(solver.settings(), settings);
    QVERIFY(!solver.latestSnapshot());
    QString error;
    QVERIFY(!solver.calibrateZero(&error));
}

void MadgwickFilterTest::A12_longSequenceRemainsFiniteAndNormalized()
{
    SixImuSolver solver;
    for (int index = 0; index < 20000; ++index) {
        solver.processCompleteGroup(makeGroup(quint8(index), 1'000'000 + qint64(index) * 5'000'000,
                                              2048, qint16((index % 200) - 100), 120));
        QVERIFY(solver.latestSnapshot());
        for (const SensorPose &pose : solver.latestSnapshot()->poses) {
            QVERIFY(finiteQuaternion(pose.worldOrientation));
            QVERIFY(std::abs(quaternionNorm(pose.worldOrientation) - 1.0) <= 1.0e-6);
        }
    }
}

QTEST_APPLESS_MAIN(MadgwickFilterTest)
#include "test_madgwick_filter.moc"
