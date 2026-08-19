#pragma once

#include <QColor>
#include <QMatrix4x4>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector2D>
#include <QVector3D>
#include <QtTypes>

#include <array>
#include <optional>
#include <utility>

namespace handrig {

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
    QMatrix4x4 bindTransform;
    QVector<QMatrix4x4> boneOffsets;
};

struct BoneData {
    QString name;
    int parentIndex{-1};
    QMatrix4x4 bindLocal;
    QMatrix4x4 inverseBind;
};

struct MaterialData {
    QString name;
    QColor baseColor{190, 190, 190};
    QString diffuseTexturePath;
    bool diffuseTextureMissing{false};
};

struct RiggedModel {
    QVector<MeshData> meshes;
    QVector<BoneData> bones;
    QMatrix4x4 globalInverse;
    QVector<MaterialData> materials;
};

inline QMatrix4x4 meshSkinTransform(const RiggedModel &model, const MeshData &mesh,
                                    const QMatrix4x4 &animatedBoneGlobal, int boneIndex)
{
    return model.globalInverse * animatedBoneGlobal * mesh.boneOffsets[boneIndex]
           * mesh.bindTransform;
}

enum class ModelLoadErrorCode {
    FileNotFound,
    ParseFailed,
    NoMeshes,
    NoBones,
    InvalidParentChain,
    TooManyBones,
    IndexOutOfRange
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

    [[nodiscard]] bool hasValue() const noexcept
    {
        return value.has_value();
    }

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

}
