#include "main_window.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("SlimeVRResearch"));
    QCoreApplication::setApplicationName(QStringLiteral("SixImuSolverQt"));

    QApplication application(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("手套六 IMU 实时解算 Qt 工具"));
    parser.addHelpOption();
    const QCommandLineOption demoOption(QStringLiteral("demo"), QStringLiteral("启用演示模式"));
    const QCommandLineOption quitAfterOption(QStringLiteral("quit-after-ms"),
                                              QStringLiteral("指定正整数毫秒后退出"),
                                              QStringLiteral("milliseconds"));
    parser.addOption(demoOption);
    parser.addOption(quitAfterOption);
    parser.process(application);

    int quitAfterMs = 0;
    if (parser.isSet(quitAfterOption)) {
        bool converted = false;
        quitAfterMs = parser.value(quitAfterOption).toInt(&converted);
        if (!converted || quitAfterMs <= 0) {
            QTextStream(stderr) << QStringLiteral("错误：--quit-after-ms 必须是正整数。\n");
            return 2;
        }
    }

    MainWindow window;
    window.show();
    if (parser.isSet(demoOption)) {
        window.setDemoModeEnabled(true);
    }
    if (quitAfterMs > 0) {
        QTimer::singleShot(quitAfterMs, &application, &QCoreApplication::quit);
    }

    return application.exec();
}
