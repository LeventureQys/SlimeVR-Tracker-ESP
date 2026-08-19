#include "import/model_importer.h"
#include "test_paths.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <limits>

using namespace handrig;

class TestModelImporter : public QObject {
    Q_OBJECT

private slots:
    void loadsResearchModel();
    void animatedSkinActsAfterMeshBinding();
    void rejectsMissingFile();
    void rejectsInvalidFile();
    void rejectsModelWithoutBones();
};

void TestModelImporter::animatedSkinActsAfterMeshBinding()
{
    RiggedModel model;
    model.globalInverse.setToIdentity();
    model.bones.resize(1);
    MeshData mesh;
    mesh.boneOffsets.resize(1);
    mesh.boneOffsets[0].setToIdentity();
    mesh.bindTransform.setToIdentity();
    mesh.bindTransform.translate(10.0F, 0.0F, 0.0F);
    QMatrix4x4 animatedBone;
    animatedBone.setToIdentity();
    animatedBone.rotate(90.0F, 0.0F, 0.0F, 1.0F);

    const QVector3D transformed = meshSkinTransform(model, mesh, animatedBone, 0)
                                      .map(QVector3D());
    QVERIFY(qAbs(transformed.x()) < 1.0e-5F);
    QVERIFY(qAbs(transformed.y() - 10.0F) < 1.0e-5F);
}

void TestModelImporter::loadsResearchModel()
{
    const ModelLoadResult result = ModelImporter().load(QString::fromUtf8(HAND_RIG_TEST_MODEL_PATH));
    QVERIFY2(result.hasValue(), qPrintable(result.error ? result.error->detail : QString()));
    const RiggedModel &model = *result.value;
    QVERIFY(!model.meshes.isEmpty());
    QCOMPARE(model.bones.size(), 20);
    QCOMPARE(model.bones.first().parentIndex, -1);

    qsizetype vertexCount = 0;
    qsizetype indexCount = 0;
    QVector3D meshMinimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    QVector3D meshMaximum(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max());
    QVector3D boneMinimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                          std::numeric_limits<float>::max());
    QVector3D boneMaximum(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                          -std::numeric_limits<float>::max());
    QVector<QMatrix4x4> globalMatrices(model.bones.size());
    float maximumBindSkinError = 0.0F;
    for (qsizetype boneIndex = 1; boneIndex < model.bones.size(); ++boneIndex) {
        QVERIFY(model.bones[boneIndex].parentIndex >= 0);
        QVERIFY(model.bones[boneIndex].parentIndex < boneIndex);
    }
    for (qsizetype boneIndex = 0; boneIndex < model.bones.size(); ++boneIndex) {
        const BoneData &bone = model.bones[boneIndex];
        globalMatrices[boneIndex] = bone.parentIndex >= 0
            ? globalMatrices[bone.parentIndex] * bone.bindLocal : bone.bindLocal;
        const QVector3D bonePosition = (model.globalInverse * globalMatrices[boneIndex]).map(QVector3D());
        for (int axis = 0; axis < 3; ++axis) {
            boneMinimum[axis] = std::min(boneMinimum[axis], bonePosition[axis]);
            boneMaximum[axis] = std::max(boneMaximum[axis], bonePosition[axis]);
        }
        const QMatrix4x4 skin = model.globalInverse * globalMatrices[boneIndex] * bone.inverseBind;
        const QMatrix4x4 identity;
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                maximumBindSkinError = std::max(maximumBindSkinError,
                    std::abs(skin(row, column) - identity(row, column)));
            }
        }
    }
    for (const MeshData &mesh : model.meshes) {
        QCOMPARE(mesh.boneOffsets.size(), model.bones.size());
        vertexCount += mesh.vertices.size();
        indexCount += mesh.indices.size();
        QSet<int> referencedBones;
        for (const Vertex &vertex : mesh.vertices) {
            float sum = 0.0F;
            int influenceCount = 0;
            for (std::size_t index = 0; index < vertex.influence.weights.size(); ++index) {
                const float weight = vertex.influence.weights[index];
                sum += weight;
                if (weight > 0.0F) {
                    referencedBones.insert(vertex.influence.boneIndices[index]);
                    ++influenceCount;
                    QVERIFY(vertex.influence.boneIndices[index] >= 0);
                    QVERIFY(vertex.influence.boneIndices[index] < model.bones.size());
                }
            }
            if (sum > 0.0F) {
                QVERIFY(std::abs(sum - 1.0F) <= 1.0e-5F);
            }
            QVERIFY(influenceCount <= 4);
            QVector3D bindPosition;
            for (std::size_t index = 0; index < vertex.influence.weights.size(); ++index) {
                const float weight = vertex.influence.weights[index];
                const int boneIndex = vertex.influence.boneIndices[index];
                if (weight > 0.0F && boneIndex >= 0) {
                    bindPosition += meshSkinTransform(model, mesh, globalMatrices[boneIndex], boneIndex)
                                        .map(vertex.position) * weight;
                }
            }
            for (int axis = 0; axis < 3; ++axis) {
                meshMinimum[axis] = std::min(meshMinimum[axis], bindPosition[axis]);
                meshMaximum[axis] = std::max(meshMaximum[axis], bindPosition[axis]);
            }
        }
        for (int boneIndex : referencedBones) {
            const QMatrix4x4 bindSkin = meshSkinTransform(
                model, mesh, globalMatrices[boneIndex], boneIndex);
            float maximumMeshError = 0.0F;
            for (int row = 0; row < 4; ++row) {
                for (int column = 0; column < 4; ++column) {
                    maximumMeshError = std::max(maximumMeshError,
                        std::abs(bindSkin(row, column) - mesh.bindTransform(row, column)));
                }
            }
            QVERIFY2(maximumMeshError < 0.001F,
                     qPrintable(QStringLiteral("mesh bind mismatch: %1").arg(maximumMeshError)));
        }
    }
    qInfo().noquote() << QStringLiteral("研究模型统计：网格 %1，骨骼 %2，顶点 %3，索引 %4，警告 %5")
                             .arg(model.meshes.size()).arg(model.bones.size())
                              .arg(vertexCount).arg(indexCount).arg(result.warnings.size());
    QVERIFY(maximumBindSkinError < 0.001F);
    const float meshDiagonal = (meshMaximum - meshMinimum).length();
    const float boneDiagonal = (boneMaximum - boneMinimum).length();
    QVERIFY(meshDiagonal > 0.0F);
    QVERIFY(boneDiagonal > 0.0F);
    const float scaleRatio = meshDiagonal / boneDiagonal;
    QVERIFY2(scaleRatio > 0.25F && scaleRatio < 4.0F,
             qPrintable(QStringLiteral("mesh/bone scale mismatch: %1").arg(scaleRatio)));
}

void TestModelImporter::rejectsMissingFile()
{
    const ModelLoadResult result = ModelImporter().load(QStringLiteral("definitely_missing.fbx"));
    QVERIFY(!result.hasValue());
    QVERIFY(result.error.has_value());
    QCOMPARE(result.error->code, ModelLoadErrorCode::FileNotFound);
}

void TestModelImporter::rejectsInvalidFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("invalid.fbx")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not a model"), 11);
    file.close();

    const ModelLoadResult result = ModelImporter().load(file.fileName());
    QVERIFY(!result.hasValue());
    QVERIFY(result.error.has_value());
    QCOMPARE(result.error->code, ModelLoadErrorCode::ParseFailed);
}

void TestModelImporter::rejectsModelWithoutBones()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("triangle.obj")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray obj("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");
    QCOMPARE(file.write(obj), obj.size());
    file.close();

    const ModelLoadResult result = ModelImporter().load(file.fileName());
    QVERIFY(!result.hasValue());
    QVERIFY(result.error.has_value());
    QCOMPARE(result.error->code, ModelLoadErrorCode::NoBones);
}

QTEST_GUILESS_MAIN(TestModelImporter)

#include "test_model_importer.moc"
