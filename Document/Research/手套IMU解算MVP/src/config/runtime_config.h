#pragma once

#include "core/diagnostic.h"
#include "core/sensor_id.h"

#include <QByteArray>
#include <QQuaternion>
#include <QString>
#include <QVector>

#include <array>

namespace handstudio {

struct SensorRuntimeConfig {
    SensorId sensorId = SensorId::Wrist;
    QQuaternion mountOrientation;
    double accelerometerRangeG = 16.0;
    double gyroscopeRangeDps = 2000.0;
};

struct RuntimeConfig {
    int schemaVersion = 0;
    QString skeletonId;
    std::array<SensorRuntimeConfig, 6> sensors{};
};

struct RuntimeConfigResult {
    bool success = false;
    RuntimeConfig config;
    QVector<Diagnostic> diagnostics;
};

RuntimeConfigResult loadRuntimeConfig(const QByteArray &json);
RuntimeConfigResult loadRuntimeConfigFile(const QString &filePath);

}
