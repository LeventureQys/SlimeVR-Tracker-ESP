#include "frame_stream_parser.h"
#include "sequence_grouper.h"
#include "serial_data_source.h"
#include "six_imu_solver.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTextStream>
#include <QTimer>

namespace {

QString sourceStateName(SourceState state)
{
    switch (state) {
    case SourceState::Closed: return QStringLiteral("Closed");
    case SourceState::Opening: return QStringLiteral("Opening");
    case SourceState::Open: return QStringLiteral("Open");
    case SourceState::Closing: return QStringLiteral("Closing");
    case SourceState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("?");
}

QString fusionModeName(FusionMode mode)
{
    switch (mode) {
    case FusionMode::Invalid: return QStringLiteral("Invalid");
    case FusionMode::SixAxis: return QStringLiteral("SixAxis");
    case FusionMode::NineAxis: return QStringLiteral("NineAxis");
    }
    return QStringLiteral("?");
}

QString rate(quint64 count, double seconds)
{
    if (seconds <= 0.0) return QStringLiteral("0.0");
    return QString::number(static_cast<double>(count) / seconds, 'f', 1);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("Headless six-IMU serial hardware probe"));
    cli.addHelpOption();
    QCommandLineOption portOpt(QStringLiteral("port"), QStringLiteral("serial port name"), QStringLiteral("name"), QStringLiteral("COM12"));
    QCommandLineOption durationOpt(QStringLiteral("duration"), QStringLiteral("run duration in seconds"), QStringLiteral("seconds"), QStringLiteral("60"));
    QCommandLineOption baudOpt(QStringLiteral("baud"), QStringLiteral("baud rate"), QStringLiteral("baud"), QStringLiteral("921600"));
    cli.addOption(portOpt);
    cli.addOption(durationOpt);
    cli.addOption(baudOpt);
    cli.process(app);

    const QString portName = cli.value(portOpt);
    bool okDuration = false;
    const int durationSec = cli.value(durationOpt).toInt(&okDuration);
    bool okBaud = false;
    const qint32 baud = cli.value(baudOpt).toInt(&okBaud);

    QTextStream out(stdout);
    QTextStream err(stderr);

    if (!okDuration || durationSec <= 0 || !okBaud || baud <= 0) {
        err << QStringLiteral("invalid --duration or --baud") << Qt::endl;
        return 2;
    }

    SerialDataSource serial;
    FrameStreamParser parser;
    SequenceGrouper grouper(SixImuProtocol::DefaultPendingGroupLimit);
    SixImuSolver solver;

    QObject::connect(&serial, &SerialDataSource::bytesReady,
                     &parser, &FrameStreamParser::appendBytes);
    QObject::connect(&parser, &FrameStreamParser::frameParsed,
                     &grouper, &SequenceGrouper::addFrame);
    QObject::connect(&grouper, &SequenceGrouper::completeGroupReady,
                     &solver, &SixImuSolver::processCompleteGroup);

    QElapsedTimer wallClock;
    wallClock.start();

    auto dumpStats = [&](const QString &phase) {
        const ParserStatistics ps = parser.statistics();
        const GroupStatistics gs = grouper.statistics();
        const double sec = wallClock.elapsed() / 1000.0;
        out << "[" << phase << "] t=" << QString::number(sec, 'f', 1) << "s"
            << " inputBytes=" << ps.inputBytes
            << " validFrames=" << ps.validFrames
            << " invalidLength=" << ps.invalidLength
            << " invalidCrc=" << ps.invalidCrc
            << " discarded=" << ps.discardedBytes
            << " unknownAddr=" << ps.unknownAddressFrames
            << " completeGroups=" << gs.completeGroups
            << " partialGroups=" << gs.partialGroups
            << " duplicateFrames=" << gs.duplicateFrames
            << " frameRate=" << rate(ps.validFrames, sec) << "/s"
            << " groupRate=" << rate(gs.completeGroups, sec) << "/s"
            << Qt::endl;
        out << "[" << phase << "] per-address valid frames:";
        for (int i = 0; i < SixImuProtocol::SensorCount; ++i) {
            out << " 0x" << QString::number(0x50 + i, 16) << "=" << gs.sensorFrameCounts[i];
        }
        out << Qt::endl;
    };

    QTimer statsTimer;
    QObject::connect(&statsTimer, &QTimer::timeout, [&]() { dumpStats(QStringLiteral("periodic")); });
    statsTimer.start(10000);

    QObject::connect(&serial, &SerialDataSource::stateChanged,
                     [&](SourceState state, const QString &message) {
                         out << "[state] " << sourceStateName(state) << " " << message << Qt::endl;
                     });
    QObject::connect(&serial, &SerialDataSource::errorOccurred,
                     [&](const QString &message) {
                         err << "[error] " << message << Qt::endl;
                     });

    QTimer::singleShot(durationSec * 1000, &app, [&]() {
        dumpStats(QStringLiteral("final"));
        const auto snap = solver.latestSnapshot();
        if (snap) {
            out << "[final] snapshot seq=0x" << QString::number(snap->sequence, 16) << Qt::endl;
            for (int i = 0; i < SixImuProtocol::SensorCount; ++i) {
                const SensorPose &p = snap->poses[i];
                out << "[final] 0x" << QString::number(0x50 + i, 16)
                    << " valid=" << (p.valid ? 1 : 0)
                    << " calibrated=" << (p.calibrated ? 1 : 0)
                    << " allZero=" << (p.sourceAllZero ? 1 : 0)
                    << " mode=" << fusionModeName(p.mode)
                    << " quatWXYZ=(" << QString::number(p.relativeOrientation.scalar(), 'f', 4)
                    << "," << QString::number(p.relativeOrientation.x(), 'f', 4)
                    << "," << QString::number(p.relativeOrientation.y(), 'f', 4)
                    << "," << QString::number(p.relativeOrientation.z(), 'f', 4) << ")"
                    << " eulerRPY=(" << QString::number(p.relativeEuler.rollDegrees, 'f', 2)
                    << "," << QString::number(p.relativeEuler.pitchDegrees, 'f', 2)
                    << "," << QString::number(p.relativeEuler.yawDegrees, 'f', 2) << ")"
                    << " status=" << p.status
                    << Qt::endl;
            }
        } else {
            out << "[final] no snapshot produced" << Qt::endl;
        }
        serial.closePort();
        app.quit();
    });

    out << "[start] opening " << portName << " @ " << baud << Qt::endl;
    serial.openPort(portName, baud);

    return app.exec();
}
