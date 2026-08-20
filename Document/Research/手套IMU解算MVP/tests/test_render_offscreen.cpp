#include <QtTest>

#include "core/hand_skeleton_frame.h"
#include "model/model_data.h"
#include "model/model_importer.h"
#include "render/hand_render_widget.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QOpenGLContext>
#include <QOpenGLFunctions_3_3_Core>

#include <memory>

#ifndef HANDSTUDIO_TEST_GLB_PATH
#error "HANDSTUDIO_TEST_GLB_PATH must be defined"
#endif

namespace {

const char *GlbPath = HANDSTUDIO_TEST_GLB_PATH;

} // namespace

class RenderOffscreenTest final : public QObject {
    Q_OBJECT

private slots:
    void createsRendersAndNoGlError();
    void invalidModelDiagnosed();
    void skeletonFrameConsumed();
};

void RenderOffscreenTest::createsRendersAndNoGlError()
{
    const handstudio::ModelLoadResult result = handstudio::ModelImporter{}.load(GlbPath);
    QVERIFY2(result.hasValue(), result.hasError() ? qPrintable(result.error->detail) : "no error");
    auto model = std::make_shared<const handstudio::RiggedModel>(*result.value);

    handstudio::HandRenderWidget widget;
    widget.resize(640, 480);
    widget.setModel(model);
    widget.show();
    QApplication::processEvents();

    QVERIFY2(!widget.lastError().has_value(),
             widget.lastError().has_value()
                 ? qPrintable(widget.lastError()->message + QStringLiteral(": ")
                              + widget.lastError()->detail)
                 : "no error");

    // 提交一帧并确认产生非空帧缓冲。
    const QImage frame = widget.grabFramebuffer();
    QVERIFY(!frame.isNull());
    QCOMPARE(frame.width(), 640);
    QCOMPARE(frame.height(), 480);

    // 离屏截图证据（构建目录下）。
    const QString screenshotPath =
        QDir::current().filePath(QStringLiteral("render_offscreen_frame.png"));
    if (frame.save(screenshotPath, "PNG")) {
        qInfo().noquote() << QStringLiteral("screenshot saved: %1").arg(QDir::toNativeSeparators(screenshotPath));
    }

    // 显式读取 GL 错误码（当前上下文为 widget 的上下文）。
    widget.makeCurrent();
    QOpenGLFunctions_3_3_Core functions;
    functions.initializeOpenGLFunctions();
    const GLenum glError = functions.glGetError();
    widget.doneCurrent();
    QCOMPARE(static_cast<int>(glError), static_cast<int>(GL_NO_ERROR));
}

void RenderOffscreenTest::invalidModelDiagnosed()
{
    handstudio::HandRenderWidget widget;
    const auto empty = std::make_shared<const handstudio::RiggedModel>();
    widget.setModel(empty);
    QVERIFY(widget.lastError().has_value());
    QCOMPARE(widget.lastError()->code, handstudio::RenderErrorCode::InvalidModel);
}

void RenderOffscreenTest::skeletonFrameConsumed()
{
    const handstudio::ModelLoadResult result = handstudio::ModelImporter{}.load(GlbPath);
    QVERIFY(result.hasValue());
    auto model = std::make_shared<const handstudio::RiggedModel>(*result.value);

    handstudio::HandRenderWidget widget;
    widget.resize(640, 480);
    widget.setModel(model);
    widget.show();
    QApplication::processEvents();

    const QMatrix4x4 displayRoot = handstudio::computeDisplayRootTransform(*model);
    const QVector<QMatrix4x4> skin = handstudio::computeSkinMatrices(*model, displayRoot);

    handstudio::HandSkeletonFrame frame;
    frame.skeletonId = QStringLiteral("generic-hand-left");
    frame.bones.resize(model->bones.size());
    for (int index = 0; index < model->bones.size(); ++index) {
        handstudio::HandBoneFrame &bone = frame.bones[index];
        bone.boneName = model->bones[index].name;
        bone.valid = true;
        bone.skinMatrix = skin[index];
        bone.globalMatrix = displayRoot * model->bones[index].bindWorld;
    }
    widget.setSkeletonFrame(frame);
    QApplication::processEvents();
    QVERIFY(!widget.lastError().has_value());
}

QTEST_MAIN(RenderOffscreenTest)
#include "test_render_offscreen.moc"
