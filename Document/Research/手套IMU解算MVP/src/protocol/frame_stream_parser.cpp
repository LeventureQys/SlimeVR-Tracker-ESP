#include "protocol/frame_stream_parser.h"

#include "core/sensor_id.h"
#include "protocol/crc16.h"
#include "protocol/protocol_constants.h"

#include <QByteArrayView>

#include <algorithm>

namespace handstudio {
namespace {

qint16 decodeBigEndianInt16(const QByteArray &frame, int offset)
{
    const quint16 value = static_cast<quint16>(static_cast<quint8>(frame.at(offset)) << 8U)
                          | static_cast<quint8>(frame.at(offset + 1));
    return static_cast<qint16>(value);
}

bool axesAreZero(const std::array<qint16, 3> &axes)
{
    return axes[0] == 0 && axes[1] == 0 && axes[2] == 0;
}

}

FrameStreamParser::FrameStreamParser(QObject *parent)
    : QObject(parent)
{
}

void FrameStreamParser::appendBytes(const QByteArray &bytes, qint64 monotonicNs)
{
    if (bytes.isEmpty()) {
        return;
    }

    buffer_.append(bytes);
    statistics_.inputBytes += static_cast<quint64>(bytes.size());

    const QByteArray header = QByteArray::fromRawData("\xAA\x55", 2);
    while (true) {
        const qsizetype headerIndex = buffer_.indexOf(header);
        if (headerIndex < 0) {
            const qsizetype keep = !buffer_.isEmpty()
                                           && static_cast<quint8>(buffer_.back()) == RawFrameHeader0
                                       ? 1
                                       : 0;
            const qsizetype discard = buffer_.size() - keep;
            if (discard > 0) {
                buffer_.remove(0, discard);
                statistics_.discardedBytes += static_cast<quint64>(discard);
            }
            break;
        }

        if (headerIndex > 0) {
            buffer_.remove(0, headerIndex);
            statistics_.discardedBytes += static_cast<quint64>(headerIndex);
        }

        if (buffer_.size() < RawFrameSize) {
            break;
        }

        ++statistics_.headerCandidates;
        if (static_cast<quint8>(buffer_.at(4)) != RawFramePayloadLength) {
            ++statistics_.invalidLength;
            ++statistics_.discardedBytes;
            buffer_.remove(0, 1);
            continue;
        }

        const QByteArrayView candidate(buffer_.constData(), RawFrameSize);
        const quint16 expectedCrc = static_cast<quint8>(buffer_.at(23))
                                    | static_cast<quint16>(static_cast<quint8>(buffer_.at(24)) << 8U);
        const quint16 actualCrc = crc16Modbus(candidate.first(23));
        if (actualCrc != expectedCrc) {
            ++statistics_.invalidCrc;
            ++statistics_.discardedBytes;
            buffer_.remove(0, 1);
            continue;
        }

        const QByteArray frameBytes = buffer_.left(RawFrameSize);
        buffer_.remove(0, RawFrameSize);

        RawImuFrame frame;
        frame.address = static_cast<quint8>(frameBytes.at(2));
        const auto sensorId = sensorIdFromAddress(frame.address);
        frame.sequence = static_cast<quint8>(frameBytes.at(3));
        frame.accelerationRaw = {decodeBigEndianInt16(frameBytes, 5),
                                 decodeBigEndianInt16(frameBytes, 7),
                                 decodeBigEndianInt16(frameBytes, 9)};
        frame.gyroscopeRaw = {decodeBigEndianInt16(frameBytes, 11),
                              decodeBigEndianInt16(frameBytes, 13),
                              decodeBigEndianInt16(frameBytes, 15)};
        frame.magnetometerRaw = {decodeBigEndianInt16(frameBytes, 17),
                                 decodeBigEndianInt16(frameBytes, 19),
                                 decodeBigEndianInt16(frameBytes, 21)};
        frame.allZero = axesAreZero(frame.accelerationRaw) && axesAreZero(frame.gyroscopeRaw)
                        && axesAreZero(frame.magnetometerRaw);
        frame.receivedMonotonicNs = monotonicNs;
        frame.crcValid = true;

        ++statistics_.validFrames;
        if (!sensorId) {
            ++statistics_.unknownAddressFrames;
        } else {
            frame.sensorId = *sensorId;
            ++statistics_.emittedFrames;
            emit frameParsed(frame);
        }
    }

    statistics_.bufferPeakBytes = std::max(statistics_.bufferPeakBytes,
                                           static_cast<quint64>(buffer_.size()));
    emit statisticsChanged(statistics_);
}

void FrameStreamParser::reset()
{
    buffer_.clear();
    statistics_ = {};
    emit statisticsChanged(statistics_);
}

ParserStatistics FrameStreamParser::statistics() const
{
    return statistics_;
}

}
