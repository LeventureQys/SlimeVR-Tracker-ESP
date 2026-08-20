#include <QtTest>

#include "model/model_data.h"
#include "model/model_importer.h"
#include "model/standard_joints.h"

#include <QDir>
#include <QHash>
#include <QQuaternion>
#include <QSet>

#include <algorithm>
#include <cmath>

#ifndef HANDSTUDIO_TEST_GLB_PATH
#error "HANDSTUDIO_TEST_GLB_PATH must be defined"
#endif

namespace {

const char *GlbPath = HANDSTUDIO_TEST_GLB_PATH;

bool matricesNear(const QMatrix4x4 &a, const QMatrix4x4 &b, float tolerance)
{
    const float *da = a.constData();
    const float *db = b.constData();
    for (int index = 0; index < 16; ++index) {
        if (std::abs(da[index] - db[index]) > tolerance) {
            return false;
        }
    }
    return true;
}

// 语义父链：metacarpal 的父为 wrist；其余由 19 条语义骨段给出。
QHash<QString, QString> semanticParents()
{
    QHash<QString, QString> parents;
    for (const handstudio::SemanticBoneSegment &segment : handstudio::semanticBoneSegments()) {
        parents.insert(segment.end, segment.start);
    }
    parents.insert(QStringLiteral("thumb-metacarpal"), QStringLiteral("wrist"));
    parents.insert(QStringLiteral("index-finger-metacarpal"), QStringLiteral("wrist"));
    parents.insert(QStringLiteral("middle-finger-metacarpal"), QStringLiteral("wrist"));
    parents.insert(QStringLiteral("ring-finger-metacarpal"), QStringLiteral("wrist"));
    parents.insert(QStringLiteral("pinky-finger-metacarpal"), QStringLiteral("wrist"));
    return parents;
}

QSet<QString> subtreeOf(const QString &root, const QHash<QString, QString> &parents)
{
    QSet<QString> result{root};
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = parents.constBegin(); it != parents.constEnd(); ++it) {
            if (result.contains(it.value()) && !result.contains(it.key())) {
                result.insert(it.key());
                changed = true;
            }
        }
    }
    return result;
}

} // namespace

class GlbModelTest final : public QObject {
    Q_OBJECT

private slots:
    void standardJointAndSegmentCounts();
    void fileLoadsAndJointContract();
    void meshStatistics();
    void weightContract();
    void bindPoseSkinnedInvariance();
    void distalRotationOnlyAffectsSubtree();
    void invalidModelRejected();
};

void GlbModelTest::standardJointAndSegmentCounts()
{
    QCOMPARE(handstudio::standardJointNames().size(), 25);
    QCOMPARE(handstudio::semanticBoneSegments().size(), 19);
    QVERIFY(handstudio::standardJointNames().contains(QStringLiteral("wrist")));
}

void GlbModelTest::fileLoadsAndJointContract()
{
    const handstudio::ModelLoadResult result = handstudio::ModelImporter{}.load(GlbPath);
    QVERIFY2(result.hasValue(), result.hasError() ? qPrintable(result.error->detail) : "no error");
    const handstudio::RiggedModel &model = *result.value;

    QCOMPARE(model.bones.size(), 25);
    QCOMPARE(model.meshes.size(), 1);
    QCOMPARE(model.boneIndexByName.size(), 25);

    // 25 个标准关节全部齐全且唯一。
    QSet<QString> seen;
    for (const QString &name : handstudio::standardJointNames()) {
        QVERIFY2(model.boneIndexByName.contains(name), qPrintable(name));
        QVERIFY2(!seen.contains(name), qPrintable(name));
        seen.insert(name);
    }
    // palette 顺序：wrist 最后（与 Python skin_joint_names[-1] == 'wrist' 一致）。
    QCOMPARE(model.bones.last().name, QStringLiteral("wrist"));
    QCOMPARE(model.boneIndexByName.value(QStringLiteral("wrist")), 24);

    for (const handstudio::BoneData &bone : model.bones) {
        QVERIFY(!bone.name.isEmpty());
        const float *bindWorld = bone.bindWorld.constData();
        const float *inverseBind = bone.inverseBind.constData();
        for (int index = 0; index < 16; ++index) {
            QVERIFY(std::isfinite(bindWorld[index]));
            QVERIFY(std::isfinite(inverseBind[index]));
        }
        QVERIFY(std::abs(bone.inverseBind(3, 0)) < 1.0e-4F);
        QVERIFY(std::abs(bone.inverseBind(3, 1)) < 1.0e-4F);
        QVERIFY(std::abs(bone.inverseBind(3, 2)) < 1.0e-4F);
        QVERIFY(std::abs(bone.inverseBind(3, 3) - 1.0F) < 1.0e-4F);
    }
}

void GlbModelTest::meshStatistics()
{
    const handstudio::ModelLoadResult result = handstudio::ModelImporter{}.load(GlbPath);
    QVERIFY(result.hasValue());
    const handstudio::RiggedModel &model = *result.value;

    int vertexCount = 0;
    int indexCount = 0;
    for (const handstudio::MeshData &mesh : model.meshes) {
        vertexCount += mesh.vertices.size();
        indexCount += mesh.indices.size();
    }
    QCOMPARE(vertexCount, 1360);
    QCOMPARE(indexCount, 6942);
}

void GlbModelTest::weightContract()
{
    const handstudio::ModelLoadResult result = handstudio::ModelImporter{}.load(GlbPath);
    QVERIFY(result.hasValue());
    const handstudio::RiggedModel &model = *result.value;

    for (const handstudio::MeshData &mesh : model.meshes) {
        for (const handstudio::Vertex &vertex : mesh.vertices) {
            float sum = 0.0F;
            for (int influence = 0; influence < 4; ++influence) {
                const int jointIndex =
                    vertex.influence.boneIndices[static_cast<std::size_t>(influence)];
                const float weight = vertex.influence.weights[static_cast<std::size_t>(influence)];
                QVERIFY(jointIndex >= 0 && jointIndex < 25);
                QVERIFY(std::isfinite(weight) && weight >= 0.0F && weight <= 1.0F);
                sum += weight;
            }
            QVERIFY(std::abs(sum - 1.0F) < 1.0e-4F);
        }
    }
}

void GlbModelTest::bindPoseSkinnedInvariance()
{
    const handstudio::ModelLoadResult result = handstudio::ModelImporter{}.load(GlbPath);
    QVERIFY(result.hasValue());
    const handstudio::RiggedModel &model = *result.value;

    // 固定显示根变换 + wrist 单位四元数（即 root = fixedTransform）。
    const QMatrix4x4 displayRoot = handstudio::computeDisplayRootTransform(model);
    const QVector<QMatrix4x4> skin = handstudio::computeSkinMatrices(model, displayRoot);

    float maxError = 0.0F;
    for (const handstudio::MeshData &mesh : model.meshes) {
        for (const handstudio::Vertex &vertex : mesh.vertices) {
            const QVector3D skinned = handstudio::skinVertex(vertex, skin);
            const QVector3D expected = displayRoot.map(vertex.position);
            maxError = std::max(maxError, (skinned - expected).length());
        }
    }
    qInfo().noquote() << QStringLiteral("bind-pose max skinning error = %1 (threshold 1e-4)")
                             .arg(maxError, 0, 'e', 3);
    QVERIFY2(maxError < 1.0e-4F,
             qPrintable(QStringLiteral("bind 不变性失败，最大误差 %1").arg(maxError)));
}

void GlbModelTest::distalRotationOnlyAffectsSubtree()
{
    const handstudio::ModelLoadResult result = handstudio::ModelImporter{}.load(GlbPath);
    QVERIFY(result.hasValue());
    const handstudio::RiggedModel &model = *result.value;

    const QMatrix4x4 displayRoot = handstudio::computeDisplayRootTransform(model);
    const QVector<QMatrix4x4> bindSkin = handstudio::computeSkinMatrices(model, displayRoot);

    // 绑定显示空间世界矩阵与位置。
    QHash<QString, QMatrix4x4> bindWorld;
    QHash<QString, QVector3D> bindPosition;
    for (const handstudio::BoneData &bone : model.bones) {
        const QMatrix4x4 world = displayRoot * bone.bindWorld;
        bindWorld.insert(bone.name, world);
        bindPosition.insert(bone.name, world.map(QVector3D(0.0F, 0.0F, 0.0F)));
    }

    const QString pivot = QStringLiteral("thumb-phalanx-distal");
    const QVector3D pivotPosition = bindPosition.value(pivot);
    const QQuaternion rotation = QQuaternion::fromAxisAndAngle(QVector3D(0.0F, 1.0F, 0.0F), 30.0F);
    const QMatrix4x4 rotateAboutPivot =
        handstudio::translationMatrix(pivotPosition) * handstudio::quaternionToMatrix(rotation)
        * handstudio::translationMatrix(-pivotPosition);

    const QHash<QString, QString> parents = semanticParents();
    const QSet<QString> subtree = subtreeOf(pivot, parents);

    // 新的显示世界矩阵：仅子树关节被旋转。
    QHash<QString, QMatrix4x4> newWorld;
    for (const handstudio::BoneData &bone : model.bones) {
        newWorld.insert(bone.name,
                        subtree.contains(bone.name) ? rotateAboutPivot * bindWorld.value(bone.name)
                                                    : bindWorld.value(bone.name));
    }

    QVector<QMatrix4x4> newSkin(model.bones.size());
    for (int index = 0; index < model.bones.size(); ++index) {
        newSkin[index] = newWorld.value(model.bones[index].name) * model.bones[index].inverseBind;
    }

    // 子树外的蒙皮矩阵不变。
    for (int index = 0; index < model.bones.size(); ++index) {
        if (!subtree.contains(model.bones[index].name)) {
            QVERIFY2(matricesNear(bindSkin[index], newSkin[index], 1.0e-5F),
                     qPrintable(model.bones[index].name));
        }
    }

    // 子树关节确实发生变化（pivot 自身）。
    const int pivotIndex = model.boneIndexByName.value(pivot);
    QVERIFY(!matricesNear(bindSkin[pivotIndex], newSkin[pivotIndex], 1.0e-5F));

    // 所有语义骨段长度保持不变（刚性旋转）。
    for (const handstudio::SemanticBoneSegment &segment : handstudio::semanticBoneSegments()) {
        const QVector3D bindLength = bindPosition.value(segment.end) - bindPosition.value(segment.start);
        const QVector3D newStart = newWorld.value(segment.start).map(QVector3D(0.0F, 0.0F, 0.0F));
        const QVector3D newEnd = newWorld.value(segment.end).map(QVector3D(0.0F, 0.0F, 0.0F));
        const QVector3D newLength = newEnd - newStart;
        QVERIFY2(std::abs(bindLength.length() - newLength.length()) < 1.0e-4F,
                 qPrintable(QStringLiteral("%1-%2 骨长不守恒").arg(segment.start, segment.end)));
    }
}

void GlbModelTest::invalidModelRejected()
{
    // 不存在的文件。
    const handstudio::ModelLoadResult missing =
        handstudio::ModelImporter{}.load(QStringLiteral("Z:/definitely/not/here.glb"));
    QVERIFY(missing.hasError());
    QCOMPARE(missing.error->code, handstudio::ModelLoadErrorCode::FileNotFound);

    // 非法内容（非 GLB 魔数）。写到当前工作目录（构建目录，workspace 可写），
    // 避免依赖沙箱临时目录（QTemporaryDir 在受限环境下可能不可写）。
    const QString garbagePath = QDir::current().filePath(QStringLiteral("__glb_garbage_tmp.glb"));
    {
        QFile garbage(garbagePath);
        if (garbage.open(QIODevice::WriteOnly)) {
            garbage.write("this is not a glb file, just some garbage bytes to trigger a parse failure");
            garbage.close();
        }
    }
    const handstudio::ModelLoadResult invalid = handstudio::ModelImporter{}.load(garbagePath);
    QFile::remove(garbagePath);
    QVERIFY(invalid.hasError());
    QVERIFY(invalid.error->code == handstudio::ModelLoadErrorCode::ParseFailed
            || invalid.error->code == handstudio::ModelLoadErrorCode::UnsupportedVersion);
}

QTEST_APPLESS_MAIN(GlbModelTest)
#include "test_glb_model.moc"
