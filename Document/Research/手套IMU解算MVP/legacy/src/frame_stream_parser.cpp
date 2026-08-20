#include "frame_stream_parser.h"

namespace {

ParserStatistics toLegacy(const handstudio::ParserStatistics &s)
{
    ParserStatistics legacy;
    legacy.inputBytes = s.inputBytes;
    legacy.headerCandidates = s.headerCandidates;
    legacy.validFrames = s.validFrames;
    legacy.invalidLength = s.invalidLength;
    legacy.invalidCrc = s.invalidCrc;
    legacy.discardedBytes = s.discardedBytes;
    legacy.unknownAddressFrames = s.unknownAddressFrames;
    return legacy;
}

ImuFrame toLegacyFrame(const handstudio::RawImuFrame &raw)
{
    ImuFrame frame;
    frame.address = raw.address;
    frame.sensorId = ::sensorIdFromAddress(raw.address);
    frame.sequence = raw.sequence;
    frame.acceleration = {raw.accelerationRaw[0], raw.accelerationRaw[1], raw.accelerationRaw[2]};
    frame.gyroscope = {raw.gyroscopeRaw[0], raw.gyroscopeRaw[1], raw.gyroscopeRaw[2]};
    frame.magnetometer = {raw.magnetometerRaw[0], raw.magnetometerRaw[1], raw.magnetometerRaw[2]};
    frame.allZero = raw.allZero;
    frame.receivedMonotonicNs = raw.receivedMonotonicNs;
    return frame;
}

}

FrameStreamParser::FrameStreamParser(QObject *parent)
    : QObject(parent)
    , impl_(this)
{
    connect(&impl_, &handstudio::FrameStreamParser::frameParsed,
            this, [this](const handstudio::RawImuFrame &raw) {
                emit frameParsed(toLegacyFrame(raw));
            });
    connect(&impl_, &handstudio::FrameStreamParser::statisticsChanged,
            this, [this](const handstudio::ParserStatistics &s) {
                emit statisticsChanged(toLegacy(s));
            });
}

void FrameStreamParser::appendBytes(const QByteArray &bytes, qint64 monotonicNs)
{
    impl_.appendBytes(bytes, monotonicNs);
}

void FrameStreamParser::reset()
{
    impl_.reset();
}

ParserStatistics FrameStreamParser::statistics() const
{
    return toLegacy(impl_.statistics());
}
