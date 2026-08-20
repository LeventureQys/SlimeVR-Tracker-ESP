#pragma once

// 产品模型数据结构（SubStage 5）。
//
// RiggedModel 是「模型无关」的蒙皮网格 + 骨骼契约：
//   - bones 保持 GLB skin.joints 的 palette 原顺序（目标资产为 25 个关节，wrist 最后）。
//   - 导入器「不」根据 Assimp/GLB 的节点 parent 推导解剖父链；解剖链由 SubStage 4 的
//     虚拟层级（standard_joints.h / hand_rig_generic_left.json）建立。
//   - bindWorld 是绑定姿态下关节在场景空间的世界矩阵（用于 skinMatrix = root·bindWorld·inverseBind）。
//   - 显示根变换（固定）与 Python 参考 hand_pose.py 的 fixed_transform 语义一致。

#include <QColor>
#include <QHash>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector2D>
#include <QVector3D>

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace handstudio {

constexpr int HandSkinJointCount = 25;
constexpr int MaximumBoneCount = 128;

struct VertexInfluence {
    std::array<int, 4> boneIndices{-1, -1, -1, -1};
    std::array<float, 4> weights{0.0F, 0.0F, 0.0F, 0.0F};
};

struct Vertex {
    QVector3D position;
    QVector3D normal;
    QVector2D texCoord;
    VertexInfluence influence;
};

struct MeshData {
    QVector<Vertex> vertices;
    QVector<quint32> indices;
    int materialIndex{-1};
};

struct BoneData {
    QString name;
    QVector3D bindTranslation;
    QQuaternion bindRotation;
    QVector3D bindScale{1.0F, 1.0F, 1.0F};
    QMatrix4x4 bindLocal;   // 关节节点局部 TRS 矩阵（T·R·S）
    QMatrix4x4 bindWorld;   // 绑定姿态关节世界矩阵（场景空间，含 Armature 路径）
    QMatrix4x4 inverseBind; // GLB skin inverseBindMatrix（palette 顺序）
};

struct MaterialData {
    QString name;
    QColor baseColor{190, 190, 190};
};

struct RiggedModel {
    QVector<MeshData> meshes;
    QVector<BoneData> bones;              // skin.joints palette 顺序
    QHash<QString, int> boneIndexByName;  // jointName -> paletteIndex
    QVector<MaterialData> materials;
    QString sourcePath;
};

enum class ModelLoadErrorCode {
    FileNotFound,
    ParseFailed,
    UnsupportedVersion,
    NoMeshes,
    MissingSkin,
    TooManyPrimitives,
    JointCountMismatch,
    MissingJoint,
    DuplicateJoint,
    MissingAttribute,
    InvalidInverseBind,
    InvalidJointIndex,
    InvalidWeight,
    InvalidIndex,
    NonFiniteMatrix,
    NonFiniteWeight,
};

struct ModelLoadError {
    ModelLoadErrorCode code{ModelLoadErrorCode::ParseFailed};
    QString message;
    QString detail;
};

template<typename Value, typename Error>
struct Result {
    std::optional<Value> value;
    std::optional<Error> error;
    QStringList warnings;

    [[nodiscard]] bool hasValue() const noexcept { return value.has_value(); }
    [[nodiscard]] bool hasError() const noexcept { return error.has_value(); }

    static Result success(Value result, QStringList resultWarnings = {})
    {
        Result output;
        output.value = std::move(result);
        output.warnings = std::move(resultWarnings);
        return output;
    }

    static Result failure(Error resultError, QStringList resultWarnings = {})
    {
        Result output;
        output.error = std::move(resultError);
        output.warnings = std::move(resultWarnings);
        return output;
    }
};

using ModelLoadResult = Result<RiggedModel, ModelLoadError>;

// ---- 矩阵工具（与 Python 参考 quaternion.py 语义一致，行主序构造） ----

// 单位四元数 -> 旋转矩阵（行主序公式，等价 quaternion.py::quat_to_mat4）。
inline QMatrix4x4 quaternionToMatrix(const QQuaternion &quaternion)
{
    const QQuaternion q = quaternion.normalized();
    const float w = q.scalar();
    const float x = q.x();
    const float y = q.y();
    const float z = q.z();
    return QMatrix4x4(
        1.0F - 2.0F * (y * y + z * z), 2.0F * (x * y - w * z), 2.0F * (x * z + w * y), 0.0F,
        2.0F * (x * y + w * z), 1.0F - 2.0F * (x * x + z * z), 2.0F * (y * z - w * x), 0.0F,
        2.0F * (x * z - w * y), 2.0F * (y * z + w * x), 1.0F - 2.0F * (x * x + y * y), 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F);
}

// M = T(t) · R(q) · S(s)（等价 quaternion.py::trs_to_mat4）。
inline QMatrix4x4 trsToMatrix(const QVector3D &translation, const QQuaternion &rotation,
                              const QVector3D &scale)
{
    const QMatrix4x4 r = quaternionToMatrix(rotation);
    const float sx = scale.x();
    const float sy = scale.y();
    const float sz = scale.z();
    return QMatrix4x4(
        r(0, 0) * sx, r(0, 1) * sy, r(0, 2) * sz, translation.x(),
        r(1, 0) * sx, r(1, 1) * sy, r(1, 2) * sz, translation.y(),
        r(2, 0) * sx, r(2, 1) * sy, r(2, 2) * sz, translation.z(),
        0.0F, 0.0F, 0.0F, 1.0F);
}

inline QMatrix4x4 translationMatrix(const QVector3D &translation)
{
    QMatrix4x4 m;
    m.setToIdentity();
    m(0, 3) = translation.x();
    m(1, 3) = translation.y();
    m(2, 3) = translation.z();
    return m;
}

inline QMatrix4x4 scaleMatrix(float scale)
{
    QMatrix4x4 m;
    m.setToIdentity();
    m(0, 0) = scale;
    m(1, 1) = scale;
    m(2, 2) = scale;
    return m;
}

// 固定显示根变换（设计文档 §5.2）：R = Rz(π) ⊗ Ry(-π/2)，缩放 7.05/maxDim，居中 +0.35 y。
inline QMatrix4x4 computeDisplayRootTransform(const RiggedModel &model)
{
    constexpr float DisplayMaxDim = 7.05F;
    constexpr float CenterOffsetY = 0.35F;

    const QQuaternion fingersUp = QQuaternion::fromAxisAndAngle(QVector3D(0.0F, 0.0F, 1.0F), 180.0F);
    const QQuaternion faceCamera = QQuaternion::fromAxisAndAngle(QVector3D(0.0F, 1.0F, 0.0F), -90.0F);
    const QQuaternion fixedRotation = (fingersUp * faceCamera).normalized();
    const QMatrix4x4 rotationMatrix = quaternionToMatrix(fixedRotation);

    QVector3D minimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max());
    QVector3D maximum(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max());
    bool hasVertex = false;
    for (const MeshData &mesh : model.meshes) {
        for (const Vertex &vertex : mesh.vertices) {
            const QVector3D rotated = rotationMatrix.mapVector(vertex.position);
            minimum.setX(std::min(minimum.x(), rotated.x()));
            minimum.setY(std::min(minimum.y(), rotated.y()));
            minimum.setZ(std::min(minimum.z(), rotated.z()));
            maximum.setX(std::max(maximum.x(), rotated.x()));
            maximum.setY(std::max(maximum.y(), rotated.y()));
            maximum.setZ(std::max(maximum.z(), rotated.z()));
            hasVertex = true;
        }
    }
    if (!hasVertex) {
        return translationMatrix(QVector3D());
    }

    const QVector3D span = maximum - minimum;
    const float maxDim = std::max({span.x(), span.y(), span.z()});
    const float scale = maxDim > 0.0F ? DisplayMaxDim / maxDim : 1.0F;

    QVector3D scaledMinimum(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                            std::numeric_limits<float>::max());
    QVector3D scaledMaximum(-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
                            -std::numeric_limits<float>::max());
    for (const MeshData &mesh : model.meshes) {
        for (const Vertex &vertex : mesh.vertices) {
            const QVector3D scaled = rotationMatrix.mapVector(vertex.position) * scale;
            scaledMinimum.setX(std::min(scaledMinimum.x(), scaled.x()));
            scaledMinimum.setY(std::min(scaledMinimum.y(), scaled.y()));
            scaledMinimum.setZ(std::min(scaledMinimum.z(), scaled.z()));
            scaledMaximum.setX(std::max(scaledMaximum.x(), scaled.x()));
            scaledMaximum.setY(std::max(scaledMaximum.y(), scaled.y()));
            scaledMaximum.setZ(std::max(scaledMaximum.z(), scaled.z()));
        }
    }

    const QVector3D center = (scaledMinimum + scaledMaximum) * 0.5F;
    const QVector3D offset(-center.x(), -center.y() + CenterOffsetY, -center.z());
    return translationMatrix(offset) * (rotationMatrix * scaleMatrix(scale));
}

// palette[i] = rootTransform · bindWorld_i · inverseBind_i（与 Python skin_matrices 一致）。
inline QVector<QMatrix4x4> computeSkinMatrices(const RiggedModel &model,
                                               const QMatrix4x4 &rootTransform)
{
    QVector<QMatrix4x4> palette(model.bones.size());
    for (int index = 0; index < model.bones.size(); ++index) {
        palette[index] = rootTransform * model.bones[index].bindWorld
                         * model.bones[index].inverseBind;
    }
    return palette;
}

// CPU 蒙皮：Σ w_j · palette[j] · v。
inline QVector3D skinVertex(const Vertex &vertex, const QVector<QMatrix4x4> &palette)
{
    QVector3D accumulated(0.0F, 0.0F, 0.0F);
    for (int influence = 0; influence < 4; ++influence) {
        const float weight = vertex.influence.weights[static_cast<std::size_t>(influence)];
        const int boneIndex = vertex.influence.boneIndices[static_cast<std::size_t>(influence)];
        if (weight > 0.0F && boneIndex >= 0 && boneIndex < palette.size()) {
            accumulated += palette[boneIndex].map(vertex.position) * weight;
        }
    }
    return accumulated;
}

} // namespace handstudio
