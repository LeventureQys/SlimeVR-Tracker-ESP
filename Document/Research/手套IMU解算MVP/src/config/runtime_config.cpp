#include "runtime_config.h"

#include "core/schema_version.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace handstudio {
namespace {

void addError(RuntimeConfigResult &result, QString code, QString message, QString detail = {})
{
    result.diagnostics.append({DiagnosticSeverity::Error, std::move(code), std::move(message), std::move(detail), 0});
}

bool finiteNumber(const QJsonValue &value, double &number)
{
    if (!value.isDouble()) {
        return false;
    }
    number = value.toDouble();
    return std::isfinite(number);
}

bool parseQuaternion(const QJsonValue &value, QQuaternion &quaternion)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    double w = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!finiteNumber(object.value(QStringLiteral("w")), w)
        || !finiteNumber(object.value(QStringLiteral("x")), x)
        || !finiteNumber(object.value(QStringLiteral("y")), y)
        || !finiteNumber(object.value(QStringLiteral("z")), z)) {
        return false;
    }
    quaternion = QQuaternion(static_cast<float>(w), static_cast<float>(x),
                             static_cast<float>(y), static_cast<float>(z));
    return std::abs(quaternion.length() - 1.0F) <= 1.0e-4F;
}

}

RuntimeConfigResult loadRuntimeConfig(const QByteArray &json)
{
    RuntimeConfigResult result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        addError(result, QStringLiteral("config.json.invalid"), QStringLiteral("运行时配置不是有效 JSON 对象"), parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("schemaVersion")) || !root.value(QStringLiteral("schemaVersion")).isDouble()) {
        addError(result, QStringLiteral("config.schema.missing"), QStringLiteral("缺少整数 schemaVersion"));
        return result;
    }
    const double schemaNumber = root.value(QStringLiteral("schemaVersion")).toDouble();
    if (!std::isfinite(schemaNumber) || std::floor(schemaNumber) != schemaNumber
        || static_cast<int>(schemaNumber) != RuntimeConfigSchemaVersion) {
        addError(result, QStringLiteral("config.schema.unsupported"), QStringLiteral("不支持的运行时配置 schemaVersion"), QString::number(schemaNumber));
        return result;
    }
    result.config.schemaVersion = static_cast<int>(schemaNumber);

    if (!root.value(QStringLiteral("skeletonId")).isString()
        || root.value(QStringLiteral("skeletonId")).toString().trimmed().isEmpty()) {
        addError(result, QStringLiteral("config.skeletonId.invalid"), QStringLiteral("缺少非空 skeletonId"));
        return result;
    }
    result.config.skeletonId = root.value(QStringLiteral("skeletonId")).toString();

    if (!root.value(QStringLiteral("sensors")).isArray()) {
        addError(result, QStringLiteral("config.sensors.missing"), QStringLiteral("缺少 sensors 数组"));
        return result;
    }
    const QJsonArray sensors = root.value(QStringLiteral("sensors")).toArray();
    if (sensors.size() != static_cast<qsizetype>(AllSensorIds.size())) {
        addError(result, QStringLiteral("config.sensors.count"), QStringLiteral("sensors 必须包含六个传感器"), QString::number(sensors.size()));
        return result;
    }

    quint8 seenMask = 0;
    for (const QJsonValue &sensorValue : sensors) {
        if (!sensorValue.isObject()) {
            addError(result, QStringLiteral("config.sensor.invalid"), QStringLiteral("传感器配置必须是对象"));
            return result;
        }
        const QJsonObject sensorObject = sensorValue.toObject();
        double addressNumber = 0.0;
        if (!finiteNumber(sensorObject.value(QStringLiteral("address")), addressNumber)
            || std::floor(addressNumber) != addressNumber) {
            addError(result, QStringLiteral("config.sensor.address"), QStringLiteral("传感器 address 必须是有限整数"));
            return result;
        }
        const auto sensorId = sensorIdFromAddress(static_cast<quint8>(addressNumber));
        if (!sensorId || addressNumber < 0.0 || addressNumber > 255.0) {
            addError(result, QStringLiteral("config.sensor.address"), QStringLiteral("传感器 address 不在 0x50..0x55"), QString::number(addressNumber));
            return result;
        }
        const int index = sensorIndex(*sensorId).value();
        const quint8 bit = static_cast<quint8>(1U << index);
        if ((seenMask & bit) != 0) {
            addError(result, QStringLiteral("config.sensor.duplicate"), QStringLiteral("传感器 address 重复"), QString::number(addressNumber));
            return result;
        }

        SensorRuntimeConfig sensorConfig;
        sensorConfig.sensorId = *sensorId;
        if (!parseQuaternion(sensorObject.value(QStringLiteral("mountQuaternion")), sensorConfig.mountOrientation)) {
            addError(result, QStringLiteral("config.sensor.mountQuaternion"), QStringLiteral("mountQuaternion 必须为有限单位四元数 wxyz"), QString::number(addressNumber));
            return result;
        }
        if (!finiteNumber(sensorObject.value(QStringLiteral("accelerometerRangeG")), sensorConfig.accelerometerRangeG)
            || sensorConfig.accelerometerRangeG <= 0.0
            || !finiteNumber(sensorObject.value(QStringLiteral("gyroscopeRangeDps")), sensorConfig.gyroscopeRangeDps)
            || sensorConfig.gyroscopeRangeDps <= 0.0) {
            addError(result, QStringLiteral("config.sensor.range"), QStringLiteral("量程必须为有限正数"), QString::number(addressNumber));
            return result;
        }
        result.config.sensors[static_cast<std::size_t>(index)] = sensorConfig;
        seenMask = static_cast<quint8>(seenMask | bit);
    }

    result.success = seenMask == 0x3F;
    return result;
}

RuntimeConfigResult loadRuntimeConfigFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        RuntimeConfigResult result;
        addError(result, QStringLiteral("config.file.open"), QStringLiteral("无法读取运行时配置文件"), file.errorString());
        return result;
    }
    return loadRuntimeConfig(file.readAll());
}

}
