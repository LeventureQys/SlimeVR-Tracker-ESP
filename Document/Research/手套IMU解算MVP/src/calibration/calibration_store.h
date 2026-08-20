#pragma once

#include "calibration_parameters.h"

#include "core/diagnostic.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <array>

namespace handstudio {

inline constexpr int CalibrationSchemaVersion = 1;

// Whole-device calibration document. Persistence binds a non-empty device
// identity and a schema version to all six per-sensor parameter sets.
struct CalibrationDocument {
    int schemaVersion = CalibrationSchemaVersion;
    QString deviceId;
    std::array<SensorCalibrationParams, 6> sensors{};

    static CalibrationDocument defaults(const QString &deviceId);
};

struct CalibrationLoadResult {
    bool success = false;
    CalibrationDocument document;
    QVector<Diagnostic> diagnostics;
};

CalibrationLoadResult loadCalibration(const QByteArray &json);
CalibrationLoadResult loadCalibrationFile(const QString &filePath);
QByteArray saveCalibration(const CalibrationDocument &document, QVector<Diagnostic> *diagnostics = nullptr);

}
