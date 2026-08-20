#include "app/runtime_controller.h"
#include "core/metatype_registration.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSurfaceFormat>
#include <QTimer>

using namespace handstudio;

namespace {

QString releaseAsset(const QString &name)
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    const QString besideExe = appDir.filePath(QStringLiteral("assets/") + name);
    if (QFileInfo::exists(besideExe)) return besideExe;
    return QFileInfo(QDir(appDir.filePath(QStringLiteral("../.."))).filePath(QStringLiteral("assets/") + name)).absoluteFilePath();
}

}

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("HandSkeletonStudio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0.0"));
    registerCoreMetaTypes();
    qRegisterMetaType<SourceState>();
    qRegisterMetaType<RecorderState>();
    qRegisterMetaType<std::array<FusedImuPose, 6>>();
    qRegisterMetaType<std::shared_ptr<const RiggedModel>>();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("六路 IMU 手部骨骼工作室"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("source"), QStringLiteral("serial|replay|demo"), QStringLiteral("type"), QStringLiteral("demo")});
    parser.addOption({QStringLiteral("port"), QStringLiteral("串口名称"), QStringLiteral("port")});
    parser.addOption({QStringLiteral("replay"), QStringLiteral("录制会话目录"), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("model"), QStringLiteral("GLB 模型路径"), QStringLiteral("glb")});
    parser.addOption({QStringLiteral("config"), QStringLiteral("运行时配置路径"), QStringLiteral("json")});
    parser.addOption({QStringLiteral("auto-exit-ms"), QStringLiteral("自动退出毫秒数"), QStringLiteral("ms"), QStringLiteral("0")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("自动截图路径"), QStringLiteral("png")});
    parser.addOption({QStringLiteral("record-dir"), QStringLiteral("启动后自动录制目录"), QStringLiteral("dir")});
    parser.process(application);

    RuntimeOptions options;
    options.source = parser.value(QStringLiteral("source"));
    options.port = parser.value(QStringLiteral("port"));
    options.replayPath = parser.value(QStringLiteral("replay"));
    options.modelPath = parser.isSet(QStringLiteral("model")) ? parser.value(QStringLiteral("model"))
                                                               : releaseAsset(QStringLiteral("generic-hand-left.glb"));
    options.rigPath = releaseAsset(QStringLiteral("hand_rig_generic_left.json"));
    options.configPath = parser.isSet(QStringLiteral("config")) ? parser.value(QStringLiteral("config"))
                                                                 : releaseAsset(QStringLiteral("default_runtime.json"));

    RuntimeController controller;
    QString error;
    if (!controller.initialize(options, &error)) {
        qCritical().noquote() << error;
        return 2;
    }

    MainWindow window(&controller);
    window.show();
    controller.start();
    const QString recordDir = parser.value(QStringLiteral("record-dir"));
    if (!recordDir.isEmpty()) QTimer::singleShot(200, &controller, [&controller, recordDir] { controller.startRecording(recordDir); });

    const QString screenshot = parser.value(QStringLiteral("screenshot"));
    if (!screenshot.isEmpty()) {
        QTimer::singleShot(3000, &window, [&window, screenshot] {
            QDir().mkpath(QFileInfo(screenshot).absolutePath());
            window.grab().save(screenshot, "PNG");
        });
    }
    const int autoExitMs = parser.value(QStringLiteral("auto-exit-ms")).toInt();
    if (autoExitMs > 0) QTimer::singleShot(autoExitMs, &window, &QWidget::close);

    return application.exec();
}
