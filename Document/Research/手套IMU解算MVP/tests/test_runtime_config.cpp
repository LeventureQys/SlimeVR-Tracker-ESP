#include <QtTest>

#include "config/runtime_config.h"
#include "core/schema_version.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

namespace {
QJsonObject validRoot()
{
    QJsonArray sensors;
    for (int address = 0x50; address <= 0x55; ++address) {
        sensors.append(QJsonObject{
            {QStringLiteral("address"), address},
            {QStringLiteral("mountQuaternion"), QJsonObject{
                {QStringLiteral("w"), 1.0}, {QStringLiteral("x"), 0.0},
                {QStringLiteral("y"), 0.0}, {QStringLiteral("z"), 0.0}}},
            {QStringLiteral("accelerometerRangeG"), 16.0},
            {QStringLiteral("gyroscopeRangeDps"), 2000.0}});
    }
    return QJsonObject{
        {QStringLiteral("schemaVersion"), handstudio::RuntimeConfigSchemaVersion},
        {QStringLiteral("skeletonId"), QStringLiteral("generic-left-v1")},
        {QStringLiteral("sensors"), sensors}};
}

handstudio::RuntimeConfigResult parse(const QJsonObject &root)
{
    return handstudio::loadRuntimeConfig(QJsonDocument(root).toJson(QJsonDocument::Compact));
}
}

class RuntimeConfigTest final : public QObject {
    Q_OBJECT

private slots:
    void acceptsValidConfig();
    void rejectsMissingField();
    void rejectsWrongSchema();
    void rejectsNonFiniteNumber();
    void rejectsNonUnitQuaternion();
};

void RuntimeConfigTest::acceptsValidConfig()
{
    const auto result = parse(validRoot());
    QVERIFY2(result.success, result.diagnostics.isEmpty() ? "unknown" : qPrintable(result.diagnostics.first().message));
    QCOMPARE(result.config.schemaVersion, handstudio::RuntimeConfigSchemaVersion);
    QCOMPARE(result.config.sensors[5].sensorId, handstudio::SensorId::Pinky);
}

void RuntimeConfigTest::rejectsMissingField()
{
    QJsonObject root = validRoot();
    root.remove(QStringLiteral("skeletonId"));
    const auto result = parse(root);
    QVERIFY(!result.success);
    QCOMPARE(result.diagnostics.first().code, QStringLiteral("config.skeletonId.invalid"));
}

void RuntimeConfigTest::rejectsWrongSchema()
{
    QJsonObject root = validRoot();
    root.insert(QStringLiteral("schemaVersion"), 99);
    const auto result = parse(root);
    QVERIFY(!result.success);
    QCOMPARE(result.diagnostics.first().code, QStringLiteral("config.schema.unsupported"));
}

void RuntimeConfigTest::rejectsNonFiniteNumber()
{
    QByteArray json = QJsonDocument(validRoot()).toJson(QJsonDocument::Compact);
    json.replace("\"accelerometerRangeG\":16", "\"accelerometerRangeG\":1e999");
    const auto result = handstudio::loadRuntimeConfig(json);
    QVERIFY(!result.success);
    QVERIFY(!result.diagnostics.isEmpty());
}

void RuntimeConfigTest::rejectsNonUnitQuaternion()
{
    QJsonObject root = validRoot();
    QJsonArray sensors = root.value(QStringLiteral("sensors")).toArray();
    QJsonObject sensor = sensors.first().toObject();
    sensor.insert(QStringLiteral("mountQuaternion"), QJsonObject{
        {QStringLiteral("w"), 2.0}, {QStringLiteral("x"), 0.0},
        {QStringLiteral("y"), 0.0}, {QStringLiteral("z"), 0.0}});
    sensors.replace(0, sensor);
    root.insert(QStringLiteral("sensors"), sensors);
    const auto result = parse(root);
    QVERIFY(!result.success);
    QCOMPARE(result.diagnostics.first().code, QStringLiteral("config.sensor.mountQuaternion"));
}

QTEST_APPLESS_MAIN(RuntimeConfigTest)
#include "test_runtime_config.moc"
