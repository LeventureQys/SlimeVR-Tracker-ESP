#include "hand/hand_observation_solver.h"
#include "hand/mount_calibration.h"

#include <QtTest>

#include <limits>

using namespace handstudio;

class TestHandObservation : public QObject {
    Q_OBJECT
private slots:
    void commonPalmRotationCancels();
    void mountOrderAndFingerIsolation();
    void wristInvalidFreezes();
    void rejectsNonFinite();
    void neutralZeroingMakesCurrentPoseNeutral();
};

static std::array<FusedImuPose, 6> poses(const QQuaternion &palm, const QQuaternion &relative)
{
    std::array<FusedImuPose, 6> result{};
    for (int index = 0; index < 6; ++index) {
        result[std::size_t(index)].sensorId = AllSensorIds[std::size_t(index)];
        result[std::size_t(index)].sequence = 7;
        result[std::size_t(index)].timestampNs = 100;
        result[std::size_t(index)].worldOrientation = index == 0 ? palm : palm * relative;
        result[std::size_t(index)].valid = true;
        result[std::size_t(index)].confidence = 1.0F;
    }
    return result;
}

void TestHandObservation::commonPalmRotationCancels()
{
    HandObservationSolver solver;
    const QQuaternion relative = QQuaternion::fromAxisAndAngle(1, 0, 0, 35);
    for (const QVector3D axis : {QVector3D(1,0,0), QVector3D(0,1,0), QVector3D(0,0,1)}) {
        auto frame = solver.solve(poses(QQuaternion::fromAxisAndAngle(axis, 47), relative));
        QVERIFY(frame.fingers[0].valid);
        QVERIFY(qAbs(frame.fingers[0].flexionDegrees - 35.0F) < 0.01F);
    }
}

void TestHandObservation::mountOrderAndFingerIsolation()
{
    QQuaternion relative = QQuaternion::fromAxisAndAngle(1, 0, 0, 30);
    QQuaternion mount = QQuaternion::fromAxisAndAngle(0, 0, 1, 90);
    const auto corrected = applyMountCalibration(relative, mount);
    QVERIFY(corrected.has_value());
    const QVector3D rotated = corrected->rotatedVector(QVector3D(0,0,1));
    const QVector3D expected = (mount * relative * mount.conjugated()).rotatedVector(QVector3D(0,0,1));
    QVERIFY((rotated - expected).length() < 1.0e-5F);

    std::array<FingerObservationConfig, 5> configs{};
    for (int index=0; index<5; ++index) configs[std::size_t(index)].sensorId = AllSensorIds[std::size_t(index+1)];
    configs[0].flexionAxis = QVector3D(0,1,0);
    HandObservationSolver solver(configs);
    auto frame = solver.solve(poses(QQuaternion(), QQuaternion::fromAxisAndAngle(1,0,0,25)));
    QVERIFY(qAbs(frame.fingers[0].flexionDegrees) < 0.01F);
    QVERIFY(qAbs(frame.fingers[1].flexionDegrees - 25.0F) < 0.01F);
}

void TestHandObservation::wristInvalidFreezes()
{
    HandObservationSolver solver;
    const auto first = solver.solve(poses(QQuaternion(), QQuaternion::fromAxisAndAngle(1,0,0,20)));
    auto invalid = poses(QQuaternion(), QQuaternion::fromAxisAndAngle(1,0,0,70));
    invalid[0].valid = false;
    const auto frozen = solver.solve(invalid);
    QCOMPARE(frozen.sequence, first.sequence);
    QCOMPARE(frozen.fingers[0].flexionDegrees, first.fingers[0].flexionDegrees);
}

void TestHandObservation::rejectsNonFinite()
{
    HandObservationSolver solver;
    auto input = poses(QQuaternion(), QQuaternion());
    input[1].worldOrientation = QQuaternion(std::numeric_limits<float>::infinity(),0,0,0);
    QVERIFY(!solver.solve(input).fingers[0].valid);
    input[1].worldOrientation = QQuaternion(0,0,0,0);
    QVERIFY(!solver.solve(input).fingers[0].valid);
}

void TestHandObservation::neutralZeroingMakesCurrentPoseNeutral()
{
    HandObservationSolver solver;
    const QQuaternion flexed = QQuaternion::fromAxisAndAngle(1, 0, 0, 40);

    // Absolute decomposition before zeroing.
    auto first = solver.solve(poses(QQuaternion(), flexed));
    QVERIFY(first.fingers[0].valid);
    QVERIFY(qAbs(first.fingers[0].flexionDegrees - 40.0F) < 0.01F);

    // 调零：capture current pose as neutral.
    QVERIFY(!solver.hasNeutral());
    QVERIFY(solver.setNeutral(poses(QQuaternion(), flexed)));
    QVERIFY(solver.hasNeutral());

    // Solving the same pose after zeroing → all-zero components, wrist identity.
    auto neutral = solver.solve(poses(QQuaternion(), flexed));
    QVERIFY(neutral.fingers[0].valid);
    QVERIFY(qAbs(neutral.fingers[0].flexionDegrees) < 0.01F);
    QVERIFY(qAbs(neutral.fingers[0].abductionDegrees) < 0.01F);
    QVERIFY(qAbs(neutral.fingers[0].twistDegrees) < 0.01F);
    QVERIFY((neutral.wristWorldOrientation - QQuaternion()).lengthSquared() < 1.0e-8F);

    // More flexed than neutral → positive flexion relative to neutral.
    auto more = solver.solve(poses(QQuaternion(), QQuaternion::fromAxisAndAngle(1, 0, 0, 55)));
    QVERIFY(qAbs(more.fingers[0].flexionDegrees - 15.0F) < 0.01F);

    // Failed capture (wrist invalid) keeps the previous neutral.
    auto invalid = poses(QQuaternion(), flexed);
    invalid[0].valid = false;
    QVERIFY(!solver.setNeutral(invalid));
    auto still = solver.solve(poses(QQuaternion(), flexed));
    QVERIFY(qAbs(still.fingers[0].flexionDegrees) < 0.01F);

    // clearNeutral restores absolute decomposition.
    solver.clearNeutral();
    QVERIFY(!solver.hasNeutral());
    auto absolute = solver.solve(poses(QQuaternion(), flexed));
    QVERIFY(qAbs(absolute.fingers[0].flexionDegrees - 40.0F) < 0.01F);
}

QTEST_APPLESS_MAIN(TestHandObservation)
#include "test_hand_observation.moc"
