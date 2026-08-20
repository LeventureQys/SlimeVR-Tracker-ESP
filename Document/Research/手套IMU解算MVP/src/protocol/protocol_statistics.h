#pragma once

#include "protocol/protocol_constants.h"

#include <QMetaType>
#include <QtGlobal>

#include <array>

namespace handstudio {

struct ParserStatistics {
    quint64 inputBytes = 0;
    quint64 headerCandidates = 0;
    quint64 validFrames = 0;          // CRC-valid frames decoded
    quint64 invalidLength = 0;
    quint64 invalidCrc = 0;
    quint64 discardedBytes = 0;
    quint64 unknownAddressFrames = 0; // CRC-valid but unknown address (dropped, not emitted)
    quint64 emittedFrames = 0;        // frames actually emitted (known address)
    quint64 bufferPeakBytes = 0;      // resync buffer high-water mark
};

enum class GroupDropReason : quint8 {
    PendingOverflow = 0
};

struct GroupStatistics {
    quint64 completeGroups = 0;
    quint64 partialGroupsDropped = 0;
    quint64 duplicateFrames = 0;
    quint64 pendingPeakGroups = 0;    // pending queue high-water mark
    std::array<quint64, ImuSensorCount> sensorFrameCounts{};
    std::array<quint64, ImuSensorCount> sensorDuplicateCounts{};
    quint64 pendingOverflowDrops = 0; // drop reason: pending queue overflow
};

}

Q_DECLARE_METATYPE(handstudio::ParserStatistics)
Q_DECLARE_METATYPE(handstudio::GroupDropReason)
Q_DECLARE_METATYPE(handstudio::GroupStatistics)
