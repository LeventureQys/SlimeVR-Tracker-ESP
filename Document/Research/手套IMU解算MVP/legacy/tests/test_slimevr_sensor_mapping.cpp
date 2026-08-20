#include "slimevr_sensor_mapping.h"

#include <QtTest>

class SlimeVrSensorMappingTest final : public QObject {
    Q_OBJECT

private slots:
    void leftMappingMatchesSpec();
    void rightMappingMatchesSpec();
    void sensorIdsAreStableAcrossSides();
};

void SlimeVrSensorMappingTest::leftMappingMatchesSpec()
{
    const auto descriptors = SlimeVrSensorMapping::descriptors(GloveSide::Left);
    QCOMPARE(descriptors.size(), size_t(6));
    const std::array<quint8, 6> expectedPositions{17, 23, 26, 29, 32, 35};
    const std::array<SensorId, 6> expectedSources{
        SensorId::Wrist, SensorId::Thumb, SensorId::Index,
        SensorId::Middle, SensorId::Ring, SensorId::Pinky};
    for (int index = 0; index < 6; ++index) {
        QCOMPARE(descriptors[size_t(index)].sensorId, quint8(index));
        QCOMPARE(descriptors[size_t(index)].sensorPosition, expectedPositions[size_t(index)]);
        QCOMPARE(descriptors[size_t(index)].sourceId, expectedSources[size_t(index)]);
    }
}

void SlimeVrSensorMappingTest::rightMappingMatchesSpec()
{
    const auto descriptors = SlimeVrSensorMapping::descriptors(GloveSide::Right);
    QCOMPARE(descriptors.size(), size_t(6));
    const std::array<quint8, 6> expectedPositions{18, 38, 41, 44, 47, 50};
    for (int index = 0; index < 6; ++index) {
        QCOMPARE(descriptors[size_t(index)].sensorId, quint8(index));
        QCOMPARE(descriptors[size_t(index)].sensorPosition, expectedPositions[size_t(index)]);
    }
}

void SlimeVrSensorMappingTest::sensorIdsAreStableAcrossSides()
{
    const auto left = SlimeVrSensorMapping::descriptors(GloveSide::Left);
    const auto right = SlimeVrSensorMapping::descriptors(GloveSide::Right);
    for (int index = 0; index < 6; ++index) {
        QCOMPARE(left[size_t(index)].sensorId, right[size_t(index)].sensorId);
        QCOMPARE(left[size_t(index)].sourceId, right[size_t(index)].sourceId);
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    SlimeVrSensorMappingTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_slimevr_sensor_mapping.moc"
