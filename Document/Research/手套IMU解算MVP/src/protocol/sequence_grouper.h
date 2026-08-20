#pragma once

#include "core/imu_frames.h"
#include "protocol/protocol_constants.h"
#include "protocol/protocol_statistics.h"

#include <QList>
#include <QObject>

namespace handstudio {

// Groups raw frames by sequence number into SixImuSampleGroup.
//
// Frozen decisions (task book):
// - duplicate node: first frame is kept, subsequent frames are counted and discarded.
// - pending limit default 8; oldest incomplete group is evicted.
// - only complete && presentMask == 0x3f is forwarded to the default downstream
//   (groupReady). Incomplete evicted groups go to partialGroupDropped for diagnostics.
class SequenceGrouper final : public QObject {
    Q_OBJECT

public:
    explicit SequenceGrouper(int maxPending = DefaultPendingGroupLimit,
                             QObject *parent = nullptr);

    void addFrame(const RawImuFrame &frame);
    void reset();
    GroupStatistics statistics() const;
    int pendingCount() const;

signals:
    void groupReady(const SixImuSampleGroup &group);
    void partialGroupDropped(const SixImuSampleGroup &group, GroupDropReason reason);
    void statisticsChanged(const GroupStatistics &statistics);

private:
    struct PendingGroup {
        SixImuSampleGroup group;
    };

    int maxPending_;
    QList<PendingGroup> pending_;
    GroupStatistics statistics_;
};

}
