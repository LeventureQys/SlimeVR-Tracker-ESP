#include "recording/recording_schema.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <utility>

namespace handstudio {
namespace {

void addError(MetadataIoResult &result, QString code, QString message, QString detail = {})
{
    result.diagnostics.append({DiagnosticSeverity::Error, std::move(code),
                               std::move(message), std::move(detail), 0});
}

}

QString computeSha256Hex(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QString computeFileSha256Hex(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QJsonObject RecordingMetadata::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("recordingSchema"), recordingSchema);
    root.insert(QStringLiteral("applicationVersion"), applicationVersion);
    root.insert(QStringLiteral("sessionId"), sessionId);
    root.insert(QStringLiteral("startedUtc"), startedUtc);
    root.insert(QStringLiteral("stoppedUtc"), stoppedUtc);
    root.insert(QStringLiteral("devicePort"), devicePort);
    root.insert(QStringLiteral("baudRate"), baudRate);
    root.insert(QStringLiteral("deviceDescription"), deviceDescription);
    root.insert(QStringLiteral("deviceSerialNumber"), deviceSerialNumber);
    root.insert(QStringLiteral("firmwareVersion"), firmwareVersion);
    root.insert(QStringLiteral("nominalSampleRateHz"), nominalSampleRateHz);
    root.insert(QStringLiteral("ranges"), ranges);
    root.insert(QStringLiteral("axes"), axes);

    QJsonArray mounts;
    for (const auto &mount : sensorMounts) {
        QJsonObject object;
        object.insert(QStringLiteral("address"), static_cast<int>(mount.address));
        object.insert(QStringLiteral("name"), mount.name);
        object.insert(QStringLiteral("position"), mount.position);
        object.insert(QStringLiteral("orientation"), mount.orientation);
        mounts.append(object);
    }
    root.insert(QStringLiteral("sensorMounts"), mounts);

    root.insert(QStringLiteral("operatorName"), operatorName);
    root.insert(QStringLiteral("actionDescription"), actionDescription);
    root.insert(QStringLiteral("configVersion"), configVersion);
    root.insert(QStringLiteral("rawBytes"), static_cast<double>(rawBytes));
    root.insert(QStringLiteral("rawSha256"), rawSha256);
    return root;
}

RecordingMetadata RecordingMetadata::fromJson(const QJsonObject &root)
{
    RecordingMetadata metadata;
    metadata.recordingSchema = root.value(QStringLiteral("recordingSchema")).toInt(RecordingSchemaVersion);
    metadata.applicationVersion = root.value(QStringLiteral("applicationVersion")).toString();
    metadata.sessionId = root.value(QStringLiteral("sessionId")).toString();
    metadata.startedUtc = root.value(QStringLiteral("startedUtc")).toString();
    metadata.stoppedUtc = root.value(QStringLiteral("stoppedUtc")).toString();
    metadata.devicePort = root.value(QStringLiteral("devicePort")).toString();
    metadata.baudRate = root.value(QStringLiteral("baudRate")).toInt();
    metadata.deviceDescription = root.value(QStringLiteral("deviceDescription")).toString();
    metadata.deviceSerialNumber = root.value(QStringLiteral("deviceSerialNumber")).toString();
    metadata.firmwareVersion = root.value(QStringLiteral("firmwareVersion")).toString();
    metadata.nominalSampleRateHz = root.value(QStringLiteral("nominalSampleRateHz")).toDouble();
    metadata.ranges = root.value(QStringLiteral("ranges")).toObject();
    metadata.axes = root.value(QStringLiteral("axes")).toObject();

    const QJsonArray mounts = root.value(QStringLiteral("sensorMounts")).toArray();
    metadata.sensorMounts.reserve(mounts.size());
    for (const QJsonValue &value : mounts) {
        const QJsonObject object = value.toObject();
        SensorMountInfo mount;
        mount.address = static_cast<quint8>(object.value(QStringLiteral("address")).toInt());
        mount.name = object.value(QStringLiteral("name")).toString();
        mount.position = object.value(QStringLiteral("position")).toString();
        mount.orientation = object.value(QStringLiteral("orientation")).toString();
        metadata.sensorMounts.append(mount);
    }

    metadata.operatorName = root.value(QStringLiteral("operatorName")).toString();
    metadata.actionDescription = root.value(QStringLiteral("actionDescription")).toString();
    metadata.configVersion = root.value(QStringLiteral("configVersion")).toString();
    metadata.rawBytes = static_cast<qint64>(root.value(QStringLiteral("rawBytes")).toDouble());
    metadata.rawSha256 = root.value(QStringLiteral("rawSha256")).toString();
    return metadata;
}

MetadataIoResult writeMetadataJson(const QString &filePath, const RecordingMetadata &metadata)
{
    MetadataIoResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        addError(result, QStringLiteral("recording.metadata.write"),
                 QStringLiteral("无法写入 metadata.json"), file.errorString());
        return result;
    }
    const QJsonDocument document(metadata.toJson());
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0) {
        addError(result, QStringLiteral("recording.metadata.write"),
                 QStringLiteral("写入 metadata.json 失败"), file.errorString());
        return result;
    }
    file.close();
    result.success = true;
    result.metadata = metadata;
    return result;
}

MetadataIoResult readMetadataJson(const QString &filePath)
{
    MetadataIoResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        addError(result, QStringLiteral("recording.metadata.read"),
                 QStringLiteral("无法读取 metadata.json"), file.errorString());
        return result;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result, QStringLiteral("recording.metadata.json"),
                 QStringLiteral("metadata.json 不是有效 JSON 对象"), parseError.errorString());
        return result;
    }
    result.success = true;
    result.metadata = RecordingMetadata::fromJson(document.object());
    return result;
}

}
