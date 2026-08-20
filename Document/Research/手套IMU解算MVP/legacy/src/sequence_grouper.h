#pragma once

#include "imu_types.h"
#include "protocol/sequence_grouper.h"

#include <QList>
#include <QObject>

// Legacy adaptation layer over handstudio::SequenceGrouper. Retained so the
// existing six_imu_core / legacy tests keep compiling; all grouping delegates to
// the single canonical implementation in src/protocol/ (no duplicate logic).
class SequenceGrouper final : public QObject {
    Q_OBJECT

public:
    explicit SequenceGrouper(int maxPending = SixImuProtocol::DefaultPendingGroupLimit,
                             QObject *parent = nullptr);

    void addFrame(const ImuFrame &frame);
    void reset();
    GroupStatistics statistics() const;

signals:
    void completeGroupReady(const ImuSampleGroup &group);
    void partialGroupDropped(const ImuSampleGroup &group);
    void statisticsChanged(const GroupStatistics &statistics);

private:
    handstudio::SequenceGrouper impl_;
};
