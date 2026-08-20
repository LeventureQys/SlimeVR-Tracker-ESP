#include "protocol/sequence_grouper.h"

#include "core/sensor_id.h"

#include <algorithm>
#include <iterator>

namespace handstudio {
namespace {

constexpr quint8 completeMask()
{
    return static_cast<quint8>((1U << ImuSensorCount) - 1U);
}

}

SequenceGrouper::SequenceGrouper(int maxPending, QObject *parent)
    : QObject(parent)
    , maxPending_(std::max(0, maxPending))
{
}

void SequenceGrouper::addFrame(const RawImuFrame &frame)
{
    const auto index = sensorIndex(frame.sensorId);
    if (!index || *index < 0 || *index >= ImuSensorCount) {
        return;
    }

    const int sensorSlot = *index;
    ++statistics_.sensorFrameCounts[static_cast<std::size_t>(sensorSlot)];

    auto pendingIt = std::find_if(pending_.begin(), pending_.end(), [&frame](const PendingGroup &pending) {
        return pending.group.sequence == frame.sequence;
    });
    if (pendingIt == pending_.end()) {
        PendingGroup pending;
        pending.group.sequence = frame.sequence;
        pending_.append(pending);
        pendingIt = std::prev(pending_.end());
    }

    const quint8 bit = static_cast<quint8>(1U << sensorSlot);
    if ((pendingIt->group.presentMask & bit) != 0) {
        // Duplicate node: first frame is kept, subsequent frames are counted and discarded.
        ++statistics_.duplicateFrames;
        ++statistics_.sensorDuplicateCounts[static_cast<std::size_t>(sensorSlot)];
    } else {
        pendingIt->group.samples[static_cast<std::size_t>(sensorSlot)] = frame;
        pendingIt->group.presentMask |= bit;
    }

    if (pendingIt->group.presentMask == completeMask()) {
        SixImuSampleGroup completed = pendingIt->group;
        completed.complete = true;
        completed.emittedMonotonicNs = frame.receivedMonotonicNs;
        pending_.erase(pendingIt);
        ++statistics_.completeGroups;
        emit groupReady(completed);
    }

    while (pending_.size() > maxPending_) {
        SixImuSampleGroup dropped = pending_.front().group;
        pending_.removeFirst();
        dropped.complete = false;
        dropped.emittedMonotonicNs = frame.receivedMonotonicNs;
        ++statistics_.partialGroupsDropped;
        ++statistics_.pendingOverflowDrops;
        emit partialGroupDropped(dropped, GroupDropReason::PendingOverflow);
    }

    statistics_.pendingPeakGroups = std::max(statistics_.pendingPeakGroups,
                                             static_cast<quint64>(pending_.size()));
    emit statisticsChanged(statistics_);
}

void SequenceGrouper::reset()
{
    pending_.clear();
    statistics_ = {};
    emit statisticsChanged(statistics_);
}

GroupStatistics SequenceGrouper::statistics() const
{
    return statistics_;
}

int SequenceGrouper::pendingCount() const
{
    return static_cast<int>(pending_.size());
}

}
