#pragma once

#include "core/diagnostic.h"
#include "core/schema_version.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

#include <QtTypes>

namespace handstudio {

struct SensorMountInfo {
    quint8 address = 0;
    QString name;
    QString position;
    QString orientation;
};

// Recording metadata (design doc section 10). Ranges/axes/orientation are only
// populated when actually measured or known from hardware documentation; they
// must never be filled with guessed values.
struct RecordingMetadata {
    int recordingSchema = RecordingSchemaVersion;
    QString applicationVersion;
    QString sessionId;
    QString startedUtc;
    QString stoppedUtc;

    QString devicePort;
    qint32 baudRate = 0;
    QString deviceDescription;
    QString deviceSerialNumber;
    QString firmwareVersion;

    double nominalSampleRateHz = 0.0;
    QJsonObject ranges;
    QJsonObject axes;
    QVector<SensorMountInfo> sensorMounts;

    QString operatorName;
    QString actionDescription;
    QString configVersion;

    qint64 rawBytes = 0;
    QString rawSha256;

    QJsonObject toJson() const;
    static RecordingMetadata fromJson(const QJsonObject &object);
};

struct MetadataIoResult {
    bool success = false;
    RecordingMetadata metadata;
    QVector<Diagnostic> diagnostics;
};

MetadataIoResult writeMetadataJson(const QString &filePath, const RecordingMetadata &metadata);
MetadataIoResult readMetadataJson(const QString &filePath);

QString computeSha256Hex(const QByteArray &bytes);
QString computeFileSha256Hex(const QString &filePath);

}
