#include "slimevr_sensor_mapping.h"

namespace SlimeVrSensorMapping {

std::array<SlimeVrSensorDescriptor, 6> descriptors(GloveSide side)
{
    const std::array<SensorId, 6> sourceIds{
        SensorId::Wrist, SensorId::Thumb, SensorId::Index,
        SensorId::Middle, SensorId::Ring, SensorId::Pinky};
    const std::array<quint8, 6> positions = side == GloveSide::Right
        ? rightPositions()
        : leftPositions();

    std::array<SlimeVrSensorDescriptor, 6> result{};
    for (int index = 0; index < 6; ++index) {
        result[size_t(index)].sourceId = sourceIds[size_t(index)];
        result[size_t(index)].sensorId = quint8(index);
        result[size_t(index)].sensorPosition = positions[size_t(index)];
    }
    return result;
}

} // namespace SlimeVrSensorMapping
