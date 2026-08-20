#include "calibration_store.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace handstudio {
namespace {

void addError(QVector<Diagnostic> &diagnostics, QString code, QString message, QString detail = {})
{
    diagnostics.append({DiagnosticSeverity::Error, std::move(code), std::move(message), std::move(detail), 0});
}

bool finiteNumber(const QJsonValue &value, double &number)
{
    if (!value.isDouble()) {
        return false;
    }
    number = value.toDouble();
    return std::isfinite(number);
}

QJsonArray vectorToJson(const QVector3D &value)
{
    return QJsonArray{double(value.x()), double(value.y()), double(value.z())};
}

bool parseVector(const QJsonValue &value, QVector3D &vector)
{
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray array = value.toArray();
    if (array.size() != 3) {
        return false;
    }
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!finiteNumber(array.at(0), x) || !finiteNumber(array.at(1), y) || !finiteNumber(array.at(2), z)) {
        return false;
    }
    vector = QVector3D(float(x), float(y), float(z));
    return true;
}

QJsonArray matrixToJson(const std::array<std::array<int, 3>, 3> &matrix)
{
    QJsonArray rows;
    for (int row = 0; row < 3; ++row) {
        QJsonArray cols;
        for (int col = 0; col < 3; ++col) {
            cols.append(matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)]);
        }
        rows.append(cols);
    }
    return rows;
}

bool parseIntMatrix(const QJsonValue &value, std::array<std::array<int, 3>, 3> &matrix)
{
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray rows = value.toArray();
    if (rows.size() != 3) {
        return false;
    }
    for (int row = 0; row < 3; ++row) {
        if (!rows.at(row).isArray()) {
            return false;
        }
        const QJsonArray cols = rows.at(row).toArray();
        if (cols.size() != 3) {
            return false;
        }
        for (int col = 0; col < 3; ++col) {
            const QJsonValue entry = cols.at(col);
            if (!entry.isDouble()) {
                return false;
            }
            const double number = entry.toDouble();
            if (!std::isfinite(number) || std::floor(number) != number) {
                return false;
            }
            matrix[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] = int(number);
        }
    }
    return true;
}

QJsonArray softIronToJson(const std::array<double, 9> &matrix)
{
    QJsonArray rows;
    for (int row = 0; row < 3; ++row) {
        QJsonArray cols;
        for (int col = 0; col < 3; ++col) {
            cols.append(matrix[static_cast<std::size_t>(row * 3 + col)]);
        }
        rows.append(cols);
    }
    return rows;
}

bool parseSoftIron(const QJsonValue &value, std::array<double, 9> &matrix)
{
    if (!value.isArray()) {
        return false;
    }
    const QJsonArray rows = value.toArray();
    if (rows.size() != 3) {
        return false;
    }
    for (int row = 0; row < 3; ++row) {
        if (!rows.at(row).isArray()) {
            return false;
        }
        const QJsonArray cols = rows.at(row).toArray();
        if (cols.size() != 3) {
            return false;
        }
        for (int col = 0; col < 3; ++col) {
            double number = 0.0;
            if (!finiteNumber(cols.at(col), number)) {
                return false;
            }
            matrix[static_cast<std::size_t>(row * 3 + col)] = number;
        }
    }
    return true;
}

QJsonObject sensorToJson(const SensorCalibrationParams &params)
{
    QJsonObject object;
    object.insert(QStringLiteral("address"), int(sensorAddress(params.sensorId)));
    object.insert(QStringLiteral("accelerometerRangeG"), params.accelerometerRangeG);
    object.insert(QStringLiteral("gyroscopeRangeDps"), params.gyroscopeRangeDps);
    object.insert(QStringLiteral("magnetometerGainMicroTeslaPerLsb"), params.magnetometerGainMicroTeslaPerLsb);
    object.insert(QStringLiteral("accelerometerAxes"), matrixToJson(params.accelerometerAxes.matrix));
    object.insert(QStringLiteral("gyroscopeAxes"), matrixToJson(params.gyroscopeAxes.matrix));
    object.insert(QStringLiteral("magnetometerAxes"), matrixToJson(params.magnetometerAxes.matrix));
    object.insert(QStringLiteral("gyroBiasRadPerSec"), vectorToJson(params.gyroBiasRadPerSec));
    object.insert(QStringLiteral("magnetometerHardIronMicroTesla"), vectorToJson(params.magnetometerHardIronMicroTesla));
    object.insert(QStringLiteral("magnetometerSoftIron"), softIronToJson(params.magnetometerSoftIron));
    object.insert(QStringLiteral("gyroBiasValid"), params.gyroBiasValid);
    object.insert(QStringLiteral("magnetometerCalibrated"), params.magnetometerCalibrated);
    object.insert(QStringLiteral("magnetometerEnabled"), params.magnetometerEnabled);
    return object;
}

bool parseSensor(const QJsonValue &value, SensorCalibrationParams &params)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    double address = 0.0;
    if (!finiteNumber(object.value(QStringLiteral("address")), address) || std::floor(address) != address
        || address < 0.0 || address > 255.0) {
        return false;
    }
    const auto sensorId = sensorIdFromAddress(static_cast<quint8>(address));
    if (!sensorId) {
        return false;
    }
    params = SensorCalibrationParams::defaults(*sensorId);

    if (!finiteNumber(object.value(QStringLiteral("accelerometerRangeG")), params.accelerometerRangeG)
        || !finiteNumber(object.value(QStringLiteral("gyroscopeRangeDps")), params.gyroscopeRangeDps)
        || !finiteNumber(object.value(QStringLiteral("magnetometerGainMicroTeslaPerLsb")),
                         params.magnetometerGainMicroTeslaPerLsb)) {
        return false;
    }
    if (!parseIntMatrix(object.value(QStringLiteral("accelerometerAxes")), params.accelerometerAxes.matrix)
        || !parseIntMatrix(object.value(QStringLiteral("gyroscopeAxes")), params.gyroscopeAxes.matrix)
        || !parseIntMatrix(object.value(QStringLiteral("magnetometerAxes")), params.magnetometerAxes.matrix)
        || !parseVector(object.value(QStringLiteral("gyroBiasRadPerSec")), params.gyroBiasRadPerSec)
        || !parseVector(object.value(QStringLiteral("magnetometerHardIronMicroTesla")),
                        params.magnetometerHardIronMicroTesla)
        || !parseSoftIron(object.value(QStringLiteral("magnetometerSoftIron")), params.magnetometerSoftIron)) {
        return false;
    }
    if (!object.value(QStringLiteral("gyroBiasValid")).isBool()
        || !object.value(QStringLiteral("magnetometerCalibrated")).isBool()
        || !object.value(QStringLiteral("magnetometerEnabled")).isBool()) {
        return false;
    }
    params.gyroBiasValid = object.value(QStringLiteral("gyroBiasValid")).toBool();
    params.magnetometerCalibrated = object.value(QStringLiteral("magnetometerCalibrated")).toBool();
    params.magnetometerEnabled = object.value(QStringLiteral("magnetometerEnabled")).toBool();

    QString reason;
    if (!params.isValid(&reason)) {
        return false;
    }
    return true;
}

}

CalibrationDocument CalibrationDocument::defaults(const QString &deviceId)
{
    CalibrationDocument document;
    document.deviceId = deviceId;
    for (std::size_t index = 0; index < AllSensorIds.size(); ++index) {
        document.sensors[index] = SensorCalibrationParams::defaults(AllSensorIds[index]);
    }
    return document;
}

QByteArray saveCalibration(const CalibrationDocument &document, QVector<Diagnostic> *diagnostics)
{
    QVector<Diagnostic> local;
    QVector<Diagnostic> &out = diagnostics ? *diagnostics : local;

    if (document.schemaVersion != CalibrationSchemaVersion) {
        addError(out, QStringLiteral("calibration.schema.unsupported"),
                 QStringLiteral("不支持的校准 schemaVersion"), QString::number(document.schemaVersion));
        return {};
    }
    if (document.deviceId.trimmed().isEmpty()) {
        addError(out, QStringLiteral("calibration.deviceId.invalid"), QStringLiteral("缺少非空设备身份"));
        return {};
    }

    QJsonArray sensors;
    for (const SensorCalibrationParams &params : document.sensors) {
        QString reason;
        if (!params.isValid(&reason)) {
            addError(out, QStringLiteral("calibration.sensor.invalid"),
                     QStringLiteral("传感器校准参数非法"), reason);
            return {};
        }
        sensors.append(sensorToJson(params));
    }
    if (sensors.size() != static_cast<qsizetype>(AllSensorIds.size())) {
        addError(out, QStringLiteral("calibration.sensors.count"), QStringLiteral("sensors 必须包含六个传感器"));
        return {};
    }

    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), document.schemaVersion);
    root.insert(QStringLiteral("deviceId"), document.deviceId);
    root.insert(QStringLiteral("sensors"), sensors);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

CalibrationLoadResult loadCalibration(const QByteArray &json)
{
    CalibrationLoadResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result.diagnostics, QStringLiteral("calibration.json.invalid"),
                 QStringLiteral("校准配置不是有效 JSON 对象"), parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    double schemaNumber = 0.0;
    if (!finiteNumber(root.value(QStringLiteral("schemaVersion")), schemaNumber)
        || std::floor(schemaNumber) != schemaNumber
        || int(schemaNumber) != CalibrationSchemaVersion) {
        addError(result.diagnostics, QStringLiteral("calibration.schema.unsupported"),
                 QStringLiteral("不支持的校准 schemaVersion"), QString::number(schemaNumber));
        return result;
    }
    result.document.schemaVersion = int(schemaNumber);

    if (!root.value(QStringLiteral("deviceId")).isString()
        || root.value(QStringLiteral("deviceId")).toString().trimmed().isEmpty()) {
        addError(result.diagnostics, QStringLiteral("calibration.deviceId.invalid"), QStringLiteral("缺少非空设备身份"));
        return result;
    }
    result.document.deviceId = root.value(QStringLiteral("deviceId")).toString();

    if (!root.value(QStringLiteral("sensors")).isArray()) {
        addError(result.diagnostics, QStringLiteral("calibration.sensors.missing"), QStringLiteral("缺少 sensors 数组"));
        return result;
    }
    const QJsonArray sensors = root.value(QStringLiteral("sensors")).toArray();
    if (sensors.size() != static_cast<qsizetype>(AllSensorIds.size())) {
        addError(result.diagnostics, QStringLiteral("calibration.sensors.count"),
                 QStringLiteral("sensors 必须包含六个传感器"), QString::number(sensors.size()));
        return result;
    }

    quint8 seenMask = 0;
    for (const QJsonValue &sensorValue : sensors) {
        SensorCalibrationParams params;
        if (!parseSensor(sensorValue, params)) {
            addError(result.diagnostics, QStringLiteral("calibration.sensor.invalid"),
                     QStringLiteral("传感器校准参数非法或不可解析"));
            return result;
        }
        const auto index = sensorIndex(params.sensorId);
        const quint8 bit = static_cast<quint8>(1U << index.value());
        if ((seenMask & bit) != 0) {
            addError(result.diagnostics, QStringLiteral("calibration.sensor.duplicate"),
                     QStringLiteral("传感器 address 重复"), QString::number(sensorAddress(params.sensorId)));
            return result;
        }
        result.document.sensors[static_cast<std::size_t>(index.value())] = params;
        seenMask = static_cast<quint8>(seenMask | bit);
    }

    result.success = seenMask == 0x3F;
    return result;
}

CalibrationLoadResult loadCalibrationFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        CalibrationLoadResult result;
        addError(result.diagnostics, QStringLiteral("calibration.file.open"),
                 QStringLiteral("无法读取校准配置文件"), file.errorString());
        return result;
    }
    return loadCalibration(file.readAll());
}

}
