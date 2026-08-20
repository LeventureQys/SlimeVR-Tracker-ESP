// Headless SlimeVR bridge probe: runs the full six-IMU pipeline (demo data
// by default, or a real serial port) and streams SensorInfo + RotationData
// to a SlimeVR Server for real-server verification without the GUI.
//
//   slimevr_bridge_probe --host 127.0.0.1 --port 6969 --side left --rate 75
//   slimevr_bridge_probe --serial COM7 --side right --quit-after-ms 60000

#include "demo_data_source.h"
#include "frame_stream_parser.h"
#include "sequence_grouper.h"
#include "serial_data_source.h"
#include "six_imu_solver.h"
#include "slimevr_pose_sender.h"
#include "slimevr_protocol.h"
#include "slimevr_udp_client.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QRandomGenerator>
#include <QTextStream>
#include <QTimer>

#include <array>

namespace {

QByteArray randomDeviceId()
{
    QByteArray id;
    id.resize(6);
    do {
        for (int i = 0; i < 6; ++i) {
            id[i] = char(QRandomGenerator::global()->bounded(256));
        }
    } while (id == QByteArray(6, char(0)));
    return id;
}

QString stateText(SlimeVrConnectionState state)
{
    switch (state) {
    case SlimeVrConnectionState::Disabled: return QStringLiteral("disabled");
    case SlimeVrConnectionState::Discovering: return QStringLiteral("discovering");
    case SlimeVrConnectionState::Handshaking: return QStringLiteral("handshaking");
    case SlimeVrConnectionState::Connected: return QStringLiteral("connected");
    case SlimeVrConnectionState::Backoff: return QStringLiteral("backoff");
    case SlimeVrConnectionState::Error: return QStringLiteral("error");
    }
    return QStringLiteral("unknown");
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("slimevr_bridge_probe"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("六 IMU SlimeVR 桥接无界面探针"));
    parser.addHelpOption();
    const QCommandLineOption hostOption(
        QStringLiteral("host"), QStringLiteral("固定 Server IPv4（省略则广播发现）"), QStringLiteral("ip"));
    const QCommandLineOption portOption(
        QStringLiteral("port"), QStringLiteral("Server UDP 端口"), QStringLiteral("port"), QStringLiteral("6969"));
    const QCommandLineOption sideOption(
        QStringLiteral("side"), QStringLiteral("手侧 left|right"), QStringLiteral("side"), QStringLiteral("left"));
    const QCommandLineOption rateOption(
        QStringLiteral("rate"), QStringLiteral("姿态发送率 50–100"), QStringLiteral("hz"), QStringLiteral("75"));
    const QCommandLineOption serialOption(
        QStringLiteral("serial"), QStringLiteral("真实串口名（省略则使用演示数据）"), QStringLiteral("name"));
    const QCommandLineOption quitOption(
        QStringLiteral("quit-after-ms"), QStringLiteral("运行毫秒数后退出（0 表示不退出）"), QStringLiteral("ms"), QStringLiteral("0"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(sideOption);
    parser.addOption(rateOption);
    parser.addOption(serialOption);
    parser.addOption(quitOption);
    parser.process(application);

    bool converted = false;
    const int port = parser.value(portOption).toInt(&converted);
    if (!converted || port <= 0 || port > 65535) {
        QTextStream(stderr) << "error: invalid port\n";
        return 2;
    }
    const int rate = parser.value(rateOption).toInt(&converted);
    if (!converted || rate < 50 || rate > 100) {
        QTextStream(stderr) << "error: rate must be 50..100\n";
        return 2;
    }
    const qint64 quitMs = parser.value(quitOption).toLongLong(&converted);
    if (!converted || quitMs < 0) {
        QTextStream(stderr) << "error: invalid quit-after-ms\n";
        return 2;
    }
    const QString sideText = parser.value(sideOption).trimmed().toLower();
    if (sideText != QStringLiteral("left") && sideText != QStringLiteral("right")) {
        QTextStream(stderr) << "error: side must be left or right\n";
        return 2;
    }

    SlimeVrSettings settings;
    settings.enabled = true;
    settings.port = quint16(port);
    settings.sendRateHz = rate;
    settings.gloveSide = sideText == QStringLiteral("right") ? GloveSide::Right : GloveSide::Left;
    settings.deviceId = randomDeviceId();
    settings.handshakeIntervalMs = 1000;
    settings.connectionTimeoutMs = 3000;
    if (parser.isSet(hostOption)) {
        settings.discoveryMode = SlimeVrDiscoveryMode::FixedHost;
        settings.host = parser.value(hostOption).trimmed();
    }
    QString error;
    if (!validateSlimeVrSettings(settings, &error)) {
        QTextStream(stderr) << "error: " << error << '\n';
        return 2;
    }

    SlimeVrProtocol::HandshakeIdentity identity;
    identity.trackerType = settings.gloveSide == GloveSide::Right
        ? SlimeVrProtocol::IdentityDefaults::TrackerTypeGloveRight
        : SlimeVrProtocol::IdentityDefaults::TrackerTypeGloveLeft;
    for (int i = 0; i < 6; ++i) {
        identity.deviceId[size_t(i)] = quint8(settings.deviceId.at(i));
    }

    SlimeVrUdpClient client;
    client.setIdentity(identity);

    FrameStreamParser parserObject;
    SequenceGrouper grouper(SixImuProtocol::DefaultPendingGroupLimit);
    SixImuSolver solver;
    SerialDataSource serialSource;
    DemoDataSource demoSource;

    SlimeVrPoseSender sender(&client);
    sender.setGloveSide(settings.gloveSide);
    sender.setSendRateHz(settings.sendRateHz);
    sender.setMaxAgeMs(500);
    sender.start();

    QObject::connect(&serialSource, &SerialDataSource::bytesReady,
                     &parserObject, &FrameStreamParser::appendBytes);
    QObject::connect(&demoSource, &DemoDataSource::bytesReady,
                     &parserObject, &FrameStreamParser::appendBytes);
    QObject::connect(&parserObject, &FrameStreamParser::frameParsed,
                     &grouper, &SequenceGrouper::addFrame);
    QObject::connect(&grouper, &SequenceGrouper::completeGroupReady,
                     &solver, &SixImuSolver::processCompleteGroup);
    QObject::connect(&solver, &SixImuSolver::snapshotReady,
                     &sender, &SlimeVrPoseSender::submitSnapshot);

    QObject::connect(&client, &SlimeVrUdpClient::protocolError,
                     [](const QString &message) {
                         QTextStream(stderr) << "protocol error: " << message << '\n';
                     });

    if (parser.isSet(serialOption)) {
        serialSource.openPort(parser.value(serialOption));
        QEventLoop waitOpen;
        QTimer openTimeout;
        openTimeout.setSingleShot(true);
        openTimeout.setInterval(3000);
        QObject::connect(&serialSource, &SerialDataSource::stateChanged, &waitOpen,
                         [&waitOpen](SourceState state, const QString &) {
                             if (state == SourceState::Open || state == SourceState::Error) {
                                 waitOpen.quit();
                             }
                         });
        QObject::connect(&openTimeout, &QTimer::timeout, &waitOpen, &QEventLoop::quit);
        openTimeout.start();
        waitOpen.exec();
        if (!serialSource.isOpen()) {
            QTextStream(stderr) << "error: cannot open serial port\n";
            return 2;
        }
        QTextStream(stdout) << "serial source: " << parser.value(serialOption) << '\n';
    } else {
        demoSource.start();
        QTextStream(stdout) << "serial source: demo (non-hardware data)\n";
    }

    client.applySettings(settings);

    QTimer statusTimer;
    QObject::connect(&statusTimer, &QTimer::timeout, [&] {
        const auto network = client.statistics();
        const auto sending = sender.statistics();
        quint64 rotationSent = 0;
        quint64 rotationSkipped = 0;
        for (int index = 0; index < 6; ++index) {
            rotationSent += sending.rotationSent[size_t(index)];
            rotationSkipped += sending.rotationSkipped[size_t(index)];
        }
        QTextStream(stdout)
            << QStringLiteral("state=%1 sent=%2 received=%3 sensorInfo=%4 rotations=%5 skipped=%6 errors=%7 reconnects=%8\n")
                   .arg(stateText(client.state()))
                   .arg(network.datagramsSent)
                   .arg(network.datagramsReceived)
                   .arg(sending.sensorInfoSent)
                   .arg(rotationSent)
                   .arg(rotationSkipped)
                   .arg(network.sendErrors)
                   .arg(network.reconnects);
    });
    statusTimer.start(1000);

    if (quitMs > 0) {
        QTimer::singleShot(int(quitMs), &application, &QCoreApplication::quit);
    }

    const int exitCode = application.exec();
    statusTimer.stop();
    sender.stop();
    client.stop();
    demoSource.stop();
    serialSource.closePort();
    return exitCode;
}
