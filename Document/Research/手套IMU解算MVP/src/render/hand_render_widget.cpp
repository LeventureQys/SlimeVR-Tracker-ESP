#include "render/hand_render_widget.h"

#include "model/standard_joints.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace handstudio {
namespace {

// 骨骼上限复用 handstudio::MaximumBoneCount（model_data.h），避免与匿名命名空间重复定义。
constexpr float Pi = 3.14159265358979323846F;

struct GpuVertex {
    float position[3];
    float normal[3];
    float texCoord[2];
    int boneIndices[4];
    float weights[4];
};

static_assert(offsetof(GpuVertex, position) == 0, "position offset");
static_assert(offsetof(GpuVertex, normal) == 12, "normal offset");
static_assert(offsetof(GpuVertex, texCoord) == 24, "texCoord offset");
static_assert(offsetof(GpuVertex, boneIndices) == 32, "boneIndices offset");
static_assert(offsetof(GpuVertex, weights) == 48, "weights offset");

float clampPitch(float value)
{
    return std::clamp(value, -89.0F, 89.0F);
}

bool intersectSphere(const QVector3D &rayOrigin, const QVector3D &rayDirection,
                     const QVector3D &center, float radius, float &distance)
{
    const QVector3D offset = rayOrigin - center;
    const float projected = QVector3D::dotProduct(offset, rayDirection);
    const float constant = QVector3D::dotProduct(offset, offset) - radius * radius;
    const float discriminant = projected * projected - constant;
    if (discriminant < 0.0F) {
        return false;
    }
    const float root = std::sqrt(discriminant);
    distance = -projected - root;
    if (distance < 0.0F) {
        distance = -projected + root;
    }
    return distance >= 0.0F;
}

} // namespace

struct HandRenderWidget::MeshResource {
    unsigned int vao{0};
    unsigned int vbo{0};
    unsigned int ebo{0};
    int indexCount{0};
    QColor color;
    int modelMeshIndex{-1};
};

struct HandRenderWidget::OverlayVertex {
    QVector3D position;
    QVector3D color;
};

HandRenderWidget::HandRenderWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat requestedFormat;
    requestedFormat.setRenderableType(QSurfaceFormat::OpenGL);
    requestedFormat.setVersion(3, 3);
    requestedFormat.setProfile(QSurfaceFormat::CoreProfile);
    requestedFormat.setDepthBufferSize(24);
    requestedFormat.setSamples(4);
    setFormat(requestedFormat);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
}

HandRenderWidget::~HandRenderWidget()
{
    if (context() != nullptr && context()->isValid()) {
        makeCurrent();
        releaseResources();
        doneCurrent();
    }
}

void HandRenderWidget::setModel(std::shared_ptr<const RiggedModel> model)
{
    model_ = std::move(model);
    skeletonFrame_.reset();
    lastError_.reset();
    modelUploadPending_ = model_ != nullptr;
    if (!model_) {
        skinMatrices_.clear();
        jointPositions_.clear();
        update();
        return;
    }
    if (model_->bones.isEmpty() || model_->meshes.isEmpty()) {
        fail(RenderErrorCode::InvalidModel, QStringLiteral("模型数据无效"),
             QStringLiteral("模型必须同时包含网格和骨骼"));
        return;
    }
    if (model_->bones.size() > MaximumBoneCount) {
        fail(RenderErrorCode::TooManyBones, QStringLiteral("蒙皮骨骼数量超过 128"),
             QString::number(model_->bones.size()));
        return;
    }
    rebuildBindPose();
    fitCameraToModel();
    if (initialized_) {
        makeCurrent();
        uploadModel();
        doneCurrent();
    }
    update();
}

void HandRenderWidget::setSkeletonFrame(const HandSkeletonFrame &frame)
{
    if (!model_ || lastError_.has_value()) {
        return;
    }
    if (frame.bones.size() != model_->bones.size()) {
        fail(RenderErrorCode::InvalidSkeletonFrame, QStringLiteral("姿态数据无效"),
             QStringLiteral("骨骼数量 %1 与模型 %2 不一致")
                 .arg(frame.bones.size()).arg(model_->bones.size()));
        return;
    }

    QHash<QString, int> frameIndexByName;
    frameIndexByName.reserve(frame.bones.size());
    for (int index = 0; index < frame.bones.size(); ++index) {
        frameIndexByName.insert(frame.bones[index].boneName, index);
    }

    QVector<QMatrix4x4> skinMatrices(model_->bones.size());
    QVector<QVector3D> jointPositions(model_->bones.size());
    for (int paletteIndex = 0; paletteIndex < model_->bones.size(); ++paletteIndex) {
        const QString &name = model_->bones[paletteIndex].name;
        const auto found = frameIndexByName.constFind(name);
        if (found == frameIndexByName.constEnd() || !frame.bones[*found].valid) {
            fail(RenderErrorCode::InvalidSkeletonFrame, QStringLiteral("姿态关节缺失或无效"), name);
            return;
        }
        const HandBoneFrame &bone = frame.bones[*found];
        skinMatrices[paletteIndex] = bone.skinMatrix;
        jointPositions[paletteIndex] = bone.globalMatrix.map(QVector3D(0.0F, 0.0F, 0.0F));
    }
    skinMatrices_ = std::move(skinMatrices);
    jointPositions_ = std::move(jointPositions);
    skeletonFrame_ = frame;
    update();
}

void HandRenderWidget::setRenderOptions(const RenderOptions &options)
{
    options_ = options;
    options_.skinOpacity = std::clamp(options_.skinOpacity, 0.0F, 1.0F);
    update();
}

void HandRenderWidget::resetCamera()
{
    fitCameraToModel();
    emit cameraChanged(camera_);
    update();
}

void HandRenderWidget::initializeGL()
{
    initialized_ = initializeOpenGLFunctions();
    const QSurfaceFormat actualFormat = context()->format();
    if (!initialized_ || actualFormat.majorVersion() < 3
        || (actualFormat.majorVersion() == 3 && actualFormat.minorVersion() < 3)) {
        fail(RenderErrorCode::OpenGlVersionUnsupported, QStringLiteral("需要 OpenGL 3.3 Core"),
             QStringLiteral("当前上下文版本 %1.%2")
                 .arg(actualFormat.majorVersion()).arg(actualFormat.minorVersion()));
        return;
    }
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_MULTISAMPLE);
    glClearColor(0.075F, 0.085F, 0.105F, 1.0F);
    if (!createPrograms()) {
        return;
    }
    glGenVertexArrays(1, &overlayVao_);
    glGenBuffers(1, &overlayVbo_);
    if (overlayVao_ == 0 || overlayVbo_ == 0) {
        fail(RenderErrorCode::ResourceCreationFailed, QStringLiteral("OpenGL 资源创建失败"),
             QStringLiteral("无法创建骨架叠加缓冲区"));
        return;
    }
    if (modelUploadPending_) {
        uploadModel();
    }
}

void HandRenderWidget::resizeGL(int, int)
{
    updateMatrices();
}

void HandRenderWidget::paintGL()
{
    // 先判空再触碰任何 GL 函数，避免上下文未初始化时崩溃。
    if (!initialized_ || lastError_.has_value()) {
        return;
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!model_ || meshes_.isEmpty()) {
        return;
    }
    updateMatrices();
    drawMeshes();
    drawSkeleton();
    emit frameRendered();
}

void HandRenderWidget::mousePressEvent(QMouseEvent *event)
{
    lastMousePosition_ = event->position().toPoint();
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::ShiftModifier))) {
        dragMode_ = DragMode::Pan;
        return;
    }
    if (event->button() == Qt::LeftButton) {
        dragMode_ = DragMode::Orbit;
    }
}

void HandRenderWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint current = event->position().toPoint();
    const QPoint delta = current - lastMousePosition_;
    lastMousePosition_ = current;
    if (dragMode_ == DragMode::Orbit) {
        camera_.yawDegrees += delta.x() * 0.35F;
        camera_.pitchDegrees = clampPitch(camera_.pitchDegrees + delta.y() * 0.35F);
        emit cameraChanged(camera_);
        update();
    } else if (dragMode_ == DragMode::Pan) {
        const QMatrix4x4 inverseView = viewMatrix_.inverted();
        const QVector3D right = inverseView.mapVector(QVector3D(1.0F, 0.0F, 0.0F)).normalized();
        const QVector3D up = inverseView.mapVector(QVector3D(0.0F, 1.0F, 0.0F)).normalized();
        const float unitsPerPixel = 2.0F * camera_.distance * std::tan(22.5F * Pi / 180.0F)
                                    / std::max(1, height());
        camera_.target += (-right * delta.x() + up * delta.y()) * unitsPerPixel;
        emit cameraChanged(camera_);
        update();
    }
}

void HandRenderWidget::mouseReleaseEvent(QMouseEvent *)
{
    dragMode_ = DragMode::None;
}

void HandRenderWidget::wheelEvent(QWheelEvent *event)
{
    camera_.distance *= std::exp(-static_cast<float>(event->angleDelta().y()) / 1200.0F);
    camera_.distance = std::clamp(camera_.distance, modelScale() * 0.02F, modelScale() * 100.0F);
    emit cameraChanged(camera_);
    update();
    event->accept();
}

void HandRenderWidget::fail(RenderErrorCode code, const QString &message, const QString &detail)
{
    lastError_ = RenderError{code, message, detail};
    emit renderFailed(*lastError_);
    update();
}

bool HandRenderWidget::createPrograms()
{
    static const char *meshVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in ivec4 aBoneIndices;
layout(location = 4) in vec4 aWeights;
uniform mat4 uViewProjection;
uniform mat4 uBones[128];
out vec3 vNormal;
out vec2 vTexCoord;
void main()
{
    mat4 skin = mat4(0.0);
    for (int index = 0; index < 4; ++index) {
        if (aWeights[index] > 0.0 && aBoneIndices[index] >= 0) {
            skin += uBones[aBoneIndices[index]] * aWeights[index];
        }
    }
    vec4 worldPosition = skin * vec4(aPosition, 1.0);
    mat3 normalMatrix = transpose(inverse(mat3(skin)));
    vNormal = normalize(normalMatrix * aNormal);
    vTexCoord = aTexCoord;
    gl_Position = uViewProjection * worldPosition;
}
)";
    static const char *meshFragmentShader = R"(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
uniform vec4 uBaseColor;
out vec4 fragmentColor;
void main()
{
    vec3 normal = normalize(vNormal);
    vec3 lightDirection = normalize(vec3(0.35, 0.7, 0.55));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float lighting = 0.28 + 0.72 * diffuse;
    fragmentColor = vec4(uBaseColor.rgb * lighting, uBaseColor.a);
}
)";
    static const char *overlayVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;
uniform mat4 uViewProjection;
uniform float uPointSize;
out vec3 vColor;
void main()
{
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
    gl_PointSize = uPointSize;
}
)";
    static const char *overlayFragmentShader = R"(
#version 330 core
in vec3 vColor;
out vec4 fragmentColor;
void main()
{
    fragmentColor = vec4(vColor, 1.0);
}
)";

    meshProgram_ = std::make_unique<QOpenGLShaderProgram>();
    if (!meshProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, meshVertexShader)
        || !meshProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, meshFragmentShader)
        || !meshProgram_->link()) {
        fail(RenderErrorCode::ShaderCompilationFailed, QStringLiteral("模型着色器初始化失败"),
             meshProgram_->log());
        return false;
    }
    overlayProgram_ = std::make_unique<QOpenGLShaderProgram>();
    if (!overlayProgram_->addShaderFromSourceCode(QOpenGLShader::Vertex, overlayVertexShader)
        || !overlayProgram_->addShaderFromSourceCode(QOpenGLShader::Fragment, overlayFragmentShader)
        || !overlayProgram_->link()) {
        fail(RenderErrorCode::ShaderCompilationFailed, QStringLiteral("骨架着色器初始化失败"),
             overlayProgram_->log());
        return false;
    }
    return true;
}

bool HandRenderWidget::uploadModel()
{
    for (const MeshResource &mesh : meshes_) {
        glDeleteBuffers(1, &mesh.ebo);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteVertexArrays(1, &mesh.vao);
    }
    meshes_.clear();
    if (!model_) {
        return false;
    }
    meshes_.reserve(model_->meshes.size());
    for (int modelMeshIndex = 0; modelMeshIndex < model_->meshes.size(); ++modelMeshIndex) {
        const MeshData &mesh = model_->meshes[modelMeshIndex];
        if (mesh.vertices.isEmpty() || mesh.indices.isEmpty()) {
            continue;
        }
        QVector<GpuVertex> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const Vertex &source : mesh.vertices) {
            GpuVertex vertex{};
            vertex.position[0] = source.position.x();
            vertex.position[1] = source.position.y();
            vertex.position[2] = source.position.z();
            vertex.normal[0] = source.normal.x();
            vertex.normal[1] = source.normal.y();
            vertex.normal[2] = source.normal.z();
            vertex.texCoord[0] = source.texCoord.x();
            vertex.texCoord[1] = source.texCoord.y();
            std::copy(source.influence.boneIndices.cbegin(), source.influence.boneIndices.cend(),
                      vertex.boneIndices);
            std::copy(source.influence.weights.cbegin(), source.influence.weights.cend(),
                      vertex.weights);
            vertices.append(vertex);
        }
        MeshResource resource;
        resource.modelMeshIndex = modelMeshIndex;
        resource.indexCount = mesh.indices.size();
        resource.color = mesh.materialIndex >= 0 && mesh.materialIndex < model_->materials.size()
            ? model_->materials[mesh.materialIndex].baseColor : QColor(190, 190, 190);
        glGenVertexArrays(1, &resource.vao);
        glGenBuffers(1, &resource.vbo);
        glGenBuffers(1, &resource.ebo);
        if (resource.vao == 0 || resource.vbo == 0 || resource.ebo == 0) {
            fail(RenderErrorCode::ResourceCreationFailed, QStringLiteral("OpenGL 资源创建失败"),
                 QStringLiteral("无法创建网格缓冲区"));
            return false;
        }
        glBindVertexArray(resource.vao);
        glBindBuffer(GL_ARRAY_BUFFER, resource.vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * static_cast<qsizetype>(sizeof(GpuVertex)),
                     vertices.constData(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resource.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     mesh.indices.size() * static_cast<qsizetype>(sizeof(quint32)),
                     mesh.indices.constData(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                              reinterpret_cast<void *>(offsetof(GpuVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                              reinterpret_cast<void *>(offsetof(GpuVertex, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                              reinterpret_cast<void *>(offsetof(GpuVertex, texCoord)));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 4, GL_INT, sizeof(GpuVertex),
                               reinterpret_cast<void *>(offsetof(GpuVertex, boneIndices)));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(GpuVertex),
                              reinterpret_cast<void *>(offsetof(GpuVertex, weights)));
        meshes_.append(resource);
    }
    glBindVertexArray(0);
    modelUploadPending_ = false;
    if (meshes_.isEmpty()) {
        fail(RenderErrorCode::InvalidModel, QStringLiteral("模型数据无效"),
             QStringLiteral("没有可上传的非空网格"));
        return false;
    }
    return true;
}

void HandRenderWidget::releaseResources()
{
    for (const MeshResource &mesh : meshes_) {
        glDeleteBuffers(1, &mesh.ebo);
        glDeleteBuffers(1, &mesh.vbo);
        glDeleteVertexArrays(1, &mesh.vao);
    }
    meshes_.clear();
    if (overlayVbo_ != 0) {
        glDeleteBuffers(1, &overlayVbo_);
        overlayVbo_ = 0;
    }
    if (overlayVao_ != 0) {
        glDeleteVertexArrays(1, &overlayVao_);
        overlayVao_ = 0;
    }
}

void HandRenderWidget::rebuildBindPose()
{
    skeletonFrame_.reset();
    if (!model_) {
        skinMatrices_.clear();
        jointPositions_.clear();
        return;
    }
    const QMatrix4x4 displayRoot = computeDisplayRootTransform(*model_);
    skinMatrices_ = computeSkinMatrices(*model_, displayRoot);
    jointPositions_.resize(model_->bones.size());
    for (int index = 0; index < model_->bones.size(); ++index) {
        jointPositions_[index] = (displayRoot * model_->bones[index].bindWorld)
                                     .map(QVector3D(0.0F, 0.0F, 0.0F));
    }
}

void HandRenderWidget::fitCameraToModel()
{
    if (!model_) {
        camera_ = {};
        return;
    }
    const QMatrix4x4 displayRoot = computeDisplayRootTransform(*model_);
    const QVector<QMatrix4x4> palette = computeSkinMatrices(*model_, displayRoot);

    const float maximum = std::numeric_limits<float>::max();
    boundsMinimum_ = QVector3D(maximum, maximum, maximum);
    boundsMaximum_ = QVector3D(-maximum, -maximum, -maximum);
    bool hasVertex = false;
    qsizetype totalVertexCount = 0;
    for (const MeshData &mesh : model_->meshes) {
        totalVertexCount += mesh.vertices.size();
    }
    constexpr qsizetype MaximumBoundsSamples = 200000;
    const qsizetype sampleStride = std::max<qsizetype>(1, totalVertexCount / MaximumBoundsSamples);
    qsizetype vertexIndex = 0;
    for (const MeshData &mesh : model_->meshes) {
        for (const Vertex &vertex : mesh.vertices) {
            const bool sampleVertex = vertexIndex % sampleStride == 0;
            ++vertexIndex;
            if (!sampleVertex) {
                continue;
            }
            const QVector3D position = skinVertex(vertex, palette);
            boundsMinimum_.setX(std::min(boundsMinimum_.x(), position.x()));
            boundsMinimum_.setY(std::min(boundsMinimum_.y(), position.y()));
            boundsMinimum_.setZ(std::min(boundsMinimum_.z(), position.z()));
            boundsMaximum_.setX(std::max(boundsMaximum_.x(), position.x()));
            boundsMaximum_.setY(std::max(boundsMaximum_.y(), position.y()));
            boundsMaximum_.setZ(std::max(boundsMaximum_.z(), position.z()));
            hasVertex = true;
        }
    }
    if (!hasVertex) {
        boundsMinimum_ = QVector3D(-1.0F, -1.0F, -1.0F);
        boundsMaximum_ = QVector3D(1.0F, 1.0F, 1.0F);
    }
    camera_.target = (boundsMinimum_ + boundsMaximum_) * 0.5F;
    camera_.yawDegrees = -35.0F;
    camera_.pitchDegrees = 20.0F;
    camera_.distance = std::max(modelScale() * 1.8F, 0.01F);
}

void HandRenderWidget::updateMatrices()
{
    const float yaw = camera_.yawDegrees * Pi / 180.0F;
    const float pitch = camera_.pitchDegrees * Pi / 180.0F;
    const QVector3D direction(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                              std::cos(pitch) * std::cos(yaw));
    const QVector3D eye = camera_.target + direction * camera_.distance;
    viewMatrix_.setToIdentity();
    viewMatrix_.lookAt(eye, camera_.target, QVector3D(0.0F, 1.0F, 0.0F));
    projectionMatrix_.setToIdentity();
    projectionMatrix_.perspective(45.0F, static_cast<float>(std::max(1, width()))
                                             / static_cast<float>(std::max(1, height())),
                                  std::max(0.001F, camera_.distance * 0.001F),
                                  std::max(100.0F, camera_.distance + modelScale() * 10.0F));
}

void HandRenderWidget::drawMeshes()
{
    if (!model_ || skinMatrices_.size() != model_->bones.size()) {
        return;
    }
    meshProgram_->bind();
    meshProgram_->setUniformValue("uViewProjection", projectionMatrix_ * viewMatrix_);

    if (options_.doubleSided) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    }
    glDepthMask(options_.depthWrite ? GL_TRUE : GL_FALSE);

    for (const MeshResource &mesh : meshes_) {
        const int boneCount = std::min<int>(skinMatrices_.size(), MaximumBoneCount);
        meshProgram_->setUniformValueArray("uBones", skinMatrices_.constData(), boneCount);
        const QVector4D baseColor(mesh.color.redF(), mesh.color.greenF(), mesh.color.blueF(),
                                  mesh.color.alphaF() * options_.skinOpacity);
        meshProgram_->setUniformValue("uBaseColor", baseColor);
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    meshProgram_->release();
}

void HandRenderWidget::drawSkeleton()
{
    if (!model_ || jointPositions_.size() != model_->bones.size()) {
        return;
    }
    QVector<OverlayVertex> lines;
    QVector<OverlayVertex> points;
    const QVector3D segmentColor(0.55F, 0.62F, 0.70F);
    const QVector3D jointColor(0.20F, 0.85F, 1.0F);

    points.reserve(model_->bones.size());
    for (int index = 0; index < model_->bones.size(); ++index) {
        points.append({jointPositions_[index], jointColor});
    }
    for (const SemanticBoneSegment &segment : semanticBoneSegments()) {
        const int start = model_->boneIndexByName.value(segment.start, -1);
        const int end = model_->boneIndexByName.value(segment.end, -1);
        if (start < 0 || end < 0) {
            continue;
        }
        lines.append({jointPositions_[start], segmentColor});
        lines.append({jointPositions_[end], segmentColor});
    }

    overlayProgram_->bind();
    overlayProgram_->setUniformValue("uViewProjection", projectionMatrix_ * viewMatrix_);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glBindVertexArray(overlayVao_);
    glBindBuffer(GL_ARRAY_BUFFER, overlayVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                          reinterpret_cast<void *>(offsetof(OverlayVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                          reinterpret_cast<void *>(offsetof(OverlayVertex, color)));
    if (!lines.isEmpty()) {
        glBufferData(GL_ARRAY_BUFFER, lines.size() * static_cast<qsizetype>(sizeof(OverlayVertex)),
                     lines.constData(), GL_DYNAMIC_DRAW);
        overlayProgram_->setUniformValue("uPointSize", 1.0F);
        glLineWidth(2.0F);
        glDrawArrays(GL_LINES, 0, lines.size());
    }
    if (!points.isEmpty()) {
        glBufferData(GL_ARRAY_BUFFER, points.size() * static_cast<qsizetype>(sizeof(OverlayVertex)),
                     points.constData(), GL_DYNAMIC_DRAW);
        overlayProgram_->setUniformValue("uPointSize", static_cast<float>(8.0 * devicePixelRatioF()));
        glDrawArrays(GL_POINTS, 0, points.size());
    }
    glBindVertexArray(0);
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDepthMask(GL_TRUE);
    overlayProgram_->release();
}

void HandRenderWidget::screenRay(const QPointF &position, QVector3D &origin,
                                 QVector3D &direction) const
{
    const float x = 2.0F * static_cast<float>(position.x()) / std::max(1, width()) - 1.0F;
    const float y = 1.0F - 2.0F * static_cast<float>(position.y()) / std::max(1, height());
    bool invertible = false;
    const QMatrix4x4 inverse = (projectionMatrix_ * viewMatrix_).inverted(&invertible);
    if (!invertible) {
        origin = QVector3D();
        direction = QVector3D(0.0F, 0.0F, -1.0F);
        return;
    }
    const QVector3D nearPoint = inverse.map(QVector3D(x, y, -1.0F));
    const QVector3D farPoint = inverse.map(QVector3D(x, y, 1.0F));
    origin = nearPoint;
    direction = (farPoint - nearPoint).normalized();
}

int HandRenderWidget::pickJoint(const QPointF &position) const
{
    if (!model_ || jointPositions_.size() != model_->bones.size()) {
        return -1;
    }
    QVector3D rayOrigin;
    QVector3D rayDirection;
    screenRay(position, rayOrigin, rayDirection);
    const float radius = modelScale() * 0.03F;
    float nearest = std::numeric_limits<float>::max();
    int result = -1;
    for (int index = 0; index < jointPositions_.size(); ++index) {
        float distance = 0.0F;
        if (intersectSphere(rayOrigin, rayDirection, jointPositions_[index], radius, distance)
            && distance < nearest) {
            nearest = distance;
            result = index;
        }
    }
    return result;
}

QVector3D HandRenderWidget::displayJointPosition(int paletteIndex) const
{
    if (paletteIndex < 0 || paletteIndex >= jointPositions_.size()) {
        return QVector3D();
    }
    return jointPositions_[paletteIndex];
}

float HandRenderWidget::modelScale() const
{
    const float scale = (boundsMaximum_ - boundsMinimum_).length();
    return std::isfinite(scale) && scale > 1.0e-6F ? scale : 1.0F;
}

} // namespace handstudio
