#include "model/model_importer.h"
#include "skeleton/kinematic_skeleton.h"

#include <QFile>
#include <QtTest>

#include <cmath>

using namespace handstudio;

class TestSkeletonSolver : public QObject {
    Q_OBJECT
private slots:
    void bindPoseLimitsDeterminismAndMissingFrames();
};

static HandObservationFrame observation(qint64 timestamp, float flexion, bool valid=true)
{
    HandObservationFrame frame;
    frame.sequence=1; frame.timestampNs=timestamp; frame.wristWorldOrientation=QQuaternion();
    for (int index=0; index<5; ++index) {
        auto &finger=frame.fingers[std::size_t(index)]; finger.sensorId=AllSensorIds[std::size_t(index+1)];
        finger.valid=valid; finger.flexionDegrees=flexion; finger.confidence=1.0F;
    }
    return frame;
}

void TestSkeletonSolver::bindPoseLimitsDeterminismAndMissingFrames()
{
    ModelImporter importer; auto loaded=importer.load(QStringLiteral(HANDSTUDIO_TEST_GLB_PATH)); QVERIFY(loaded.hasValue());
    QFile file(QStringLiteral(HANDSTUDIO_TEST_RIG_PATH)); QVERIFY(file.open(QIODevice::ReadOnly));
    auto rig=loadHandRigConfig(file.readAll(), *loaded.value); QVERIFY(rig.config.has_value());
    KinematicSkeleton solver(*loaded.value, *rig.config); QVERIFY2(solver.isValid(), qPrintable(solver.errorString()));
    auto bind=solver.solve(observation(1000000,0)); QCOMPARE(bind.bones.size(),25);
    const QMatrix4x4 displayRoot = computeDisplayRootTransform(*loaded.value);
    QCOMPARE(bind.rootTransform, displayRoot);
    QVERIFY(!bind.rootTransform.isIdentity());
    for (int boneIndex = 0; boneIndex < bind.bones.size(); ++boneIndex) {
        const int paletteIndex = loaded.value->boneIndexByName.value(bind.bones[boneIndex].boneName, -1);
        QVERIFY(paletteIndex >= 0);
        const QMatrix4x4 expectedGlobal = displayRoot * loaded.value->bones[paletteIndex].bindWorld;
        const QVector3D actualPosition = bind.bones[boneIndex].globalMatrix.column(3).toVector3D();
        const QVector3D expectedPosition = expectedGlobal.column(3).toVector3D();
        QVERIFY2((actualPosition - expectedPosition).length() <= 1.0e-4F,
                 qPrintable(QStringLiteral("global joint position mismatch for %1").arg(bind.bones[boneIndex].boneName)));
        const QMatrix4x4 expectedSkin = bind.bones[boneIndex].globalMatrix
                                       * loaded.value->bones[paletteIndex].inverseBind;
        for (int element = 0; element < 16; ++element) {
            QVERIFY(std::abs(bind.bones[boneIndex].skinMatrix.constData()[element]
                             - expectedSkin.constData()[element]) <= 1.0e-4F);
        }
    }
    auto bent=solver.solve(observation(2000000,200));
    for(const auto &bone:bent.bones){ QVERIFY(bone.valid); QVERIFY(bone.source==BoneSource::Estimated); }
    auto repeat=solver.solve(observation(2000000,200));
    for(int i=0;i<bent.bones.size();++i) QCOMPARE(repeat.bones[i].globalMatrix,bent.bones[i].globalMatrix);
    auto held=solver.solve(observation(100000000,false,false));
    QVERIFY(held.bones[1].source==BoneSource::Held);
    auto returned=solver.solve(observation(700000000,0,false));
    QVERIFY(returned.bones[1].localRotation.isIdentity());
    auto recovered=solver.solve(observation(710000000,90,true));
    QVERIFY(recovered.bones[1].source==BoneSource::Recovered);
    auto frozenInput=observation(720000000,0,true); frozenInput.wristWorldOrientation=QQuaternion(0,0,0,0);
    auto frozen=solver.solve(frozenInput); QCOMPARE(frozen.bones[1].globalMatrix,recovered.bones[1].globalMatrix);
}

QTEST_APPLESS_MAIN(TestSkeletonSolver)
#include "test_skeleton_solver.moc"
