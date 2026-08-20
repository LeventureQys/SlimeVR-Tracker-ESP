#include "sequence_grouper.h"

namespace {

handstudio::RawImuFrame toRawFrame(const ImuFrame &frame)
{
    handstudio::RawImuFrame raw;
    raw.sensorId = static_cast<handstudio::SensorId>(frame.address);
    raw.address = frame.address;
    raw.sequence = frame.sequence;
    raw.receivedMonotonicNs = frame.receivedMonotonicNs;
    raw.accelerationRaw = {frame.acceleration.x, frame.acceleration.y, frame.acceleration.z};
    raw.gyroscopeRaw = {frame.gyroscope.x, frame.gyroscope.y, frame.gyroscope.z};
    raw.magnetometerRaw = {frame.magnetometer.x, frame.magnetometer.y, frame.magnetometer.z};
    raw.crcValid = true;
    raw.allZero = frame.allZero;
    return raw;
}

ImuSampleGroup toLegacyGroup(const handstudio::SixImuSampleGroup &group)
{
    ImuSampleGroup legacy;
    legacy.sequence = group.sequence;
    legacy.complete = group.complete;
    legacy.presentMask = group.presentMask;
    legacy.emittedMonotonicNs = group.emittedMonotonicNs;
    for (int i = 0; i < SixImuProtocol::SensorCount; ++i) {
        const auto &raw = group.samples[static_cast<std::size_t>(i)];
        ImuFrame frame;
        frame.address = raw.address;
        frame.sensorId = ::sensorIdFromAddress(raw.address);
        frame.sequence = raw.sequence;
        frame.acceleration = {raw.accelerationRaw[0], raw.accelerationRaw[1], raw.accelerationRaw[2]};
        frame.gyroscope = {raw.gyroscopeRaw[0], raw.gyroscopeRaw[1], raw.gyroscopeRaw[2]};
        frame.magnetometer = {raw.magnetometerRaw[0], raw.magnetometerRaw[1], raw.magnetometerRaw[2]};
        frame.allZero = raw.allZero;
        frame.receivedMonotonicNs = raw.receivedMonotonicNs;
        legacy.frames[static_cast<std::size_t>(i)] = frame;
    }
    return legacy;
}

GroupStatistics toLegacyStats(const handstudio::GroupStatistics &s)
{
    GroupStatistics legacy;
    legacy.completeGroups = s.completeGroups;
    legacy.partialGroups = s.partialGroupsDropped;
    legacy.duplicateFrames = s.duplicateFrames;
    for (int i = 0; i < SixImuProtocol::SensorCount; ++i) {
        legacy.sensorFrameCounts[static_cast<std::size_t>(i)]
            = s.sensorFrameCounts[static_cast<std::size_t>(i)];
    }
    return legacy;
}

}

SequenceGrouper::SequenceGrouper(int maxPending, QObject *parent)
    : QObject(parent)
    , impl_(maxPending, this)
{
    connect(&impl_, &handstudio::SequenceGrouper::groupReady,
            this, [this](const handstudio::SixImuSampleGroup &group) {
                emit completeGroupReady(toLegacyGroup(group));
            });
    connect(&impl_, &handstudio::SequenceGrouper::partialGroupDropped,
            this, [this](const handstudio::SixImuSampleGroup &group,
                         handstudio::GroupDropReason /*reason*/) {
                emit partialGroupDropped(toLegacyGroup(group));
            });
    connect(&impl_, &handstudio::SequenceGrouper::statisticsChanged,
            this, [this](const handstudio::GroupStatistics &s) {
                emit statisticsChanged(toLegacyStats(s));
            });
}

void SequenceGrouper::addFrame(const ImuFrame &frame)
{
    if (!frame.sensorId.has_value()) {
        return;
    }
    impl_.addFrame(toRawFrame(frame));
}

void SequenceGrouper::reset()
{
    impl_.reset();
}

GroupStatistics SequenceGrouper::statistics() const
{
    return toLegacyStats(impl_.statistics());
}
