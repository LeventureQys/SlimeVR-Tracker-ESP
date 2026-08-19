#pragma once

#include "core/model_data.h"
#include "motion/motion_types.h"

#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QSet>
#include <QVector>

#include <memory>

class QKeyEvent;
class QMouseEvent;
class QOpenGLShaderProgram;
class QWheelEvent;

namespace handdemo::render {

enum class RenderErrorCode {
    OpenGlVersionUnsupported,
    ShaderCompilationFailed,
    ResourceCreationFailed,
    TooManyBones,
    InvalidModel,
    InvalidPose
};

struct RenderError {
    RenderErrorCode code{RenderErrorCode::InvalidModel};
    QString message;
    QString detail;
};

struct CameraState {
    QVector3D target;
    float yawDegrees{-35.0F};
    float pitchDegrees{20.0F};
    float distance{5.0F};
};

struct BoneInteraction {
    int boneIndex{-1};
    QVector<QVector3D> localAxes;
};

class HandRenderWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit HandRenderWidget(QWidget *parent = nullptr);
    ~HandRenderWidget() override;

    void setModel(std::shared_ptr<const handrig::RiggedModel> model);
    void setPoseResult(const motion::PoseResult &pose);
    void setBoneInteractions(const QVector<BoneInteraction> &interactions);
    void setSelectedBone(int boneIndex);
    void setCameraState(const CameraState &state);
    void resetCamera();

    [[nodiscard]] int selectedBone() const noexcept { return selectedBone_; }
    [[nodiscard]] CameraState cameraState() const noexcept { return camera_; }
    [[nodiscard]] bool editingEnabled() const noexcept { return editingEnabled_; }
    [[nodiscard]] const std::optional<RenderError> &lastError() const noexcept { return lastError_; }

signals:
    void boneSelected(int boneIndex);
    void localRotationDeltaRequested(int boneIndex, const QVector3D &deltaDegrees);
    void cameraChanged(const handdemo::render::CameraState &state);
    void renderFailed(const handdemo::render::RenderError &error);
    void frameRendered();

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct MeshResource;
    struct OverlayVertex;
    enum class DragMode { None, Orbit, Pan, Manipulator };

    void fail(RenderErrorCode code, const QString &message, const QString &detail);
    bool createPrograms();
    bool uploadModel();
    void releaseResources();
    void rebuildBindPose();
    void fitCameraToModel();
    void updateMatrices();
    void drawMeshes();
    void drawSkeleton();
    QVector3D displayBonePosition(int boneIndex) const;
    QVector3D displayBoneAxis(int boneIndex, const QVector3D &localAxis) const;
    int pickBone(const QPointF &position) const;
    int pickManipulatorAxis(const QPointF &position) const;
    void screenRay(const QPointF &position, QVector3D &origin, QVector3D &direction) const;
    QPointF projectToScreen(const QVector3D &position, bool *visible = nullptr) const;
    float modelScale() const;

    std::shared_ptr<const handrig::RiggedModel> model_;
    motion::PoseResult pose_;
    QVector<MeshResource> meshes_;
    QVector<BoneInteraction> interactions_;
    QSet<int> editableBones_;
    std::unique_ptr<QOpenGLShaderProgram> meshProgram_;
    std::unique_ptr<QOpenGLShaderProgram> overlayProgram_;
    unsigned int overlayVao_{0};
    unsigned int overlayVbo_{0};
    QMatrix4x4 viewMatrix_;
    QMatrix4x4 projectionMatrix_;
    CameraState camera_;
    QVector3D boundsMinimum_;
    QVector3D boundsMaximum_;
    QPoint lastMousePosition_;
    DragMode dragMode_{DragMode::None};
    int selectedBone_{-1};
    int activeAxis_{-1};
    bool initialized_{false};
    bool modelUploadPending_{false};
    bool editingEnabled_{false};
    std::optional<RenderError> lastError_;
};

}

Q_DECLARE_METATYPE(handdemo::render::CameraState)
Q_DECLARE_METATYPE(handdemo::render::RenderError)
