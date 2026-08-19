#include "hand_render_widget.h"

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

namespace handdemo::render {
namespace {

constexpr int MaximumBoneCount = 128;
constexpr float DegreesPerPixel = 0.45F;
constexpr float Pi = 3.14159265358979323846F;

struct GpuVertex {
    float position[3];
    float normal[3];
    float texCoord[2];
    int boneIndices[4];
    float weights[4];
};

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

bool intersectCapsule(const QVector3D &rayOrigin, const QVector3D &rayDirection,
                      const QVector3D &start, const QVector3D &end, float radius,
                      float &distance)
{
    const QVector3D segment = end - start;
    const QVector3D offset = rayOrigin - start;
    const float segmentLengthSquared = segment.lengthSquared();
    if (segmentLengthSquared < 1.0e-12F) {
        return intersectSphere(rayOrigin, rayDirection, start, radius, distance);
    }
    const float raySegment = QVector3D::dotProduct(rayDirection, segment);
    const float rayOffset = QVector3D::dotProduct(rayDirection, offset);
    const float segmentOffset = QVector3D::dotProduct(segment, offset);
    const float denominator = segmentLengthSquared - raySegment * raySegment;
    float rayParameter = 0.0F;
    float segmentParameter = 0.0F;
    if (std::abs(denominator) > 1.0e-8F) {
        rayParameter = (raySegment * segmentOffset - segmentLengthSquared * rayOffset) / denominator;
        segmentParameter = (segmentOffset + raySegment * rayParameter) / segmentLengthSquared;
    } else {
        rayParameter = -rayOffset;
    }
    rayParameter = std::max(0.0F, rayParameter);
    segmentParameter = std::clamp(segmentParameter, 0.0F, 1.0F);
    rayParameter = std::max(0.0F, QVector3D::dotProduct(start + segment * segmentParameter - rayOrigin,
                                                        rayDirection));
    segmentParameter = std::clamp(QVector3D::dotProduct(rayOrigin + rayDirection * rayParameter - start,
                                                        segment) / segmentLengthSquared, 0.0F, 1.0F);
    const QVector3D rayPoint = rayOrigin + rayDirection * rayParameter;
    const QVector3D segmentPoint = start + segment * segmentParameter;
    if ((rayPoint - segmentPoint).lengthSquared() > radius * radius) {
        return false;
    }
    const float penetration = std::sqrt(std::max(0.0F, radius * radius
        - (rayPoint - segmentPoint).lengthSquared()));
    distance = std::max(0.0F, rayParameter - penetration);
    return true;
}

QVector3D transformedPoint(const QMatrix4x4 &matrix, const QVector3D &point)
{
    return matrix.map(point);
}

}

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

void HandRenderWidget::setModel(std::shared_ptr<const handrig::RiggedModel> model)
{
    model_ = std::move(model);
    selectedBone_ = -1;
    lastError_.reset();
    editingEnabled_ = false;
    modelUploadPending_ = model_ != nullptr;
    if (!model_) {
        pose_ = {};
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

void HandRenderWidget::setPoseResult(const motion::PoseResult &pose)
{
    if (!model_ || pose.skinMatrices.size() != model_->bones.size()
        || pose.globalMatrices.size() != model_->bones.size()) {
        fail(RenderErrorCode::InvalidPose, QStringLiteral("姿态数据无效"),
             QStringLiteral("globalMatrices 和 skinMatrices 必须与模型骨骼数量一致"));
        return;
    }
    pose_ = pose;
    editingEnabled_ = initialized_ && !meshes_.isEmpty() && !lastError_.has_value();
    update();
}

void HandRenderWidget::setBoneInteractions(const QVector<BoneInteraction> &interactions)
{
    interactions_.clear();
    editableBones_.clear();
    if (!model_) {
        return;
    }
    for (const BoneInteraction &interaction : interactions) {
        if (interaction.boneIndex < 0 || interaction.boneIndex >= model_->bones.size()) {
            continue;
        }
        BoneInteraction normalized = interaction;
        normalized.localAxes.clear();
        for (const QVector3D &axis : interaction.localAxes) {
            if (axis.lengthSquared() > 1.0e-12F) {
                normalized.localAxes.append(axis.normalized());
            }
        }
        if (!normalized.localAxes.isEmpty()) {
            interactions_.append(normalized);
            editableBones_.insert(normalized.boneIndex);
        }
    }
    if (!editableBones_.contains(selectedBone_)) {
        setSelectedBone(-1);
    }
    update();
}

void HandRenderWidget::setSelectedBone(int boneIndex)
{
    const int accepted = editableBones_.contains(boneIndex) ? boneIndex : -1;
    if (selectedBone_ == accepted) {
        return;
    }
    selectedBone_ = accepted;
    activeAxis_ = -1;
    emit boneSelected(selectedBone_);
    update();
}

void HandRenderWidget::setCameraState(const CameraState &state)
{
    camera_ = state;
    camera_.pitchDegrees = clampPitch(camera_.pitchDegrees);
    camera_.distance = std::max(camera_.distance, modelScale() * 0.02F);
    emit cameraChanged(camera_);
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
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
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
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!initialized_ || lastError_.has_value() || !model_ || meshes_.isEmpty()) {
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
    if (event->button() != Qt::LeftButton) {
        return;
    }
    if (editingEnabled_) {
        activeAxis_ = pickManipulatorAxis(event->position());
        if (activeAxis_ >= 0) {
            dragMode_ = DragMode::Manipulator;
            return;
        }
        const int pickedBone = pickBone(event->position());
        if (pickedBone >= 0) {
            setSelectedBone(pickedBone);
            dragMode_ = DragMode::None;
            return;
        }
        setSelectedBone(-1);
    }
    dragMode_ = DragMode::Orbit;
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
    } else if (dragMode_ == DragMode::Manipulator && selectedBone_ >= 0 && activeAxis_ >= 0) {
        const auto iterator = std::find_if(interactions_.cbegin(), interactions_.cend(),
            [this](const BoneInteraction &entry) { return entry.boneIndex == selectedBone_; });
        if (iterator != interactions_.cend() && activeAxis_ < iterator->localAxes.size()) {
            const QVector3D origin = displayBonePosition(selectedBone_);
            const QVector3D worldAxis = displayBoneAxis(selectedBone_, iterator->localAxes[activeAxis_]);
            const QPointF screenOrigin = projectToScreen(origin);
            const QPointF screenAxis = projectToScreen(origin + worldAxis * modelScale() * 0.2F) - screenOrigin;
            QPointF tangent(-screenAxis.y(), screenAxis.x());
            const float tangentLength = std::hypot(tangent.x(), tangent.y());
            if (tangentLength > 1.0e-4F) {
                tangent /= tangentLength;
                const float degrees = static_cast<float>(delta.x() * tangent.x() + delta.y() * tangent.y())
                                      * DegreesPerPixel;
                emit localRotationDeltaRequested(selectedBone_, iterator->localAxes[activeAxis_] * degrees);
            }
        }
    }
}

void HandRenderWidget::mouseReleaseEvent(QMouseEvent *)
{
    dragMode_ = DragMode::None;
    activeAxis_ = -1;
}

void HandRenderWidget::wheelEvent(QWheelEvent *event)
{
    camera_.distance *= std::exp(-static_cast<float>(event->angleDelta().y()) / 1200.0F);
    camera_.distance = std::clamp(camera_.distance, modelScale() * 0.02F, modelScale() * 100.0F);
    emit cameraChanged(camera_);
    update();
    event->accept();
}

void HandRenderWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F) {
        resetCamera();
        event->accept();
        return;
    }
    QOpenGLWidget::keyPressEvent(event);
}

void HandRenderWidget::fail(RenderErrorCode code, const QString &message, const QString &detail)
{
    lastError_ = RenderError{code, message, detail};
    editingEnabled_ = false;
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
uniform mat4 uMeshTransform;
out vec3 vNormal;
void main()
{
    mat4 skin = mat4(0.0);
    float totalWeight = 0.0;
    for (int index = 0; index < 4; ++index) {
        if (aWeights[index] > 0.0 && aBoneIndices[index] >= 0) {
            skin += uBones[aBoneIndices[index]] * aWeights[index];
            totalWeight += aWeights[index];
        }
    }
    mat4 transform = totalWeight > 0.0 ? skin : uMeshTransform;
    vec4 worldPosition = transform * vec4(aPosition, 1.0);
    vNormal = normalize(mat3(transform) * aNormal);
    gl_Position = uViewProjection * worldPosition;
}
)";
    static const char *meshFragmentShader = R"(
#version 330 core
in vec3 vNormal;
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
        const handrig::MeshData &mesh = model_->meshes[modelMeshIndex];
        if (mesh.vertices.isEmpty() || mesh.indices.isEmpty()) {
            continue;
        }
        QVector<GpuVertex> vertices;
        vertices.reserve(mesh.vertices.size());
        for (const handrig::Vertex &source : mesh.vertices) {
            GpuVertex vertex{};
            vertex.position[0] = source.position.x();
            vertex.position[1] = source.position.y();
            vertex.position[2] = source.position.z();
            vertex.normal[0] = source.normal.x();
            vertex.normal[1] = source.normal.y();
            vertex.normal[2] = source.normal.z();
            vertex.texCoord[0] = source.texCoord.x();
            vertex.texCoord[1] = source.texCoord.y();
            std::copy(source.influence.boneIndices.cbegin(), source.influence.boneIndices.cend(), vertex.boneIndices);
            std::copy(source.influence.weights.cbegin(), source.influence.weights.cend(), vertex.weights);
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
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * static_cast<qsizetype>(sizeof(quint32)),
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
    editingEnabled_ = !meshes_.isEmpty() && !lastError_.has_value();
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
    pose_ = {};
    pose_.pose.localPoses.resize(model_->bones.size());
    pose_.globalMatrices.resize(model_->bones.size());
    pose_.skinMatrices.resize(model_->bones.size());
    pose_.joints.resize(model_->bones.size());
    for (int index = 0; index < model_->bones.size(); ++index) {
        const handrig::BoneData &bone = model_->bones[index];
        pose_.globalMatrices[index] = bone.parentIndex >= 0
            ? pose_.globalMatrices[bone.parentIndex] * bone.bindLocal : bone.bindLocal;
        pose_.skinMatrices[index] = model_->globalInverse * pose_.globalMatrices[index] * bone.inverseBind;
    }
}

void HandRenderWidget::fitCameraToModel()
{
    if (!model_) {
        camera_ = {};
        return;
    }
    const float maximum = std::numeric_limits<float>::max();
    boundsMinimum_ = QVector3D(maximum, maximum, maximum);
    boundsMaximum_ = QVector3D(-maximum, -maximum, -maximum);
    bool hasVertex = false;
    qsizetype totalVertexCount = 0;
    for (const handrig::MeshData &mesh : model_->meshes) {
        totalVertexCount += mesh.vertices.size();
    }
    constexpr qsizetype MaximumBoundsSamples = 200000;
    const qsizetype sampleStride = std::max<qsizetype>(1, totalVertexCount / MaximumBoundsSamples);
    qsizetype vertexIndex = 0;
    for (const handrig::MeshData &mesh : model_->meshes) {
        for (const handrig::Vertex &vertex : mesh.vertices) {
            const bool sampleVertex = vertexIndex % sampleStride == 0;
            ++vertexIndex;
            if (!sampleVertex) {
                continue;
            }
            QVector3D position;
            float totalWeight = 0.0F;
            for (int influence = 0; influence < 4; ++influence) {
                const int boneIndex = vertex.influence.boneIndices[static_cast<std::size_t>(influence)];
                const float weight = vertex.influence.weights[static_cast<std::size_t>(influence)];
                if (weight > 0.0F && boneIndex >= 0 && boneIndex < model_->bones.size()) {
                    const QMatrix4x4 transform = handrig::meshSkinTransform(
                        *model_, mesh, pose_.globalMatrices[boneIndex], boneIndex);
                    position += transformedPoint(transform, vertex.position) * weight;
                    totalWeight += weight;
                }
            }
            if (totalWeight <= 0.0F) {
                position = transformedPoint(mesh.bindTransform, vertex.position);
            }
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
    meshProgram_->bind();
    meshProgram_->setUniformValue("uViewProjection", projectionMatrix_ * viewMatrix_);
    for (const MeshResource &mesh : meshes_) {
        const handrig::MeshData &modelMesh = model_->meshes[mesh.modelMeshIndex];
        QVector<QMatrix4x4> palette(model_->bones.size());
        for (int boneIndex = 0; boneIndex < model_->bones.size(); ++boneIndex) {
            palette[boneIndex] = handrig::meshSkinTransform(
                *model_, modelMesh, pose_.globalMatrices[boneIndex], boneIndex);
        }
        meshProgram_->setUniformValueArray("uBones", palette.constData(), palette.size());
        meshProgram_->setUniformValue("uMeshTransform", modelMesh.bindTransform);
        meshProgram_->setUniformValue("uBaseColor", mesh.color);
        glBindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    }
    glBindVertexArray(0);
    meshProgram_->release();
}

void HandRenderWidget::drawSkeleton()
{
    QVector<OverlayVertex> lines;
    QVector<OverlayVertex> points;
    for (int index = 0; index < model_->bones.size(); ++index) {
        const QVector3D position = displayBonePosition(index);
        const bool selected = index == selectedBone_;
        const QVector3D color = selected ? QVector3D(1.0F, 0.75F, 0.1F)
                                         : (editableBones_.contains(index) ? QVector3D(0.2F, 0.85F, 1.0F)
                                                                          : QVector3D(0.55F, 0.62F, 0.7F));
        points.append({position, color});
        const int parent = model_->bones[index].parentIndex;
        if (parent >= 0) {
            lines.append({displayBonePosition(parent), color});
            lines.append({position, color});
        }
    }
    const auto interaction = std::find_if(interactions_.cbegin(), interactions_.cend(),
        [this](const BoneInteraction &entry) { return entry.boneIndex == selectedBone_; });
    if (interaction != interactions_.cend()) {
        const std::array<QVector3D, 3> colors{QVector3D(1.0F, 0.2F, 0.2F), QVector3D(0.2F, 1.0F, 0.25F),
                                              QVector3D(0.25F, 0.45F, 1.0F)};
        const QVector3D origin = displayBonePosition(selectedBone_);
        for (int axisIndex = 0; axisIndex < interaction->localAxes.size(); ++axisIndex) {
            const QVector3D color = colors[static_cast<std::size_t>(axisIndex % colors.size())];
            lines.append({origin, color});
            lines.append({origin + displayBoneAxis(selectedBone_, interaction->localAxes[axisIndex])
                                  * modelScale() * 0.18F, color});
        }
    }
    overlayProgram_->bind();
    overlayProgram_->setUniformValue("uViewProjection", projectionMatrix_ * viewMatrix_);
    glDisable(GL_CULL_FACE);
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
    glEnable(GL_CULL_FACE);
    overlayProgram_->release();
}

QVector3D HandRenderWidget::displayBonePosition(int boneIndex) const
{
    return transformedPoint(model_->globalInverse * pose_.globalMatrices[boneIndex], QVector3D());
}

QVector3D HandRenderWidget::displayBoneAxis(int boneIndex, const QVector3D &localAxis) const
{
    return (model_->globalInverse * pose_.globalMatrices[boneIndex]).mapVector(localAxis).normalized();
}

int HandRenderWidget::pickBone(const QPointF &position) const
{
    QVector3D rayOrigin;
    QVector3D rayDirection;
    screenRay(position, rayOrigin, rayDirection);
    const float radius = modelScale() * 0.025F;
    float nearest = std::numeric_limits<float>::max();
    int result = -1;
    for (int boneIndex : editableBones_) {
        float distance = 0.0F;
        const QVector3D end = displayBonePosition(boneIndex);
        const int parent = model_->bones[boneIndex].parentIndex;
        const bool hit = parent >= 0
            ? intersectCapsule(rayOrigin, rayDirection, displayBonePosition(parent), end, radius, distance)
            : intersectSphere(rayOrigin, rayDirection, end, radius, distance);
        if (hit && distance < nearest) {
            nearest = distance;
            result = boneIndex;
        }
    }
    return result;
}

int HandRenderWidget::pickManipulatorAxis(const QPointF &position) const
{
    const auto interaction = std::find_if(interactions_.cbegin(), interactions_.cend(),
        [this](const BoneInteraction &entry) { return entry.boneIndex == selectedBone_; });
    if (interaction == interactions_.cend()) {
        return -1;
    }
    const QVector3D origin = displayBonePosition(selectedBone_);
    const QPointF screenOrigin = projectToScreen(origin);
    float nearest = 10.0F;
    int result = -1;
    for (int index = 0; index < interaction->localAxes.size(); ++index) {
        const QPointF end = projectToScreen(origin + displayBoneAxis(selectedBone_, interaction->localAxes[index])
                                                    * modelScale() * 0.18F);
        const QPointF segment = end - screenOrigin;
        const float lengthSquared = static_cast<float>(segment.x() * segment.x() + segment.y() * segment.y());
        if (lengthSquared < 1.0e-4F) {
            continue;
        }
        const QPointF offset = position - screenOrigin;
        const float parameter = std::clamp(static_cast<float>((offset.x() * segment.x() + offset.y() * segment.y())
                                                              / lengthSquared), 0.15F, 1.0F);
        const QPointF closest = screenOrigin + segment * parameter;
        const float distance = static_cast<float>(std::hypot(position.x() - closest.x(), position.y() - closest.y()));
        if (distance < nearest) {
            nearest = distance;
            result = index;
        }
    }
    return result;
}

void HandRenderWidget::screenRay(const QPointF &position, QVector3D &origin, QVector3D &direction) const
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

QPointF HandRenderWidget::projectToScreen(const QVector3D &position, bool *visible) const
{
    const QVector4D clip = projectionMatrix_ * viewMatrix_ * QVector4D(position, 1.0F);
    const bool isVisible = clip.w() > 0.0F;
    if (visible != nullptr) {
        *visible = isVisible;
    }
    if (std::abs(clip.w()) < 1.0e-8F) {
        return {};
    }
    const QVector3D normalized = clip.toVector3DAffine();
    return QPointF((normalized.x() * 0.5F + 0.5F) * width(),
                   (0.5F - normalized.y() * 0.5F) * height());
}

float HandRenderWidget::modelScale() const
{
    const float scale = (boundsMaximum_ - boundsMinimum_).length();
    return std::isfinite(scale) && scale > 1.0e-6F ? scale : 1.0F;
}

}
