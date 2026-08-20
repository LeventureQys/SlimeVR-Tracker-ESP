#pragma once

#include "imu_types.h"
#include "slimevr_settings.h"

#include <array>

struct SlimeVrSensorDescriptor {
    SensorId sourceId = SensorId::Wrist;
    quint8 sensorId = 0;      // 0..5, stable across sessions
    quint8 sensorPosition = 0;
};

namespace SlimeVrSensorMapping {

// Fixed node tables: wrist + thumb/index/middle/ring/little distal.
constexpr std::array<quint8, 6> leftPositions()
{
    return {17, 23, 26, 29, 32, 35};
}

constexpr std::array<quint8, 6> rightPositions()
{
    return {18, 38, 41, 44, 47, 50};
}

std::array<SlimeVrSensorDescriptor, 6> descriptors(GloveSide side);

} // namespace SlimeVrSensorMapping
