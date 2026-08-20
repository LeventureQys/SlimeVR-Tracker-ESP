#include <QtTest>

#include "fusion/fusion_bank.h"
#include "fusion/madgwick_fusion_filter.h"
#include "fusion/magnetic_health_monitor.h"
#include "fusion/pose_guard.h"
#include "fusion/quaternion_util.h"
#include "fusion/vqf_fusion_filter.h"

#include "core/calibrated_types.h"
#include "core/fusion_types.h"
#include "core/sensor_id.h"

#include <cmath>
#include <limits>

using namespace handstudio;

namespace {
constexpr double Pi = 3.14159265358979323846;

CalibratedImuSample makeSample(SensorId sensorId, quint8 sequence, qint64 timestampNs,
                               const QVector3D &accel, const QVector3D &gyro, const QVector3D &mag)
{
    CalibratedImuSample sample;
    sample.sensorId = sensorId;
    sample.sequence = sequence;
    sample.timestampNs = timestampNs;
    sample.accelerationMps2 = accel;
    sample.gyroscopeRadPerSec = gyro;
    sample.magneticMicroTesla = mag;
    sample.valid = true;
    sample.calibrationState = CalibrationState::Calibrated;
    return sample;
}

double angularDistance(const QQuaternion &expected, const QQuaternion &actual)
{
    const double dot = double(expected.scalar()) * actual.scalar() + double(expected.x()) * actual.x()
        + double(expected.y()) * actual.y() + double(expected.z()) * actual.z();
    const double denom = quaternionNorm(expected) * quaternionNorm(actual);
    const double normalized = denom > 0.0 ? dot / denom : 0.0;
    return 2.0 * std::acos(std::clamp(std::abs(normalized), 0.0, 1.0));
}
}

class FusionTest final : public QObject {
    Q_OBJECT

private slots:
    void madgwickSingleAxisRotationHasCorrectDirection();
    void vqfSingleAxisRotationHasCorrectDirection();
    void dtZeroBatchAggregatesAndFallbackRateLimits();
    void nanInfZeroNormProtectionHoldsLastPose();
    void signContinuityNegatesEquivalentQuaternion();
    void magneticHealthHysteresisStateMachine();
    void disturbedDegradesToSixDAndRecoveringRateLimitsHeading();
    void madgwickAndVqfProduceUnifiedFieldsOnSameDataset();
};

void FusionTest::madgwickSingleAxisRotationHasCorrectDirection()
{
    MadgwickFusionFilter filter;
    filter.setBeta(0.0);
    const CalibratedImuSample sample = makeSample(SensorId::Wrist, 1, 0,
                                                  QVector3D(0.0f, 0.0f, 9.80665f),
                                                  QVector3D(0.0f, 0.0f, float(Pi / 2.0)),
                                                  QVector3D());
    FusedImuPose pose;
    for (int i = 0; i < 100; ++i) {
        pose = filter.update(sample, 0.005);
    }
    QVERIFY(pose.valid);
    QVERIFY(pose.mode == FusionMode::SixD);
    const QQuaternion expected = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, 45.0f);
    QVERIFY(pose.worldOrientation.z() > 0.0f);
    QVERIFY(angularDistance(expected, pose.worldOrientation) < 1.0e-3);
    QVERIFY(quaternionNormError(pose.worldOrientation) <= 1.0e-4);
}

void FusionTest::vqfSingleAxisRotationHasCorrectDirection()
{
    VqfFusionFilter filter(200.0);
    QVERIFY(filter.isAvailable());
    const CalibratedImuSample sample = makeSample(SensorId::Wrist, 1, 0,
                                                  QVector3D(0.0f, 0.0f, 9.80665f),
                                                  QVector3D(0.0f, 0.0f, float(Pi / 2.0)),
                                                  QVector3D());
    FusedImuPose pose;
    for (int i = 0; i < 100; ++i) {
        pose = filter.update(sample, 0.005);
    }
    QVERIFY(pose.valid);
    QVERIFY(pose.mode == FusionMode::SixD);
    QVERIFY(isFiniteQuaternion(pose.worldOrientation));
    QVERIFY(quaternionNormError(pose.worldOrientation) <= 1.0e-4);
    const QQuaternion expected = QQuaternion::fromAxisAndAngle(0.0f, 0.0f, 1.0f, 45.0f);
    QVERIFY(pose.worldOrientation.z() > 0.0f);
    QVERIFY(angularDistance(expected, pose.worldOrientation) < 0.2);
}

void FusionTest::dtZeroBatchAggregatesAndFallbackRateLimits()
{
    FusionBank bank;
    const QVector3D accel(0.0f, 0.0f, 9.80665f);
    const QVector3D gyro(0.0f, 0.0f, 0.0f);
    const QVector3D mag(50.0f, 0.0f, 0.0f);

    // 200 groups sharing one serial-read timestamp (real hardware batch
    // arrival at ~200 groups/s): zero-dt must be aggregated, not flooded.
    for (int i = 0; i < 200; ++i) {
        QVERIFY(bank.process(makeSample(SensorId::Wrist, quint8(i), 1'000'000'000,
                                        accel, gyro, mag)).valid);
    }
    // dt<0 and dt>0.1 inside the same 1s rate-limit window → suppressed.
    QVERIFY(bank.process(makeSample(SensorId::Wrist, 201, 500'000'000, accel, gyro, mag)).valid);
    QVERIFY(bank.process(makeSample(SensorId::Wrist, 202, 1'600'000'000, accel, gyro, mag)).valid);
    // dt>0.1 after the 1s window → aggregated diagnostic emits again.
    QVERIFY(bank.process(makeSample(SensorId::Wrist, 203, 3'000'000'000, accel, gyro, mag)).valid);

    int anomalyCount = 0;
    int fallbackCount = 0;
    int zeroBatchCount = 0;
    const QVector<Diagnostic> diagnostics = bank.takeDiagnostics();
    for (const Diagnostic &diagnostic : diagnostics) {
        if (diagnostic.code == QStringLiteral("fusion.dt.anomaly")) {
            ++anomalyCount;
        } else if (diagnostic.code == QStringLiteral("fusion.dt.fallback")) {
            ++fallbackCount;
        } else if (diagnostic.code == QStringLiteral("fusion.dt.zero-batch")) {
            ++zeroBatchCount;
        }
    }
    // First window (lastDtDiagnosticNs initialized to -1e9) emits at ts=1e9
    // with 199 zero-dt groups; second window emits at ts=3e9 with 2 fallbacks.
    QCOMPARE(anomalyCount, 2);
    QCOMPARE(fallbackCount, 0);
    QCOMPARE(zeroBatchCount, 0);
}

void FusionTest::nanInfZeroNormProtectionHoldsLastPose()
{
    PoseGuard guard;
    const QQuaternion valid(1.0f, 0.0f, 0.0f, 0.0f);
    PoseGuardResult first = guard.protect(valid);
    QVERIFY(first.valid);
    QVERIFY(!first.held);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const QList<QQuaternion> invalidCandidates = {
        QQuaternion(nan, 0.0f, 0.0f, 0.0f),
        QQuaternion(inf, 0.0f, 0.0f, 0.0f),
        QQuaternion(0.0f, 0.0f, 0.0f, 0.0f),  // zero norm
        QQuaternion(2.0f, 0.0f, 0.0f, 0.0f),  // norm error 1.0 > 1e-4
    };
    for (const QQuaternion &candidate : invalidCandidates) {
        PoseGuardResult result = guard.protect(candidate);
        QVERIFY(result.held);
        QVERIFY(result.valid);
        QVERIFY(angularDistance(valid, result.orientation) < 1.0e-9);
    }
    QVERIFY(guard.hasValidPose());

    // A slightly-off unit quaternion (norm error <= 1e-4) is accepted and normalized.
    const QQuaternion slightlyOff(1.00005f, 0.0f, 0.0f, 0.0f);
    PoseGuardResult accepted = guard.protect(slightlyOff);
    QVERIFY(accepted.valid);
    QVERIFY(!accepted.held);
    QVERIFY(quaternionNormError(accepted.orientation) <= 1.0e-4);
}

void FusionTest::signContinuityNegatesEquivalentQuaternion()
{
    PoseGuard guard;
    QVERIFY(guard.protect(QQuaternion(1.0f, 0.0f, 0.0f, 0.0f)).valid);
    const PoseGuardResult result = guard.protect(QQuaternion(-1.0f, 0.0f, 0.0f, 0.0f));
    QVERIFY(result.valid);
    QVERIFY(!result.held);
    QVERIFY(result.orientation.scalar() > 0.0f);
}

void FusionTest::magneticHealthHysteresisStateMachine()
{
    MagneticHealthMonitor::Config config;
    config.enabled = true;
    config.minNormMicroTesla = 1.0;
    config.referenceNormMicroTesla = 50.0;
    config.toleranceRatio = 0.3;
    config.disturbSamples = 3;
    config.recoverSamples = 4;
    config.healthySamples = 8;

    MagneticHealthMonitor monitor(config);
    QVERIFY(monitor.state() == MagneticHealth::Unavailable);

    const QVector3D normal(50.0f, 0.0f, 0.0f);
    const QVector3D disturbed(200.0f, 0.0f, 0.0f);

    // Unavailable -> Recovering -> Healthy after healthySamples normals.
    for (int i = 0; i < 8; ++i) {
        monitor.update(normal);
    }
    QVERIFY(monitor.state() == MagneticHealth::Healthy);

    // Healthy -> Disturbed after disturbSamples abnormals.
    for (int i = 0; i < 3; ++i) {
        monitor.update(disturbed);
    }
    QVERIFY(monitor.state() == MagneticHealth::Disturbed);

    // Disturbed -> Recovering after recoverSamples normals.
    for (int i = 0; i < 4; ++i) {
        monitor.update(normal);
    }
    QVERIFY(monitor.state() == MagneticHealth::Recovering);

    // Recovering -> Healthy after healthySamples normals.
    for (int i = 0; i < 4; ++i) {
        monitor.update(normal);
    }
    QVERIFY(monitor.state() == MagneticHealth::Healthy);

    // Disabled monitor reports Unavailable.
    config.enabled = false;
    monitor.setConfig(config);
    monitor.update(normal);
    QVERIFY(monitor.state() == MagneticHealth::Unavailable);
}

void FusionTest::disturbedDegradesToSixDAndRecoveringRateLimitsHeading()
{
    FusionBank bank;
    FusionBank::Settings settings = bank.settings();
    settings.magneticHealth.disturbSamples = 3;
    settings.magneticHealth.recoverSamples = 4;
    settings.magneticHealth.healthySamples = 8;
    settings.magneticHealth.referenceNormMicroTesla = 50.0;
    settings.magneticHealth.toleranceRatio = 0.3;
    settings.magneticHealth.minNormMicroTesla = 1.0;
    settings.maxHeadingRecoveryRadPerSec = 0.2;
    bank.setSettings(settings);

    const QVector3D accel(0.0f, 0.0f, 9.80665f);
    const QVector3D magNormal(50.0f, 0.0f, 0.0f);
    const QVector3D magDisturbed(200.0f, 0.0f, 0.0f);

    // Phase 1: healthy NineD.
    for (int i = 0; i < 20; ++i) {
        const FusedImuPose pose = bank.process(makeSample(SensorId::Wrist, quint8(i),
                                                          qint64(1'000'000 + i * 5'000'000),
                                                          accel, QVector3D(), magNormal));
        QVERIFY(pose.valid);
    }

    // Phase 2: disturb + heading drift (SixD).
    bool sawSixD = false;
    for (int i = 20; i < 40; ++i) {
        const FusedImuPose pose = bank.process(makeSample(SensorId::Wrist, quint8(i),
                                                          qint64(1'000'000 + i * 5'000'000),
                                                          accel, QVector3D(0.0f, 0.0f, 3.0f),
                                                          magDisturbed));
        if (pose.magneticHealth == MagneticHealth::Disturbed) {
            QVERIFY(pose.mode == FusionMode::SixD);
            sawSixD = true;
        }
    }
    QVERIFY(sawSixD);

    // Phase 3: recover; heading correction must be rate limited.
    const double maxStep = settings.maxHeadingRecoveryRadPerSec * settings.nominalDtSeconds;
    double previousYaw = 0.0;
    bool hasPreviousYaw = false;
    bool sawRecovering = false;
    for (int i = 40; i < 140; ++i) {
        const FusedImuPose pose = bank.process(makeSample(SensorId::Wrist, quint8(i),
                                                          qint64(1'000'000 + i * 5'000'000),
                                                          accel, QVector3D(), magNormal));
        QVERIFY(pose.valid);
        if (pose.magneticHealth == MagneticHealth::Recovering) {
            sawRecovering = true;
            const double yaw = yawZyxRadians(pose.worldOrientation);
            if (hasPreviousYaw) {
                const double delta = std::abs(wrapToPi(yaw - previousYaw));
                QVERIFY2(delta <= maxStep + 1.0e-5,
                         qPrintable(QStringLiteral("航向跳变超限：%1 > %2").arg(delta).arg(maxStep)));
            }
            previousYaw = yaw;
            hasPreviousYaw = true;
        }
    }
    QVERIFY(sawRecovering);
}

void FusionTest::madgwickAndVqfProduceUnifiedFieldsOnSameDataset()
{
    QVector<CalibratedImuSample> dataset;
    for (int i = 0; i < 120; ++i) {
        const double t = double(i) * 0.005;
        const QVector3D accel(0.0f, 0.0f, 9.80665f);
        const QVector3D gyro(0.0f, 0.0f, float(0.5 * std::sin(t)));
        const QVector3D mag(50.0f, 0.0f, 0.0f);
        dataset.append(makeSample(SensorId::Wrist, quint8(i % 256), qint64(1'000'000 + i * 5'000'000),
                                  accel, gyro, mag));
    }

    FusionBank madgwick(FusionBank::Algorithm::Madgwick);
    FusionBank vqf(FusionBank::Algorithm::Vqf);
    for (const CalibratedImuSample &sample : dataset) {
        const FusedImuPose madgwickPose = madgwick.process(sample);
        const FusedImuPose vqfPose = vqf.process(sample);

        QVERIFY(madgwickPose.valid);
        QVERIFY(vqfPose.valid);
        QVERIFY(madgwickPose.sensorId == sample.sensorId);
        QVERIFY(vqfPose.sensorId == sample.sensorId);
        QCOMPARE(madgwickPose.sequence, sample.sequence);
        QCOMPARE(vqfPose.sequence, sample.sequence);
        QCOMPARE(madgwickPose.timestampNs, sample.timestampNs);
        QCOMPARE(vqfPose.timestampNs, sample.timestampNs);
        QVERIFY(madgwickPose.mode != FusionMode::Invalid);
        QVERIFY(vqfPose.mode != FusionMode::Invalid);
        QVERIFY(isFiniteQuaternion(madgwickPose.worldOrientation));
        QVERIFY(isFiniteQuaternion(vqfPose.worldOrientation));
        QVERIFY(quaternionNormError(madgwickPose.worldOrientation) <= 1.0e-4);
        QVERIFY(quaternionNormError(vqfPose.worldOrientation) <= 1.0e-4);
        QVERIFY(madgwickPose.confidence >= 0.0f && madgwickPose.confidence <= 1.0f);
        QVERIFY(vqfPose.confidence >= 0.0f && vqfPose.confidence <= 1.0f);
    }
}

QTEST_APPLESS_MAIN(FusionTest)
#include "test_fusion.moc"
