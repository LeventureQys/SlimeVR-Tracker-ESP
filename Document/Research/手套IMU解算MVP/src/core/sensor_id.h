#pragma once

#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <array>
#include <optional>

namespace handstudio {

enum class SensorId : quint8 {
    Wrist = 0x50,
    Thumb = 0x51,
    Index = 0x52,
    Middle = 0x53,
    Ring = 0x54,
    Pinky = 0x55
};

inline constexpr std::array<SensorId, 6> AllSensorIds{
    SensorId::Wrist, SensorId::Thumb, SensorId::Index,
    SensorId::Middle, SensorId::Ring, SensorId::Pinky};

inline constexpr std::optional<SensorId> sensorIdFromAddress(quint8 address) noexcept
{
    if (address < static_cast<quint8>(SensorId::Wrist)
        || address > static_cast<quint8>(SensorId::Pinky)) {
        return std::nullopt;
    }
    return static_cast<SensorId>(address);
}

inline constexpr quint8 sensorAddress(SensorId sensorId) noexcept
{
    return static_cast<quint8>(sensorId);
}

inline constexpr std::optional<int> sensorIndex(SensorId sensorId) noexcept
{
    const auto address = sensorAddress(sensorId);
    if (!sensorIdFromAddress(address)) {
        return std::nullopt;
    }
    return static_cast<int>(address - sensorAddress(SensorId::Wrist));
}

inline constexpr std::optional<SensorId> sensorIdFromIndex(int index) noexcept
{
    if (index < 0 || index >= static_cast<int>(AllSensorIds.size())) {
        return std::nullopt;
    }
    return AllSensorIds[static_cast<std::size_t>(index)];
}

inline QString sensorDisplayName(SensorId sensorId)
{
    switch (sensorId) {
    case SensorId::Wrist: return QStringLiteral("手腕");
    case SensorId::Thumb: return QStringLiteral("拇指");
    case SensorId::Index: return QStringLiteral("食指");
    case SensorId::Middle: return QStringLiteral("中指");
    case SensorId::Ring: return QStringLiteral("无名指");
    case SensorId::Pinky: return QStringLiteral("小指");
    }
    return {};
}

}

Q_DECLARE_METATYPE(handstudio::SensorId)
