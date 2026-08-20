#include "app/runtime_controller.h"
#include "render/hand_render_widget.h"
#include "ui/main_window.h"

#include <QPushButton>
#include <QtTest>

using namespace handstudio;

class TestAppIntegration : public QObject {
    Q_OBJECT

private slots:
    void mainWindowCreatesAndCameraResetIsAlgorithmIndependent();
    void demoRunsAndShutdownStopsWorker();
};

static RuntimeOptions options()
{
    RuntimeOptions value;
    value.source = QStringLiteral("demo");
    value.modelPath = QStringLiteral(HANDSTUDIO_TEST_GLB_PATH);
    value.rigPath = QStringLiteral(HANDSTUDIO_TEST_RIG_PATH);
    value.configPath = QStringLiteral(HANDSTUDIO_TEST_RUNTIME_PATH);
    return value;
}

void TestAppIntegration::mainWindowCreatesAndCameraResetIsAlgorithmIndependent()
{
    RuntimeController controller;
    QString error;
    QVERIFY2(controller.initialize(options(), &error), qPrintable(error));
    MainWindow window(&controller);
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("cameraResetButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("recordButton")));
    const QString modelPath = controller.modelPath();
    const CameraState before = window.renderWidget()->cameraState();
    window.renderWidget()->resetCamera();
    QCOMPARE(controller.modelPath(), modelPath);
    QCOMPARE(window.renderWidget()->cameraState().yawDegrees, before.yawDegrees);
    window.close();
    QVERIFY(!controller.isWorkerRunning());
}

void TestAppIntegration::demoRunsAndShutdownStopsWorker()
{
    RuntimeController controller;
    QString error;
    QVERIFY2(controller.initialize(options(), &error), qPrintable(error));
    QSignalSpy frameSpy(&controller, &RuntimeController::skeletonFrameReady);
    controller.start();
    QTRY_VERIFY_WITH_TIMEOUT(frameSpy.count() > 0, 3000);
    QVERIFY(controller.isWorkerRunning());
    controller.stop();
    QVERIFY(!controller.isWorkerRunning());
}

QTEST_MAIN(TestAppIntegration)
#include "test_app_integration.moc"
