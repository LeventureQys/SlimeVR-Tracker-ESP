#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("IMUExampleQt"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({QStringLiteral("demo"), QStringLiteral("启动后开启演示模式")});
    parser.addOption({QStringLiteral("quit-after-ms"), QStringLiteral("指定毫秒后自动退出"),
                      QStringLiteral("milliseconds")});
    parser.process(application);

    MainWindow window;
    window.show();
    if (parser.isSet(QStringLiteral("demo"))) {
        window.setDemoModeEnabled(true);
    }
    if (parser.isSet(QStringLiteral("quit-after-ms"))) {
        bool valid = false;
        const int milliseconds = parser.value(QStringLiteral("quit-after-ms")).toInt(&valid);
        if (valid && milliseconds > 0) {
            QTimer::singleShot(milliseconds, &application, &QCoreApplication::quit);
        }
    }
    return application.exec();
}
