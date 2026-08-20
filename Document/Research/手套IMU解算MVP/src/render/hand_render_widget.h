#pragma once

// OpenGL 3.3 Core 蒙皮渲染 + 骨架覆盖层 + 轨道相机 + 拾取基础（SubStage 5）。
//
// 只消费 HandSkeletonFrame 的 skinMatrix / globalMatrix（按骨名匹配），不读取融合器。
// 覆盖层显示 25 关节与 19 条语义骨段（语义链来自 standard_joints.h，不从节点 parent 画线）。

#include "core/hand_skeleton_frame.h"
#include "model/model_data.h"

#include <QMatrix4x4>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>
#include <QVector3D>

#include <memory>
#include <optional>

class QMouseEvent;
class QWheelEvent;
class QOpenGLShaderProgram;

namespace handstudio {

enum class RenderErrorCode {
    OpenGlVersionUnsupported,
    ShaderCompilationFailed,
    ResourceCreationFailed,
    TooManyBones,
    InvalidModel,
    InvalidSkeletonFrame,
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

struct RenderOptions {
    float skinOpacity{1.0F}; // 半透明皮肤 alpha（与材质 alpha 相乘）
    bool doubleSided{true};  // 双面渲染（关背面剔除）
    bool depthWrite{true};   // 深度写控制
};

class HandRenderWidget final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit HandRenderWidget(QWidget *parent = nullptr);
    ~HandRenderWidget() override;

public slots:
    void setModel(std::shared_ptr<const RiggedModel> model);
    void setSkeletonFrame(const HandSkeletonFrame &frame);
    void resetCamera();

public:
    void setRenderOptions(const RenderOptions &options);
    [[nodiscard]] CameraState cameraState() const noexcept { return camera_; }
    [[nodiscard]] const std::optional<RenderError> &lastError() const noexcept { return lastError_; }
    [[nodiscard]] int pickJoint(const QPointF &position) const;

signals:
    void renderFailed(const RenderError &error);
    void cameraChanged(const CameraState &state);
    void frameRendered();

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct MeshResource;
    struct OverlayVertex;
    enum class DragMode { None, Orbit, Pan };

    void fail(RenderErrorCode code, const QString &message, const QString &detail);
    bool createPrograms();
    bool uploadModel();
    void releaseResources();
    void rebuildBindPose();
    void fitCameraToModel();
    void updateMatrices();
    void drawMeshes();
    void drawSkeleton();
    void screenRay(const QPointF &position, QVector3D &origin, QVector3D &direction) const;
    [[nodiscard]] QVector3D displayJointPosition(int paletteIndex) const;
    [[nodiscard]] float modelScale() const;

    std::shared_ptr<const RiggedModel> model_;
    std::optional<HandSkeletonFrame> skeletonFrame_;
    QVector<QMatrix4x4> skinMatrices_;  // palette 顺序（bind 或 frame）
    QVector<QVector3D> jointPositions_; // palette 顺序（显示空间）
    QVector<MeshResource> meshes_;
    std::unique_ptr<QOpenGLShaderProgram> meshProgram_;
    std::unique_ptr<QOpenGLShaderProgram> overlayProgram_;
    unsigned int overlayVao_{0};
    unsigned int overlayVbo_{0};
    QMatrix4x4 viewMatrix_;
    QMatrix4x4 projectionMatrix_;
    CameraState camera_;
    RenderOptions options_;
    QVector3D boundsMinimum_;
    QVector3D boundsMaximum_;
    QPoint lastMousePosition_;
    DragMode dragMode_{DragMode::None};
    bool initialized_{false};
    bool modelUploadPending_{false};
    std::optional<RenderError> lastError_;
};

} // namespace handstudio

Q_DECLARE_METATYPE(handstudio::CameraState)
Q_DECLARE_METATYPE(handstudio::RenderError)
