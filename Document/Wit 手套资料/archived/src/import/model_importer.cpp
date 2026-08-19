#include "import/model_importer.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace handrig {
namespace {

constexpr int MaximumBoneCount = 128;

QMatrix4x4 toQtMatrix(const aiMatrix4x4 &matrix)
{
    return QMatrix4x4(
        matrix.a1, matrix.a2, matrix.a3, matrix.a4,
        matrix.b1, matrix.b2, matrix.b3, matrix.b4,
        matrix.c1, matrix.c2, matrix.c3, matrix.c4,
        matrix.d1, matrix.d2, matrix.d3, matrix.d4);
}

QString toQString(const aiString &value)
{
    return QString::fromUtf8(value.C_Str());
}

ModelLoadResult failure(ModelLoadErrorCode code, const QString &message, const QString &detail,
                        QStringList warnings = {})
{
    return ModelLoadResult::failure({code, message, detail}, std::move(warnings));
}

void collectBoneNames(const aiScene *scene, QSet<QString> &boneNames,
                      QHash<QString, QMatrix4x4> &inverseBindMatrices)
{
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh *mesh = scene->mMeshes[meshIndex];
        for (unsigned int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            const aiBone *bone = mesh->mBones[boneIndex];
            const QString name = toQString(bone->mName);
            boneNames.insert(name);
            if (!inverseBindMatrices.contains(name)) {
                inverseBindMatrices.insert(name, toQtMatrix(bone->mOffsetMatrix));
            }
        }
    }
}

void collectMeshTransforms(const aiNode *node, const QMatrix4x4 &parentGlobal,
                           QVector<QMatrix4x4> &meshTransforms)
{
    const QMatrix4x4 nodeGlobal = parentGlobal * toQtMatrix(node->mTransformation);
    for (unsigned int index = 0; index < node->mNumMeshes; ++index) {
        const unsigned int meshIndex = node->mMeshes[index];
        if (meshIndex < static_cast<unsigned int>(meshTransforms.size())) {
            meshTransforms[static_cast<qsizetype>(meshIndex)] = nodeGlobal;
        }
    }
    for (unsigned int index = 0; index < node->mNumChildren; ++index) {
        collectMeshTransforms(node->mChildren[index], nodeGlobal, meshTransforms);
    }
}

bool buildBones(const aiNode *node, const QSet<QString> &boneNames,
                const QHash<QString, QMatrix4x4> &inverseBindMatrices,
                const QMatrix4x4 &parentGlobal, int nearestBoneIndex,
                QVector<BoneData> &bones, QHash<QString, int> &boneIndices)
{
    const QMatrix4x4 nodeGlobal = parentGlobal * toQtMatrix(node->mTransformation);
    const QString nodeName = toQString(node->mName);
    int currentNearest = nearestBoneIndex;

    if (boneNames.contains(nodeName)) {
        if (boneIndices.contains(nodeName)) {
            return false;
        }

        QMatrix4x4 bindLocal = nodeGlobal;
        if (nearestBoneIndex >= 0) {
            QMatrix4x4 selectedParentGlobal;
            const BoneData &parentBone = bones.at(nearestBoneIndex);
            selectedParentGlobal = parentBone.bindLocal;
            int ancestor = parentBone.parentIndex;
            while (ancestor >= 0) {
                selectedParentGlobal = bones.at(ancestor).bindLocal * selectedParentGlobal;
                ancestor = bones.at(ancestor).parentIndex;
            }
            bool invertible = false;
            const QMatrix4x4 parentInverse = selectedParentGlobal.inverted(&invertible);
            if (!invertible) {
                return false;
            }
            bindLocal = parentInverse * nodeGlobal;
        }

        currentNearest = bones.size();
        boneIndices.insert(nodeName, currentNearest);
        bones.append({nodeName, nearestBoneIndex, bindLocal, inverseBindMatrices.value(nodeName)});
    }

    for (unsigned int childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        if (!buildBones(node->mChildren[childIndex], boneNames, inverseBindMatrices,
                        nodeGlobal, currentNearest, bones, boneIndices)) {
            return false;
        }
    }
    return true;
}

MaterialData importMaterial(const aiMaterial *material, const QDir &modelDirectory,
                            QStringList &warnings)
{
    MaterialData output;
    aiString name;
    if (material->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
        output.name = toQString(name);
    }

    aiColor4D diffuse;
    if (aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &diffuse) == AI_SUCCESS) {
        output.baseColor = QColor::fromRgbF(
            std::clamp(diffuse.r, 0.0F, 1.0F), std::clamp(diffuse.g, 0.0F, 1.0F),
            std::clamp(diffuse.b, 0.0F, 1.0F), std::clamp(diffuse.a, 0.0F, 1.0F));
    }

    aiString texture;
    if (material->GetTexture(aiTextureType_DIFFUSE, 0, &texture) == AI_SUCCESS) {
        const QString referencedPath = QDir::fromNativeSeparators(toQString(texture));
        const QString absolutePath = QFileInfo(referencedPath).isAbsolute()
            ? referencedPath
            : modelDirectory.absoluteFilePath(referencedPath);
        output.diffuseTexturePath = QDir::cleanPath(absolutePath);
        output.diffuseTextureMissing = !QFileInfo::exists(output.diffuseTexturePath);
        if (output.diffuseTextureMissing) {
            warnings.append(QStringLiteral("材质“%1”的漫反射纹理不存在，已使用默认纯色：%2")
                                .arg(output.name, output.diffuseTexturePath));
            output.diffuseTexturePath.clear();
        }
    }
    return output;
}

bool normalizeInfluences(VertexInfluence &influence, int rootBoneIndex)
{
    float sum = 0.0F;
    for (float weight : influence.weights) {
        sum += weight;
    }
    if (sum <= 0.0F) {
        influence.boneIndices[0] = rootBoneIndex;
        influence.weights[0] = 1.0F;
        return true;
    }
    if (!std::isfinite(sum)) {
        return false;
    }
    for (float &weight : influence.weights) {
        weight /= sum;
    }
    return true;
}

}

ModelLoadResult ModelImporter::load(const QString &filePath) const
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return failure(ModelLoadErrorCode::FileNotFound, QStringLiteral("模型文件不存在"),
                       fileInfo.absoluteFilePath());
    }

    Assimp::Importer importer;
    const unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
        | aiProcess_GenSmoothNormals | aiProcess_ImproveCacheLocality
        | aiProcess_LimitBoneWeights | aiProcess_ValidateDataStructure;
    const aiScene *scene = importer.ReadFile(fileInfo.absoluteFilePath().toUtf8().constData(), flags);
    if (scene == nullptr) {
        return failure(ModelLoadErrorCode::ParseFailed, QStringLiteral("模型解析失败"),
                       QString::fromUtf8(importer.GetErrorString()));
    }
    if (scene->mNumMeshes == 0 || scene->mMeshes == nullptr) {
        return failure(ModelLoadErrorCode::NoMeshes, QStringLiteral("模型不包含网格"),
                       fileInfo.absoluteFilePath());
    }

    QSet<QString> boneNames;
    QHash<QString, QMatrix4x4> inverseBindMatrices;
    collectBoneNames(scene, boneNames, inverseBindMatrices);
    if (boneNames.isEmpty()) {
        return failure(ModelLoadErrorCode::NoBones, QStringLiteral("模型不包含蒙皮骨骼"),
                       fileInfo.absoluteFilePath());
    }
    if (boneNames.size() > MaximumBoneCount) {
        return failure(ModelLoadErrorCode::TooManyBones, QStringLiteral("蒙皮骨骼数量超过 128"),
                       QString::number(boneNames.size()));
    }

    RiggedModel model;
    QHash<QString, int> boneIndices;
    QMatrix4x4 identity;
    identity.setToIdentity();
    if (!buildBones(scene->mRootNode, boneNames, inverseBindMatrices, identity, -1,
                    model.bones, boneIndices)
        || boneIndices.size() != boneNames.size()) {
        return failure(ModelLoadErrorCode::InvalidParentChain, QStringLiteral("骨骼父链无效"),
                       QStringLiteral("Assimp 节点层级无法覆盖全部蒙皮骨骼"));
    }

    bool rootInvertible = false;
    model.globalInverse = toQtMatrix(scene->mRootNode->mTransformation).inverted(&rootInvertible);
    if (!rootInvertible) {
        return failure(ModelLoadErrorCode::InvalidParentChain, QStringLiteral("骨骼父链无效"),
                       QStringLiteral("场景根变换不可逆"));
    }

    QStringList warnings;
    QVector<QMatrix4x4> meshTransforms(static_cast<qsizetype>(scene->mNumMeshes));
    for (QMatrix4x4 &transform : meshTransforms) {
        transform.setToIdentity();
    }
    collectMeshTransforms(scene->mRootNode, identity, meshTransforms);
    const QDir modelDirectory = fileInfo.absoluteDir();
    model.materials.reserve(static_cast<qsizetype>(scene->mNumMaterials));
    for (unsigned int index = 0; index < scene->mNumMaterials; ++index) {
        model.materials.append(importMaterial(scene->mMaterials[index], modelDirectory, warnings));
    }

    model.meshes.reserve(static_cast<qsizetype>(scene->mNumMeshes));
    for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        const aiMesh *source = scene->mMeshes[meshIndex];
        MeshData mesh;
        mesh.bindTransform = model.globalInverse * meshTransforms[static_cast<qsizetype>(meshIndex)];
        mesh.boneOffsets.resize(model.bones.size());
        for (QMatrix4x4 &offset : mesh.boneOffsets) {
            offset.setToIdentity();
        }
        mesh.materialIndex = source->mMaterialIndex < scene->mNumMaterials
            ? static_cast<int>(source->mMaterialIndex) : -1;
        mesh.vertices.resize(static_cast<qsizetype>(source->mNumVertices));
        for (unsigned int vertexIndex = 0; vertexIndex < source->mNumVertices; ++vertexIndex) {
            Vertex &vertex = mesh.vertices[static_cast<qsizetype>(vertexIndex)];
            const aiVector3D &position = source->mVertices[vertexIndex];
            vertex.position = QVector3D(position.x, position.y, position.z);
            if (source->HasNormals()) {
                const aiVector3D &normal = source->mNormals[vertexIndex];
                vertex.normal = QVector3D(normal.x, normal.y, normal.z);
            }
            if (source->HasTextureCoords(0)) {
                const aiVector3D &texCoord = source->mTextureCoords[0][vertexIndex];
                vertex.texCoord = QVector2D(texCoord.x, texCoord.y);
            }
        }

        QVector<QVector<QPair<int, float>>> influences(mesh.vertices.size());
        for (unsigned int sourceBoneIndex = 0; sourceBoneIndex < source->mNumBones; ++sourceBoneIndex) {
            const aiBone *sourceBone = source->mBones[sourceBoneIndex];
            const int boneIndex = boneIndices.value(toQString(sourceBone->mName), -1);
            if (boneIndex < 0) {
                return failure(ModelLoadErrorCode::InvalidParentChain, QStringLiteral("骨骼父链无效"),
                               toQString(sourceBone->mName), warnings);
            }
            mesh.boneOffsets[boneIndex] = toQtMatrix(sourceBone->mOffsetMatrix);
            for (unsigned int weightIndex = 0; weightIndex < sourceBone->mNumWeights; ++weightIndex) {
                const aiVertexWeight &weight = sourceBone->mWeights[weightIndex];
                if (weight.mVertexId >= source->mNumVertices) {
                    return failure(ModelLoadErrorCode::IndexOutOfRange, QStringLiteral("顶点权重索引越界"),
                                   QString::number(weight.mVertexId), warnings);
                }
                if (weight.mWeight > 0.0F) {
                    influences[static_cast<qsizetype>(weight.mVertexId)].append({boneIndex, weight.mWeight});
                }
            }
        }

        for (qsizetype vertexIndex = 0; vertexIndex < mesh.vertices.size(); ++vertexIndex) {
            auto &vertexInfluences = influences[vertexIndex];
            std::sort(vertexInfluences.begin(), vertexInfluences.end(),
                      [](const auto &left, const auto &right) { return left.second > right.second; });
            VertexInfluence &output = mesh.vertices[vertexIndex].influence;
            const qsizetype count = std::min<qsizetype>(4, vertexInfluences.size());
            for (qsizetype influenceIndex = 0; influenceIndex < count; ++influenceIndex) {
                output.boneIndices[static_cast<std::size_t>(influenceIndex)] = vertexInfluences[influenceIndex].first;
                output.weights[static_cast<std::size_t>(influenceIndex)] = vertexInfluences[influenceIndex].second;
            }
            if (!vertexInfluences.isEmpty() && !normalizeInfluences(output, 0)) {
                return failure(ModelLoadErrorCode::IndexOutOfRange, QStringLiteral("顶点权重无效"),
                               QString::number(vertexIndex), warnings);
            }
        }

        for (unsigned int faceIndex = 0; faceIndex < source->mNumFaces; ++faceIndex) {
            const aiFace &face = source->mFaces[faceIndex];
            for (unsigned int index = 0; index < face.mNumIndices; ++index) {
                if (face.mIndices[index] >= source->mNumVertices) {
                    return failure(ModelLoadErrorCode::IndexOutOfRange, QStringLiteral("网格索引越界"),
                                   QString::number(face.mIndices[index]), warnings);
                }
                mesh.indices.append(static_cast<quint32>(face.mIndices[index]));
            }
        }
        model.meshes.append(std::move(mesh));
    }

    return ModelLoadResult::success(std::move(model), std::move(warnings));
}

}
