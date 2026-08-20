#include "hand_skeleton_model.h"
#include "hand_skeleton_pipeline.h"

#include <QtTest>

#include <cmath>

namespace {

QString modelPath()
{
    return QString::fromUtf8(HAND_SKELETON_TEST_MODEL);
}

QString configPath()
{
    return QString::fromUtf8(HAND_SKELETON_TEST_CONFIG);
}

SixImuSnapshot snapshot(qint64 timestampNs, float flexionDegrees)
{
    SixImuSnapshot result;
    result.sequence = 1;
    result.updatedMonotonicNs = timestampNs;
    const QQuaternion palm = QQuaternion::fromAxisAndAngle(0.0F, 1.0F, 0.0F, 20.0F);
    for (int index = 0; index < SixImuProtocol::SensorCount; ++index) {
        SensorPose &pose = result.poses[size_t(index)];
        pose.sensorId = static_cast<SensorId>(static_cast<quint8>(SensorId::Wrist) + index);
        pose.worldOrientation = palm;
        pose.valid = true;
        pose.updatedMonotonicNs = timestampNs;
    }
    result.poses[2].worldOrientation = palm
        * QQuaternion::fromAxisAndAngle(0.0F, 0.0F, 1.0F, flexionDegrees);
    return result;
}

bool finiteMatrix(const QMatrix4x4 &matrix)
{
    const float *values = matrix.constData();
    for (int index = 0; index < 16; ++index) {
        if (!std::isfinite(values[index])) {
            return false;
        }
    }
    return true;
}

float maximumIdentityError(const QMatrix4x4 &matrix)
{
    const QMatrix4x4 identity;
    float maximum = 0.0F;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            maximum = std::max(maximum, std::abs(matrix(row, column) - identity(row, column)));
        }
    }
    return maximum;
}

} // namespace

class HandSkeletonModelTest final : public QObject {
    Q_OBJECT

private slots:
    void loadsRealModelAndConfiguration();
    void bindPoseProducesIdentitySkinMatrices();
    void drivesRealIndexFingerChain();
    void reportsMissingConfiguration();
};

void HandSkeletonModelTest::loadsRealModelAndConfiguration()
{
    HandSkeleton::ModelSetupResult result = HandSkeleton::ModelLoader::load(modelPath(), configPath());
    QVERIFY2(result, qPrintable(result.error ? result.error->detail : QString()));
    QCOMPARE(result.value->model.bones.size(), 20);
    QCOMPARE(result.value->binding.bones.size(), 20);
    QCOMPARE(result.value->config.fingers[1].name, QString("index"));
    QCOMPARE(result.value->config.fingers[1].bones.size(), 4);
    QCOMPARE(result.value->config.rootBone, QString("Bone"));
    for (int index = 1; index < result.value->binding.bones.size(); ++index) {
        QVERIFY(result.value->binding.bones[index].parentIndex >= 0);
        QVERIFY(result.value->binding.bones[index].parentIndex < index);
    }
}

void HandSkeletonModelTest::bindPoseProducesIdentitySkinMatrices()
{
    HandSkeleton::ModelSetupResult result = HandSkeleton::ModelLoader::load(modelPath(), configPath());
    QVERIFY(result);
    handdemo::motion::PoseSolver solver(result.value->binding, result.value->config);
    const handdemo::motion::PoseResult bind = solver.solve(solver.bindPose());
    QCOMPARE(bind.skinMatrices.size(), 20);
    for (const QMatrix4x4 &matrix : bind.skinMatrices) {
        QVERIFY(finiteMatrix(matrix));
        QVERIFY(maximumIdentityError(matrix) < 0.001F);
    }
}

void HandSkeletonModelTest::drivesRealIndexFingerChain()
{
    HandSkeleton::ModelSetupResult result = HandSkeleton::ModelLoader::load(modelPath(), configPath());
    QVERIFY(result);
    HandSkeleton::Pipeline pipeline(result.value->binding, result.value->config);
    const HandSkeletonFrame neutral = pipeline.process(snapshot(1'000'000, 0.0F));
    const HandSkeletonFrame flexed = pipeline.process(snapshot(2'000'000, 60.0F));
    QVERIFY(neutral.frameApplied);
    QVERIFY(flexed.frameApplied);
    QCOMPARE(flexed.bones.size(), 20);

    for (const QString &boneName : result.value->config.fingers[1].bones) {
        int index = -1;
        for (int candidate = 0; candidate < flexed.bones.size(); ++candidate) {
            if (flexed.bones[candidate].boneName == boneName) {
                index = candidate;
                break;
            }
        }
        QVERIFY(index >= 0);
        QVERIFY(finiteMatrix(flexed.bones[index].globalMatrix));
        QVERIFY(finiteMatrix(flexed.bones[index].skinMatrix));
    }

    const QString baseName = result.value->config.fingers[1].bones.front();
    int baseIndex = -1;
    for (int candidate = 0; candidate < flexed.bones.size(); ++candidate) {
        if (flexed.bones[candidate].boneName == baseName) {
            baseIndex = candidate;
            break;
        }
    }
    QVERIFY(baseIndex >= 0);
    QVERIFY(neutral.bones[baseIndex].localRotation != flexed.bones[baseIndex].localRotation);
}

void HandSkeletonModelTest::reportsMissingConfiguration()
{
    const HandSkeleton::ModelSetupResult result
        = HandSkeleton::ModelLoader::load(modelPath(), QStringLiteral("missing_hand_rig.json"));
    QVERIFY(!result);
    QVERIFY(result.error.has_value());
    QCOMPARE(result.error->code, QString("config_open_failed"));
}

QTEST_GUILESS_MAIN(HandSkeletonModelTest)
#include "test_hand_skeleton_model.moc"
