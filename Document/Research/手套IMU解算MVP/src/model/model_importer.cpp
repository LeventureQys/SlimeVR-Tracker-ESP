#include "model/model_importer.h"

#include "model/standard_joints.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace handstudio {
namespace {

constexpr quint32 GlbMagic = 0x46546C67u;      // "glTF"
constexpr quint32 JsonChunkType = 0x4E4F534Au; // "JSON"
constexpr quint32 BinChunkType = 0x004E4942u;  // "BIN\0"

constexpr int ComponentByte = 5120;
constexpr int ComponentUnsignedByte = 5121;
constexpr int ComponentShort = 5122;
constexpr int ComponentUnsignedShort = 5123;
constexpr int ComponentUnsignedInt = 5125;
constexpr int ComponentFloat = 5126;

ModelLoadResult failure(ModelLoadErrorCode code, const QString &message, const QString &detail,
                        QStringList warnings = {})
{
    return ModelLoadResult::failure({code, message, detail}, std::move(warnings));
}

float readFloat(const char *p)
{
    float value = 0.0F;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

quint32 readU32(const char *p)
{
    quint32 value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

quint16 readU16(const char *p)
{
    quint16 value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

qint16 readI16(const char *p)
{
    qint16 value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

quint8 readU8(const char *p)
{
    return static_cast<quint8>(*p);
}

qint8 readI8(const char *p)
{
    return static_cast<qint8>(*p);
}

int componentSize(int componentType)
{
    switch (componentType) {
    case ComponentByte:
    case ComponentUnsignedByte:
        return 1;
    case ComponentShort:
    case ComponentUnsignedShort:
        return 2;
    case ComponentUnsignedInt:
    case ComponentFloat:
        return 4;
    default:
        return 0;
    }
}

int typeElements(const QString &type)
{
    if (type == QLatin1String("SCALAR")) {
        return 1;
    }
    if (type == QLatin1String("VEC2")) {
        return 2;
    }
    if (type == QLatin1String("VEC3")) {
        return 3;
    }
    if (type == QLatin1String("VEC4")) {
        return 4;
    }
    if (type == QLatin1String("MAT4")) {
        return 16;
    }
    return 0;
}

// glTF 矩阵按列主序存储，转置为 QMatrix4x4（构造器为行主序）。
QMatrix4x4 columnMajorToQMatrix(const float *f)
{
    return QMatrix4x4(
        f[0], f[4], f[8], f[12],
        f[1], f[5], f[9], f[13],
        f[2], f[6], f[10], f[14],
        f[3], f[7], f[11], f[15]);
}

bool matrixIsFinite(const QMatrix4x4 &matrix)
{
    const float *data = matrix.constData();
    for (int index = 0; index < 16; ++index) {
        if (!std::isfinite(data[index])) {
            return false;
        }
    }
    return true;
}

bool matrixIsAffine(const QMatrix4x4 &matrix, float tolerance = 1.0e-4F)
{
    return std::abs(matrix(3, 0)) <= tolerance && std::abs(matrix(3, 1)) <= tolerance
           && std::abs(matrix(3, 2)) <= tolerance && std::abs(matrix(3, 3) - 1.0F) <= tolerance;
}

struct AccessorView {
    QByteArray data; // 已按 byteOffset 切片的连续字节（含 stride 空洞按 stride 布局）
    int componentType = 0;
    int count = 0;
    int elementCount = 0; // SCALAR=1 / VEC2=2 / VEC3=3 / VEC4=4 / MAT4=16
    int stride = 0;       // 0 表示紧凑
};

struct GltfNode {
    QString name;
    QVector<int> children;
    QVector3D translation{0.0F, 0.0F, 0.0F};
    QQuaternion rotation; // Hamilton (w,x,y,z)
    QVector3D scale{1.0F, 1.0F, 1.0F};
    bool hasMatrix = false;
    QMatrix4x4 matrix;
    QMatrix4x4 world;
    bool isJoint = false;
};

QMatrix4x4 nodeLocalMatrix(const GltfNode &node)
{
    if (node.hasMatrix) {
        return node.matrix;
    }
    return trsToMatrix(node.translation, node.rotation, node.scale);
}

// 读取 accessor 的连续字节切片（应用 bufferView.byteOffset 与 accessor.byteOffset）。
AccessorView readAccessor(const QJsonArray &accessors, const QJsonArray &bufferViews,
                          int accessorIndex, const QByteArray &bin)
{
    AccessorView view;
    const QJsonObject accessor = accessors.at(accessorIndex).toObject();
    const QJsonObject bufferView = bufferViews.at(accessor.value(QLatin1String("bufferView")).toInt())
                                       .toObject();
    const qsizetype start = bufferView.value(QLatin1String("byteOffset")).toInt()
                            + accessor.value(QLatin1String("byteOffset")).toInt();
    view.componentType = accessor.value(QLatin1String("componentType")).toInt();
    view.count = accessor.value(QLatin1String("count")).toInt();
    const QString type = accessor.value(QLatin1String("type")).toString();
    view.elementCount = typeElements(type);
    view.stride = bufferView.value(QLatin1String("byteStride")).toInt(0);

    const int size = componentSize(view.componentType);
    const int elementBytes = size * view.elementCount;
    const int stride = view.stride > 0 ? view.stride : elementBytes;
    const qsizetype length = elementBytes + stride * (view.count - 1);
    if (start + length <= bin.size()) {
        view.data = bin.mid(start, length);
    }
    return view;
}

const char *elementPointer(const AccessorView &view, int index)
{
    const int size = componentSize(view.componentType);
    const int elementBytes = size * view.elementCount;
    const int stride = view.stride > 0 ? view.stride : elementBytes;
    return view.data.constData() + static_cast<qsizetype>(index) * stride;
}

bool decodeVec3(const AccessorView &view, QVector<QVector3D> &out)
{
    out.resize(view.count);
    for (int index = 0; index < view.count; ++index) {
        const char *p = elementPointer(view, index);
        out[index] = QVector3D(readFloat(p), readFloat(p + 4), readFloat(p + 8));
    }
    return true;
}

bool decodeVec2(const AccessorView &view, QVector<QVector2D> &out)
{
    out.resize(view.count);
    for (int index = 0; index < view.count; ++index) {
        const char *p = elementPointer(view, index);
        out[index] = QVector2D(readFloat(p), readFloat(p + 4));
    }
    return true;
}

bool decodeJoints(const AccessorView &view, QVector<std::array<int, 4>> &out)
{
    out.resize(view.count);
    for (int index = 0; index < view.count; ++index) {
        const char *p = elementPointer(view, index);
        if (view.componentType == ComponentUnsignedByte) {
            out[index] = {readU8(p), readU8(p + 1), readU8(p + 2), readU8(p + 3)};
        } else if (view.componentType == ComponentUnsignedShort) {
            out[index] = {readU16(p), readU16(p + 2), readU16(p + 4), readU16(p + 6)};
        } else {
            out[index] = {0, 0, 0, 0};
            return false;
        }
    }
    return true;
}

bool decodeWeights(const AccessorView &view, QVector<std::array<float, 4>> &out)
{
    out.resize(view.count);
    for (int index = 0; index < view.count; ++index) {
        const char *p = elementPointer(view, index);
        out[index] = {readFloat(p), readFloat(p + 4), readFloat(p + 8), readFloat(p + 12)};
    }
    return true;
}

bool decodeIndices(const AccessorView &view, QVector<quint32> &out)
{
    out.resize(view.count);
    for (int index = 0; index < view.count; ++index) {
        const char *p = elementPointer(view, index);
        if (view.componentType == ComponentUnsignedShort) {
            out[index] = readU16(p);
        } else if (view.componentType == ComponentUnsignedInt) {
            out[index] = readU32(p);
        } else {
            out[index] = 0;
            return false;
        }
    }
    return true;
}

bool decodeMat4(const AccessorView &view, QVector<QMatrix4x4> &out)
{
    out.resize(view.count);
    for (int index = 0; index < view.count; ++index) {
        const char *p = elementPointer(view, index);
        float f[16];
        for (int element = 0; element < 16; ++element) {
            f[element] = readFloat(p + element * 4);
        }
        out[index] = columnMajorToQMatrix(f);
    }
    return true;
}

QQuaternion gltfRotationToQuaternion(const QJsonArray &rotation)
{
    // glTF 旋转为 [x,y,z,w]，转换为 Hamilton (w,x,y,z)。
    const float x = static_cast<float>(rotation.at(0).toDouble());
    const float y = static_cast<float>(rotation.at(1).toDouble());
    const float z = static_cast<float>(rotation.at(2).toDouble());
    const float w = static_cast<float>(rotation.at(3).toDouble());
    return QQuaternion(w, x, y, z).normalized();
}

QVector3D jsonVec3(const QJsonArray &array)
{
    return QVector3D(static_cast<float>(array.at(0).toDouble()),
                     static_cast<float>(array.at(1).toDouble()),
                     static_cast<float>(array.at(2).toDouble()));
}

// 递归计算场景空间世界矩阵。
void computeWorldMatrices(QVector<GltfNode> &nodes, const QVector<int> &sceneRoots)
{
    const QMatrix4x4 identity;
    std::function<void(int, const QMatrix4x4 &)> walk = [&](int index,
                                                            const QMatrix4x4 &parentWorld) {
        if (index < 0 || index >= nodes.size()) {
            return;
        }
        GltfNode &node = nodes[index];
        node.world = parentWorld * nodeLocalMatrix(node);
        for (int child : node.children) {
            walk(child, node.world);
        }
    };
    for (int root : sceneRoots) {
        walk(root, identity);
    }
}

} // namespace

ModelLoadResult ModelImporter::load(const QString &filePath) const
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        return failure(ModelLoadErrorCode::FileNotFound, QStringLiteral("模型文件不存在"),
                       fileInfo.absoluteFilePath());
    }

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return failure(ModelLoadErrorCode::FileNotFound, QStringLiteral("模型文件无法读取"),
                       fileInfo.absoluteFilePath());
    }
    const QByteArray raw = file.readAll();
    file.close();

    // ---- GLB 分块 ----
    if (raw.size() < 20) {
        return failure(ModelLoadErrorCode::ParseFailed, QStringLiteral("GLB 文件过短"),
                       QString::number(raw.size()));
    }
    const quint32 magic = readU32(raw.constData());
    const quint32 version = readU32(raw.constData() + 4);
    if (magic != GlbMagic) {
        return failure(ModelLoadErrorCode::ParseFailed, QStringLiteral("非 GLB 文件（magic 不符）"),
                       QStringLiteral("magic=0x%1").arg(magic, 8, 16, QLatin1Char('0')));
    }
    if (version != 2) {
        return failure(ModelLoadErrorCode::UnsupportedVersion, QStringLiteral("不支持的 glTF 版本"),
                       QString::number(version));
    }

    QByteArray jsonBytes;
    QByteArray bin;
    qsizetype offset = 12;
    while (offset + 8 <= raw.size()) {
        const quint32 chunkLength = readU32(raw.constData() + offset);
        const quint32 chunkType = readU32(raw.constData() + offset + 4);
        const QByteArray chunk = raw.mid(offset + 8, static_cast<qsizetype>(chunkLength));
        if (chunkType == JsonChunkType) {
            jsonBytes = chunk;
        } else if (chunkType == BinChunkType) {
            bin = chunk;
        }
        offset += 8 + static_cast<qsizetype>(chunkLength);
    }
    if (jsonBytes.isEmpty()) {
        return failure(ModelLoadErrorCode::ParseFailed, QStringLiteral("GLB 缺少 JSON chunk"),
                       fileInfo.absoluteFilePath());
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (document.isNull() || !document.isObject()) {
        return failure(ModelLoadErrorCode::ParseFailed, QStringLiteral("GLB JSON 解析失败"),
                       parseError.errorString());
    }
    const QJsonObject root = document.object();
    const QJsonArray accessors = root.value(QLatin1String("accessors")).toArray();
    const QJsonArray bufferViews = root.value(QLatin1String("bufferViews")).toArray();
    const QJsonArray nodesArray = root.value(QLatin1String("nodes")).toArray();

    // ---- 节点 ----
    QVector<GltfNode> nodes(nodesArray.size());
    for (int index = 0; index < nodesArray.size(); ++index) {
        const QJsonObject nodeObject = nodesArray.at(index).toObject();
        GltfNode &node = nodes[index];
        node.name = nodeObject.value(QLatin1String("name")).toString();
        const QJsonArray children = nodeObject.value(QLatin1String("children")).toArray();
        for (const QJsonValue &child : children) {
            node.children.append(child.toInt());
        }
        if (nodeObject.contains(QLatin1String("matrix"))) {
            const QJsonArray matrix = nodeObject.value(QLatin1String("matrix")).toArray();
            if (matrix.size() != 16) {
                return failure(ModelLoadErrorCode::ParseFailed, QStringLiteral("节点矩阵元素数错误"),
                               node.name);
            }
            float f[16];
            for (int element = 0; element < 16; ++element) {
                f[element] = static_cast<float>(matrix.at(element).toDouble());
            }
            node.hasMatrix = true;
            node.matrix = columnMajorToQMatrix(f);
        } else {
            if (nodeObject.contains(QLatin1String("translation"))) {
                node.translation = jsonVec3(nodeObject.value(QLatin1String("translation")).toArray());
            }
            if (nodeObject.contains(QLatin1String("rotation"))) {
                node.rotation = gltfRotationToQuaternion(
                    nodeObject.value(QLatin1String("rotation")).toArray());
            }
            if (nodeObject.contains(QLatin1String("scale"))) {
                node.scale = jsonVec3(nodeObject.value(QLatin1String("scale")).toArray());
            }
        }
    }

    const QJsonArray scenes = root.value(QLatin1String("scenes")).toArray();
    QVector<int> sceneRoots;
    if (!scenes.isEmpty()) {
        const QJsonArray rootsArray = scenes.at(0).toObject().value(QLatin1String("nodes")).toArray();
        for (const QJsonValue &rootIndex : rootsArray) {
            sceneRoots.append(rootIndex.toInt());
        }
    }
    if (sceneRoots.isEmpty() && !nodes.isEmpty()) {
        sceneRoots.append(0);
    }
    computeWorldMatrices(nodes, sceneRoots);

    // ---- 网格/primitive ----
    const QJsonArray meshes = root.value(QLatin1String("meshes")).toArray();
    if (meshes.size() != 1) {
        return failure(ModelLoadErrorCode::NoMeshes, QStringLiteral("仅支持单网格资产"),
                       QString::number(meshes.size()));
    }
    const QJsonArray primitives = meshes.at(0).toObject().value(QLatin1String("primitives")).toArray();
    if (primitives.size() != 1) {
        return failure(ModelLoadErrorCode::TooManyPrimitives, QStringLiteral("仅支持单 primitive 网格"),
                       QString::number(primitives.size()));
    }
    const QJsonObject primitive = primitives.at(0).toObject();
    const int mode = primitive.value(QLatin1String("mode")).toInt(4);
    if (mode != 4) {
        return failure(ModelLoadErrorCode::ParseFailed, QStringLiteral("仅支持 TRIANGLES 模式"),
                       QString::number(mode));
    }
    const QJsonObject attributes = primitive.value(QLatin1String("attributes")).toObject();
    for (const char *required : {"POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", "WEIGHTS_0"}) {
        if (!attributes.contains(QLatin1String(required))) {
            return failure(ModelLoadErrorCode::MissingAttribute,
                           QStringLiteral("网格缺少属性 %1").arg(QLatin1String(required)),
                           fileInfo.absoluteFilePath());
        }
    }
    if (!primitive.contains(QLatin1String("indices"))) {
        return failure(ModelLoadErrorCode::MissingAttribute, QStringLiteral("网格缺少索引"),
                       fileInfo.absoluteFilePath());
    }

    // ---- skin ----
    const QJsonArray skins = root.value(QLatin1String("skins")).toArray();
    if (skins.isEmpty()) {
        return failure(ModelLoadErrorCode::MissingSkin, QStringLiteral("资产缺少 skin（无法蒙皮）"),
                       fileInfo.absoluteFilePath());
    }
    const QJsonObject skin = skins.at(0).toObject();
    const QJsonArray joints = skin.value(QLatin1String("joints")).toArray();
    if (joints.size() != HandSkinJointCount) {
        return failure(ModelLoadErrorCode::JointCountMismatch,
                       QStringLiteral("skin.joints 应为 25 个关节"), QString::number(joints.size()));
    }
    if (!skin.contains(QLatin1String("inverseBindMatrices"))) {
        return failure(ModelLoadErrorCode::InvalidInverseBind, QStringLiteral("skin 缺少 inverseBindMatrices"),
                       fileInfo.absoluteFilePath());
    }

    // ---- 解码网格属性 ----
    const AccessorView positionView = readAccessor(
        accessors, bufferViews, attributes.value(QLatin1String("POSITION")).toInt(), bin);
    const AccessorView normalView = readAccessor(
        accessors, bufferViews, attributes.value(QLatin1String("NORMAL")).toInt(), bin);
    const AccessorView texCoordView = readAccessor(
        accessors, bufferViews, attributes.value(QLatin1String("TEXCOORD_0")).toInt(), bin);
    const AccessorView jointsView = readAccessor(
        accessors, bufferViews, attributes.value(QLatin1String("JOINTS_0")).toInt(), bin);
    const AccessorView weightsView = readAccessor(
        accessors, bufferViews, attributes.value(QLatin1String("WEIGHTS_0")).toInt(), bin);
    const AccessorView indicesView = readAccessor(
        accessors, bufferViews, primitive.value(QLatin1String("indices")).toInt(), bin);
    const AccessorView inverseBindView = readAccessor(
        accessors, bufferViews, skin.value(QLatin1String("inverseBindMatrices")).toInt(), bin);

    if (positionView.data.isEmpty() || positionView.componentType != ComponentFloat
        || positionView.elementCount != 3) {
        return failure(ModelLoadErrorCode::MissingAttribute, QStringLiteral("POSITION 属性无效"),
                       fileInfo.absoluteFilePath());
    }
    if (normalView.data.isEmpty() || texCoordView.data.isEmpty() || jointsView.data.isEmpty()
        || weightsView.data.isEmpty() || indicesView.data.isEmpty()
        || inverseBindView.data.isEmpty()) {
        return failure(ModelLoadErrorCode::MissingAttribute, QStringLiteral("网格访问器越界或为空"),
                       fileInfo.absoluteFilePath());
    }

    QVector<QVector3D> positions;
    QVector<QVector3D> normals;
    QVector<QVector2D> texCoords;
    QVector<std::array<int, 4>> jointsData;
    QVector<std::array<float, 4>> weightsData;
    QVector<quint32> indices;
    QVector<QMatrix4x4> inverseBindMatrices;

    decodeVec3(positionView, positions);
    decodeVec3(normalView, normals);
    decodeVec2(texCoordView, texCoords);
    if (!decodeJoints(jointsView, jointsData)) {
        return failure(ModelLoadErrorCode::InvalidJointIndex, QStringLiteral("JOINTS_0 分量类型不支持"),
                       QString::number(jointsView.componentType));
    }
    decodeWeights(weightsView, weightsData);
    if (!decodeIndices(indicesView, indices)) {
        return failure(ModelLoadErrorCode::InvalidIndex, QStringLiteral("索引分量类型不支持"),
                       QString::number(indicesView.componentType));
    }
    decodeMat4(inverseBindView, inverseBindMatrices);

    const qsizetype vertexCount = positions.size();
    if (normals.size() != vertexCount || texCoords.size() != vertexCount
        || jointsData.size() != vertexCount || weightsData.size() != vertexCount) {
        return failure(ModelLoadErrorCode::MissingAttribute, QStringLiteral("顶点属性数量不一致"),
                       QStringLiteral("%1/%2/%3/%4/%5")
                           .arg(positions.size()).arg(normals.size()).arg(texCoords.size())
                           .arg(jointsData.size()).arg(weightsData.size()));
    }
    if (inverseBindMatrices.size() != HandSkinJointCount) {
        return failure(ModelLoadErrorCode::InvalidInverseBind,
                       QStringLiteral("inverseBindMatrices 数量应为 25"),
                       QString::number(inverseBindMatrices.size()));
    }

    // ---- 组装骨骼（palette 顺序 = skin.joints 顺序） ----
    RiggedModel model;
    model.sourcePath = fileInfo.absoluteFilePath();
    QHash<QString, int> seenJointNames;
    for (int paletteIndex = 0; paletteIndex < joints.size(); ++paletteIndex) {
        const int nodeIndex = joints.at(paletteIndex).toInt();
        if (nodeIndex < 0 || nodeIndex >= nodes.size()) {
            return failure(ModelLoadErrorCode::InvalidJointIndex, QStringLiteral("skin.joints 节点越界"),
                           QString::number(nodeIndex));
        }
        const GltfNode &node = nodes[nodeIndex];
        BoneData bone;
        bone.name = node.name;
        bone.bindTranslation = node.translation;
        bone.bindRotation = node.rotation;
        bone.bindScale = node.scale;
        bone.bindLocal = nodeLocalMatrix(node);
        bone.bindWorld = node.world;
        bone.inverseBind = inverseBindMatrices.at(paletteIndex);

        if (bone.name.isEmpty() || seenJointNames.contains(bone.name)) {
            return failure(ModelLoadErrorCode::DuplicateJoint, QStringLiteral("关节名重复或为空"),
                           bone.name);
        }
        seenJointNames.insert(bone.name, paletteIndex);
        if (!matrixIsFinite(bone.bindWorld) || !matrixIsFinite(bone.inverseBind)) {
            return failure(ModelLoadErrorCode::NonFiniteMatrix, QStringLiteral("关节矩阵含非有限值"),
                           bone.name);
        }
        if (!matrixIsAffine(bone.inverseBind)) {
            return failure(ModelLoadErrorCode::InvalidInverseBind,
                           QStringLiteral("inverseBind 矩阵非仿射"), bone.name);
        }
        model.bones.append(std::move(bone));
    }
    model.boneIndexByName = seenJointNames;

    // 25 个标准关节必须齐全（唯一性已由上面检查）。
    QStringList warnings;
    for (const QString &name : standardJointNames()) {
        if (!model.boneIndexByName.contains(name)) {
            return failure(ModelLoadErrorCode::MissingJoint, QStringLiteral("真实手模型缺少标准关节"),
                           name);
        }
    }

    // ---- 网格 ----
    MeshData mesh;
    mesh.vertices.resize(vertexCount);
    const int skinJointCount = model.bones.size();
    for (qsizetype vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
        Vertex &vertex = mesh.vertices[vertexIndex];
        vertex.position = positions.at(vertexIndex);
        vertex.normal = normals.at(vertexIndex);
        vertex.texCoord = texCoords.at(vertexIndex);

        float weightSum = 0.0F;
        for (int influence = 0; influence < 4; ++influence) {
            const int jointIndex = jointsData.at(vertexIndex)[static_cast<std::size_t>(influence)];
            const float weight = weightsData.at(vertexIndex)[static_cast<std::size_t>(influence)];
            if (jointIndex < 0 || jointIndex >= skinJointCount) {
                return failure(ModelLoadErrorCode::InvalidJointIndex,
                               QStringLiteral("顶点关节索引越界"),
                               QStringLiteral("vertex=%1 joint=%2").arg(vertexIndex).arg(jointIndex));
            }
            if (!std::isfinite(weight) || weight < 0.0F) {
                return failure(ModelLoadErrorCode::InvalidWeight, QStringLiteral("顶点权重非法"),
                               QStringLiteral("vertex=%1").arg(vertexIndex));
            }
            vertex.influence.boneIndices[static_cast<std::size_t>(influence)] = jointIndex;
            vertex.influence.weights[static_cast<std::size_t>(influence)] = weight;
            weightSum += weight;
        }
        if (weightSum <= 0.0F) {
            return failure(ModelLoadErrorCode::InvalidWeight, QStringLiteral("顶点权重和为 0"),
                           QStringLiteral("vertex=%1").arg(vertexIndex));
        }
        if (std::abs(weightSum - 1.0F) > 1.0e-3F) {
            warnings.append(QStringLiteral("顶点 %1 权重和偏离 1（%2），已安全归一化")
                                .arg(vertexIndex).arg(weightSum));
        }
        for (int influence = 0; influence < 4; ++influence) {
            vertex.influence.weights[static_cast<std::size_t>(influence)] /= weightSum;
        }
    }

    mesh.indices.reserve(indices.size());
    for (quint32 index : indices) {
        if (index >= static_cast<quint32>(vertexCount)) {
            return failure(ModelLoadErrorCode::InvalidIndex, QStringLiteral("网格索引越界"),
                           QString::number(index));
        }
        mesh.indices.append(index);
    }

    // ---- 材质 ----
    const QJsonArray materials = root.value(QLatin1String("materials")).toArray();
    if (!materials.isEmpty()) {
        const QJsonObject material = materials.at(0).toObject();
        MaterialData materialData;
        materialData.name = material.value(QLatin1String("name")).toString();
        const QJsonArray baseColor = material.value(QLatin1String("pbrMetallicRoughness"))
                                         .toObject()
                                         .value(QLatin1String("baseColorFactor"))
                                         .toArray();
        if (baseColor.size() == 4) {
            materialData.baseColor = QColor::fromRgbF(
                std::clamp(static_cast<float>(baseColor.at(0).toDouble()), 0.0F, 1.0F),
                std::clamp(static_cast<float>(baseColor.at(1).toDouble()), 0.0F, 1.0F),
                std::clamp(static_cast<float>(baseColor.at(2).toDouble()), 0.0F, 1.0F),
                std::clamp(static_cast<float>(baseColor.at(3).toDouble()), 0.0F, 1.0F));
        }
        mesh.materialIndex = 0;
        model.materials.append(std::move(materialData));
    } else {
        mesh.materialIndex = -1;
    }

    model.meshes.append(std::move(mesh));
    return ModelLoadResult::success(std::move(model), std::move(warnings));
}

} // namespace handstudio
