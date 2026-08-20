#include <QtTest>

#include "calibration/axis_remap.h"
#include "calibration/calibration_parameters.h"
#include "calibration/calibration_pipeline.h"
#include "calibration/calibration_store.h"
#include "calibration/magnetic_calibration.h"
#include "calibration/static_gyro_bias_estimator.h"

#include "core/calibrated_types.h"
#include "core/fusion_types.h"
#include "core/imu_frames.h"
#include "core/sensor_id.h"

#include <cmath>

using namespace handstudio;

namespace {
constexpr double Pi = 3.14159265358979323846;

RawImuFrame makeRawFrame(SensorId sensorId, quint8 sequence, qint16 ax, qint16 ay, qint16 az,
                         qint16 gx, qint16 gy, qint16 gz, qint16 mx, qint16 my, qint16 mz,
                         qint64 timestampNs = 1'000'000)
{
    RawImuFrame frame;
    frame.sensorId = sensorId;
    frame.address = sensorAddress(sensorId);
    frame.sequence = sequence;
    frame.receivedMonotonicNs = timestampNs;
    frame.accelerationRaw = {ax, ay, az};
    frame.gyroscopeRaw = {gx, gy, gz};
    frame.magnetometerRaw = {mx, my, mz};
    frame.crcValid = true;
    frame.allZero = (ax == 0 && ay == 0 && az == 0 && gx == 0 && gy == 0 && gz == 0
                     && mx == 0 && my == 0 && mz == 0);
    return frame;
}

bool near(double a, double b, double tolerance)
{
    return std::abs(a - b) <= tolerance;
}
}

class CalibrationTest final : public QObject {
    Q_OBJECT

private slots:
    void axisRemapIdentityAndPermutation();
    void axisRemapRejectsInvalidMatrix();
    void rangeScalingConvertsUnits();
    void axisRemapAndSignAppliedToRaw();
    void perSensorParamsAreIndependent();
    void staticGyroBiasConvergesAtRest();
    void staticGyroBiasFreezesDuringMotion();
    void magneticHardSoftIronCalibrationAndValidation();
    void magneticCalibrationRejectsInsufficientOrDegenerate();
    void calibrationPersistenceRoundTrip();
    void calibrationPersistenceRejectsBadSchema();
    void calibrationPersistenceRequiresDeviceId();
    void invalidParamsProduceInvalidSample();
};

void CalibrationTest::axisRemapIdentityAndPermutation()
{
    const AxisRemap identity = AxisRemap::identity();
    QVERIFY(identity.isValid());
    const QVector3D out = identity.apply(QVector3D(1.0f, 2.0f, 3.0f));
    QCOMPARE(out.x(), 1.0f);
    QCOMPARE(out.y(), 2.0f);
    QCOMPARE(out.z(), 3.0f);

    AxisRemap permute;
    permute.matrix = {{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}};
    QVERIFY(permute.isValid());
    const QVector3D mapped = permute.apply(QVector3D(1.0f, 2.0f, 3.0f));
    QCOMPARE(mapped.x(), 2.0f);
    QCOMPARE(mapped.y(), 1.0f);
    QCOMPARE(mapped.z(), -3.0f);
}

void CalibrationTest::axisRemapRejectsInvalidMatrix()
{
    AxisRemap bad;
    bad.matrix = {{{1, 1, 0}, {0, 1, 0}, {0, 0, 1}}};  // two non-zero in row 0
    QString reason;
    QVERIFY(!bad.isValid(&reason));
    QVERIFY(!reason.isEmpty());

    AxisRemap badColumn;
    badColumn.matrix = {{{0, 0, 1}, {0, 1, 0}, {0, 1, 0}}};  // column 1 has two entries
    QVERIFY(!badColumn.isValid());
}

void CalibrationTest::rangeScalingConvertsUnits()
{
    CalibrationPipeline pipeline;
    pipeline.setRestBiasEstimationEnabled(false);

    const CalibratedImuSample sample = pipeline.calibrate(
        makeRawFrame(SensorId::Wrist, 1, 16384, 0, 0, 16384, 0, 0, 120, 0, 0));

    QVERIFY(sample.valid);
    // 16384/32768 * 16 g = 8 g -> 8 * 9.80665 m/s^2
    QVERIFY(near(double(sample.accelerationMps2.x()), 8.0 * 9.80665, 1.0e-3));
    // 16384/32768 * 2000 dps = 1000 dps -> 1000 * pi/180 rad/s
    QVERIFY(near(double(sample.gyroscopeRadPerSec.x()), 1000.0 * Pi / 180.0, 1.0e-4));
    QVERIFY(near(double(sample.magneticMicroTesla.x()), 120.0, 1.0e-3));
}

void CalibrationTest::axisRemapAndSignAppliedToRaw()
{
    CalibrationPipeline pipeline;
    pipeline.setRestBiasEstimationEnabled(false);

    SensorCalibrationParams params = SensorCalibrationParams::defaults(SensorId::Wrist);
    params.accelerometerAxes.matrix = {{{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}};
    pipeline.setParams(SensorId::Wrist, params);

    const CalibratedImuSample sample = pipeline.calibrate(
        makeRawFrame(SensorId::Wrist, 1, 16384, 0, 8192, 0, 0, 0, 0, 0, 0));
    QVERIFY(sample.valid);
    // remap: outX = inY (0), outY = inX (16384), outZ = -inZ (-8192)
    QVERIFY(near(double(sample.accelerationMps2.x()), 0.0, 1.0e-3));
    QVERIFY(near(double(sample.accelerationMps2.y()), 8.0 * 9.80665, 1.0e-3));
    QVERIFY(near(double(sample.accelerationMps2.z()), -4.0 * 9.80665, 1.0e-3));
}

void CalibrationTest::perSensorParamsAreIndependent()
{
    CalibrationPipeline pipeline;
    pipeline.setRestBiasEstimationEnabled(false);

    SensorCalibrationParams wrist = SensorCalibrationParams::defaults(SensorId::Wrist);
    wrist.accelerometerRangeG = 8.0;
    SensorCalibrationParams thumb = SensorCalibrationParams::defaults(SensorId::Thumb);
    thumb.accelerometerRangeG = 16.0;
    pipeline.setParams(SensorId::Wrist, wrist);
    pipeline.setParams(SensorId::Thumb, thumb);

    const CalibratedImuSample wristSample = pipeline.calibrate(
        makeRawFrame(SensorId::Wrist, 1, 16384, 0, 0, 0, 0, 0, 0, 0, 0));
    const CalibratedImuSample thumbSample = pipeline.calibrate(
        makeRawFrame(SensorId::Thumb, 1, 16384, 0, 0, 0, 0, 0, 0, 0, 0));

    QVERIFY(near(double(wristSample.accelerationMps2.x()), 4.0 * 9.80665, 1.0e-3));
    QVERIFY(near(double(thumbSample.accelerationMps2.x()), 8.0 * 9.80665, 1.0e-3));
}

void CalibrationTest::staticGyroBiasConvergesAtRest()
{
    StaticGyroBiasEstimator estimator;
    const QVector3D accel(0.0f, 0.0f, 9.80665f);
    const QVector3D bias(0.01f, -0.005f, 0.002f);
    for (int i = 0; i < 400; ++i) {
        estimator.update(accel, bias, 0.005);
    }
    QVERIFY(estimator.converged());
    const QVector3D estimate = estimator.bias();
    QVERIFY(near(double(estimate.x()), 0.01, 1.0e-4));
    QVERIFY(near(double(estimate.y()), -0.005, 1.0e-4));
    QVERIFY(near(double(estimate.z()), 0.002, 1.0e-4));
}

void CalibrationTest::staticGyroBiasFreezesDuringMotion()
{
    StaticGyroBiasEstimator estimator;
    const QVector3D accel(0.0f, 0.0f, 9.80665f);
    const QVector3D bias(0.01f, -0.005f, 0.002f);
    for (int i = 0; i < 400; ++i) {
        estimator.update(accel, bias, 0.005);
    }
    QVERIFY(estimator.converged());
    const QVector3D before = estimator.bias();

    const QVector3D motionGyro(1.5f, -1.0f, 0.8f);
    for (int i = 0; i < 200; ++i) {
        estimator.update(accel, motionGyro, 0.005);
    }
    QVERIFY(!estimator.isRest());
    const QVector3D after = estimator.bias();
    QVERIFY(near(double(before.x()), double(after.x()), 1.0e-9));
    QVERIFY(near(double(before.y()), double(after.y()), 1.0e-9));
    QVERIFY(near(double(before.z()), double(after.z()), 1.0e-9));
}

void CalibrationTest::magneticHardSoftIronCalibrationAndValidation()
{
    MagneticCalibration calibration;
    const QVector3D center(10.0f, -5.0f, 3.0f);
    const double radius = 20.0;
    const int pairs = 120;
    for (int i = 0; i < pairs; ++i) {
        const double theta = 2.0 * Pi * double(i) / double(pairs);
        const double phi = std::acos(2.0 * double(i % 60) / 60.0 - 1.0);
        const double ux = std::sin(phi) * std::cos(theta);
        const double uy = std::sin(phi) * std::sin(theta);
        const double uz = std::cos(phi);
        calibration.addSample(QVector3D(float(double(center.x()) + radius * ux),
                                        float(double(center.y()) + radius * uy),
                                        float(double(center.z()) + radius * uz)));
        calibration.addSample(QVector3D(float(double(center.x()) - radius * ux),
                                        float(double(center.y()) - radius * uy),
                                        float(double(center.z()) - radius * uz)));
    }

    const MagneticCalibration::Result result = calibration.compute();
    QVERIFY2(result.valid, qPrintable(result.error));
    QVERIFY(near(double(result.hardIronMicroTesla.x()), double(center.x()), 0.5));
    QVERIFY(near(double(result.hardIronMicroTesla.y()), double(center.y()), 0.5));
    QVERIFY(near(double(result.hardIronMicroTesla.z()), double(center.z()), 0.5));
    QVERIFY(result.softIron[0] > 0.0);
    QVERIFY(result.softIron[4] > 0.0);
    QVERIFY(result.softIron[8] > 0.0);

    // Applying the correction to the center maps it near zero.
    const QVector3D corrected = MagneticCalibration::apply(center, result.hardIronMicroTesla,
                                                           result.softIron);
    QVERIFY(near(double(corrected.x()), 0.0, 0.5));
    QVERIFY(near(double(corrected.y()), 0.0, 0.5));
    QVERIFY(near(double(corrected.z()), 0.0, 0.5));
}

void CalibrationTest::magneticCalibrationRejectsInsufficientOrDegenerate()
{
    MagneticCalibration insufficient;
    for (int i = 0; i < 5; ++i) {
        insufficient.addSample(QVector3D(float(i), float(i), float(i)));
    }
    QVERIFY(!insufficient.compute().valid);

    MagneticCalibration degenerate;
    for (int i = 0; i < 30; ++i) {
        degenerate.addSample(QVector3D(5.0f, 5.0f, 5.0f));  // zero spread
    }
    const MagneticCalibration::Result result = degenerate.compute();
    QVERIFY(!result.valid);
    QVERIFY(!result.error.isEmpty());
}

void CalibrationTest::calibrationPersistenceRoundTrip()
{
    CalibrationDocument document = CalibrationDocument::defaults(QStringLiteral("glove-001"));
    document.sensors[0].accelerometerRangeG = 8.0;
    document.sensors[1].gyroBiasValid = true;
    document.sensors[1].gyroBiasRadPerSec = QVector3D(0.01f, -0.005f, 0.002f);
    document.sensors[2].magnetometerCalibrated = true;
    document.sensors[2].magnetometerHardIronMicroTesla = QVector3D(10.0f, -5.0f, 3.0f);

    QVector<Diagnostic> diagnostics;
    const QByteArray json = saveCalibration(document, &diagnostics);
    QVERIFY2(!json.isEmpty(), "saveCalibration produced empty JSON");
    QVERIFY(diagnostics.isEmpty());

    const CalibrationLoadResult loaded = loadCalibration(json);
    QVERIFY2(loaded.success, "loadCalibration failed");
    QCOMPARE(loaded.document.deviceId, QStringLiteral("glove-001"));
    QCOMPARE(loaded.document.schemaVersion, CalibrationSchemaVersion);
    QVERIFY(near(loaded.document.sensors[0].accelerometerRangeG, 8.0, 1.0e-9));
    QCOMPARE(loaded.document.sensors[1].gyroBiasValid, true);
    QVERIFY(near(double(loaded.document.sensors[1].gyroBiasRadPerSec.x()), 0.01, 1.0e-6));
    QCOMPARE(loaded.document.sensors[2].magnetometerCalibrated, true);
}

void CalibrationTest::calibrationPersistenceRejectsBadSchema()
{
    const QByteArray bad = R"({"schemaVersion": 999, "deviceId": "x", "sensors": []})";
    const CalibrationLoadResult loaded = loadCalibration(bad);
    QVERIFY(!loaded.success);
    QVERIFY(!loaded.diagnostics.isEmpty());
}

void CalibrationTest::calibrationPersistenceRequiresDeviceId()
{
    CalibrationDocument document = CalibrationDocument::defaults(QString());
    QVector<Diagnostic> diagnostics;
    const QByteArray json = saveCalibration(document, &diagnostics);
    QVERIFY(json.isEmpty());
    QVERIFY(!diagnostics.isEmpty());
}

void CalibrationTest::invalidParamsProduceInvalidSample()
{
    CalibrationPipeline pipeline;
    pipeline.setRestBiasEstimationEnabled(false);

    SensorCalibrationParams params = SensorCalibrationParams::defaults(SensorId::Wrist);
    params.accelerometerAxes.matrix = {{{1, 1, 0}, {0, 1, 0}, {0, 0, 1}}};
    pipeline.setParams(SensorId::Wrist, params);

    const CalibratedImuSample sample = pipeline.calibrate(
        makeRawFrame(SensorId::Wrist, 1, 16384, 0, 0, 0, 0, 0, 120, 0, 0));
    QVERIFY(!sample.valid);
    QCOMPARE(int(sample.calibrationState), int(CalibrationState::Invalid));
    QVERIFY(!pipeline.takeDiagnostics().isEmpty());
}

QTEST_APPLESS_MAIN(CalibrationTest)
#include "test_calibration.moc"
