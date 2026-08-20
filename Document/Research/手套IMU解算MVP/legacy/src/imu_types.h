#pragma once

#include "protocol_constants.h"

#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <array>
#include <optional>

enum class SensorId : quint8 {
    Wrist = 0x50,
    Thumb = 0x51,
    Index = 0x52,
    Middle = 0x53,
    Ring = 0x54,
    Pinky = 0x55
};

inline std::optional<SensorId> sensorIdFromAddress(quint8 address)
{
    switch (address) {
    case 0x50: return SensorId::Wrist;
    case 0x51: return SensorId::Thumb;
    case 0x52: return SensorId::Index;
    case 0x53: return SensorId::Middle;
    case 0x54: return SensorId::Ring;
    case 0x55: return SensorId::Pinky;
    default: return std::nullopt;
    }
}

inline int sensorIndex(SensorId id)
{
    return static_cast<int>(static_cast<quint8>(id) - static_cast<quint8>(SensorId::Wrist));
}

inline QString sensorDisplayName(SensorId id)
{
    switch (id) {
    case SensorId::Wrist: return QStringLiteral("手腕");
    case SensorId::Thumb: return QStringLiteral("拇指");
    case SensorId::Index: return QStringLiteral("食指");
    case SensorId::Middle: return QStringLiteral("中指");
    case SensorId::Ring: return QStringLiteral("无名指");
    case SensorId::Pinky: return QStringLiteral("小指");
    }
    return QString();
}

struct RawAxes {
    qint16 x = 0;
    qint16 y = 0;
    qint16 z = 0;
};

struct ImuFrame {
    quint8 address = 0;
    std::optional<SensorId> sensorId;
    quint8 sequence = 0;
    RawAxes acceleration;
    RawAxes gyroscope;
    RawAxes magnetometer;
    bool allZero = true;
    qint64 receivedMonotonicNs = 0;
};

struct ImuSampleGroup {
    quint8 sequence = 0;
    std::array<std::optional<ImuFrame>, SixImuProtocol::SensorCount> frames;
    bool complete = false;
    quint8 presentMask = 0;
    qint64 emittedMonotonicNs = 0;
};

struct ParserStatistics {
    quint64 inputBytes = 0;
    quint64 headerCandidates = 0;
    quint64 validFrames = 0;
    quint64 invalidLength = 0;
    quint64 invalidCrc = 0;
    quint64 discardedBytes = 0;
    quint64 unknownAddressFrames = 0;
};

struct GroupStatistics {
    quint64 completeGroups = 0;
    quint64 partialGroups = 0;
    quint64 duplicateFrames = 0;
    std::array<quint64, SixImuProtocol::SensorCount> sensorFrameCounts{};
};

Q_DECLARE_METATYPE(ImuFrame)
Q_DECLARE_METATYPE(ImuSampleGroup)
Q_DECLARE_METATYPE(ParserStatistics)
Q_DECLARE_METATYPE(GroupStatistics)
