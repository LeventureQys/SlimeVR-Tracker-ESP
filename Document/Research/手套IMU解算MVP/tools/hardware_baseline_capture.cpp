#include "protocol/frame_stream_parser.h"
#include "protocol/sequence_grouper.h"
#include "recording/recording_schema.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTextStream>
#include <QTimer>

#include <cstdio>

using namespace handstudio;

namespace {

struct Options {
    QString port;
    int durationSeconds = 5;
    QString datasetId;
    QString outBase = QStringLiteral("testdata/hardware");
    QString action = QStringLiteral("六路 IMU 基线采集");
    QString configVersion = QStringLiteral("2.0.0-substage2-baseline");
};

void printUsage()
{
    QTextStream out(stdout);
    out << "usage: hardware_baseline_capture [--port COMx] [--duration 5]\n"
        << "       [--dataset-id id] [--out dir] [--action text]\n";
}

bool parseArgs(const QStringList &args, Options &options)
{
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        auto nextValue = [&](QString &out) {
            if (i + 1 >= args.size()) {
                return false;
            }
            out = args.at(++i);
            return true;
        };
        if (arg == QStringLiteral("--port")) {
            if (!nextValue(options.port)) return false;
        } else if (arg == QStringLiteral("--duration")) {
            QString value;
            if (!nextValue(value)) return false;
            bool ok = false;
            options.durationSeconds = value.toInt(&ok);
            if (!ok || options.durationSeconds <= 0) return false;
        } else if (arg == QStringLiteral("--dataset-id")) {
            if (!nextValue(options.datasetId)) return false;
        } else if (arg == QStringLiteral("--out")) {
            if (!nextValue(options.outBase)) return false;
        } else if (arg == QStringLiteral("--action")) {
            if (!nextValue(options.action)) return false;
        } else {
            return false;
        }
    }
    return true;
}

QString uniqueDatasetId()
{
    return QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmss"));
}

void writeBlocked(const QString &reason)
{
    QTextStream out(stdout);
    out << "BLOCKED: " << reason << '\n';
    out.flush();
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("hardware_baseline_capture"));

    Options options;
    if (!parseArgs(app.arguments(), options)) {
        printUsage();
        return 1;
    }

    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    QTextStream out(stdout);
    out << "枚举到的串口 (" << ports.size() << "):\n";
    for (const QSerialPortInfo &info : ports) {
        out << "  " << info.portName()
            << " | " << info.description()
            << " | " << info.manufacturer()
            << " | " << info.serialNumber() << '\n';
    }
    out.flush();

    if (ports.isEmpty() && options.port.isEmpty()) {
        writeBlocked(QStringLiteral("没有可用串口设备"));
        return 2;
    }

    QString portName = options.port;
    QString deviceDescription;
    QString deviceManufacturer;
    QString deviceSerial;
    if (portName.isEmpty()) {
        // Prefer a USB/VCP port; fall back to the first available port.
        const QSerialPortInfo *candidate = nullptr;
        for (const QSerialPortInfo &info : ports) {
            if (info.description().contains(QStringLiteral("VCP"), Qt::CaseInsensitive)
                || info.manufacturer().contains(QStringLiteral("CH340"), Qt::CaseInsensitive)
                || info.manufacturer().contains(QStringLiteral("CP210"), Qt::CaseInsensitive)
                || info.manufacturer().contains(QStringLiteral("FTDI"), Qt::CaseInsensitive)) {
                candidate = &info;
                break;
            }
        }
        if (!candidate) {
            candidate = &ports.first();
        }
        portName = candidate->portName();
        deviceDescription = candidate->description();
        deviceManufacturer = candidate->manufacturer();
        deviceSerial = candidate->serialNumber();
    } else {
        for (const QSerialPortInfo &info : ports) {
            if (info.portName() == portName) {
                deviceDescription = info.description();
                deviceManufacturer = info.manufacturer();
                deviceSerial = info.serialNumber();
                break;
            }
        }
    }

    out << "尝试打开 " << portName << " @ 921600 8N1 ...\n";
    out.flush();

    QSerialPort serial;
    serial.setPortName(portName);
    serial.setBaudRate(921600);
    serial.setDataBits(QSerialPort::Data8);
    serial.setParity(QSerialPort::NoParity);
    serial.setStopBits(QSerialPort::OneStop);
    serial.setFlowControl(QSerialPort::NoFlowControl);
    if (!serial.open(QIODevice::ReadOnly)) {
        writeBlocked(QStringLiteral("无法打开串口 %1: %2").arg(portName, serial.errorString()));
        return 2;
    }

    QByteArray rawBytes;
    FrameStreamParser parser;
    SequenceGrouper grouper(DefaultPendingGroupLimit);
    QObject::connect(&parser, &FrameStreamParser::frameParsed,
                     &grouper, &SequenceGrouper::addFrame);

    QElapsedTimer elapsed;
    elapsed.start();
    QObject::connect(&serial, &QSerialPort::readyRead, [&]() {
        const QByteArray bytes = serial.readAll();
        if (!bytes.isEmpty()) {
            rawBytes.append(bytes);
            parser.appendBytes(bytes, elapsed.nsecsElapsed());
        }
    });

    QEventLoop loop;
    QTimer stopTimer;
    QObject::connect(&stopTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    stopTimer.start(options.durationSeconds * 1000);
    loop.exec();

    serial.close();

    const ParserStatistics parserStats = parser.statistics();
    const GroupStatistics groupStats = grouper.statistics();

    out << "采集结束: 输入字节=" << rawBytes.size()
        << " 有效帧=" << parserStats.validFrames
        << " CRC错误=" << parserStats.invalidCrc
        << " 未知地址=" << parserStats.unknownAddressFrames
        << " 完整组=" << groupStats.completeGroups << '\n';
    out.flush();

    if (parserStats.validFrames == 0 || rawBytes.isEmpty()) {
        writeBlocked(QStringLiteral("串口 %1 未产生任何有效 AA 55 帧（有效帧=%2，输入字节=%3）")
                         .arg(portName)
                         .arg(parserStats.validFrames)
                         .arg(rawBytes.size()));
        return 2;
    }

    if (options.datasetId.isEmpty()) {
        options.datasetId = uniqueDatasetId();
    }
    const QString datasetDir = options.outBase + QLatin1Char('/') + options.datasetId;
    if (!QDir().mkpath(datasetDir)) {
        writeBlocked(QStringLiteral("无法创建数据集目录 %1").arg(datasetDir));
        return 2;
    }

    QFile rawFile(datasetDir + QStringLiteral("/raw.bin"));
    if (!rawFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        writeBlocked(QStringLiteral("无法写入 raw.bin"));
        return 2;
    }
    rawFile.write(rawBytes);
    rawFile.close();

    RecordingMetadata metadata;
    metadata.recordingSchema = RecordingSchemaVersion;
    metadata.applicationVersion = ApplicationVersion.toString();
    metadata.sessionId = options.datasetId;
    metadata.startedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    metadata.devicePort = portName;
    metadata.baudRate = 921600;
    metadata.deviceDescription = deviceDescription;
    metadata.deviceSerialNumber = deviceSerial;
    metadata.firmwareVersion = QStringLiteral("unknown"); // not queried: never guessed
    const double measuredHz = options.durationSeconds > 0
                                  ? static_cast<double>(parserStats.validFrames)
                                        / static_cast<double>(options.durationSeconds)
                                  : 0.0;
    metadata.nominalSampleRateHz = measuredHz;
    metadata.operatorName = QStringLiteral("SubStage2");
    metadata.actionDescription = options.action;
    metadata.configVersion = options.configVersion;
    // Ranges/axes/mount orientation are NOT measured by this tool; leave empty
    // rather than guessing. Downstream precision conclusions stay blocked until
    // hardware documentation or measurement fills them in.
    for (int i = 0; i < ImuSensorCount; ++i) {
        SensorMountInfo mount;
        mount.address = static_cast<quint8>(0x50 + i);
        mount.name = sensorDisplayName(sensorIdFromIndex(i).value());
        mount.position = QStringLiteral("unknown");
        mount.orientation = QStringLiteral("unknown");
        metadata.sensorMounts.append(mount);
    }
    metadata.rawBytes = rawBytes.size();
    metadata.rawSha256 = computeFileSha256Hex(datasetDir + QStringLiteral("/raw.bin"));

    const auto writeResult = writeMetadataJson(datasetDir + QStringLiteral("/metadata.json"), metadata);
    if (!writeResult.success) {
        writeBlocked(QStringLiteral("无法写入 metadata.json"));
        return 2;
    }

    out << "OK: 数据集写入 " << datasetDir << '\n'
        << "  完整组=" << groupStats.completeGroups
        << " 采样率(实测)=" << measuredHz << " Hz\n"
        << "  注意: 量程/轴向/安装方向为 unknown，未猜测，相关后续结论保持阻塞\n";
    out.flush();
    return 0;
}
