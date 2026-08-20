#pragma once

#include "core/imu_frames.h"
#include "protocol/protocol_statistics.h"

#include <QByteArray>
#include <QObject>

namespace handstudio {

// Byte-stream resynchronizing parser for the 25-byte "AA 55" raw IMU frame.
//
// Contract (design doc 7.1): only CRC-valid frames with a known address
// (0x50..0x55) are emitted via frameParsed. CRC-invalid and unknown-address
// frames are counted in statistics but never forwarded to fusion.
class FrameStreamParser final : public QObject {
    Q_OBJECT

public:
    explicit FrameStreamParser(QObject *parent = nullptr);

    void appendBytes(const QByteArray &bytes, qint64 monotonicNs);
    void reset();
    ParserStatistics statistics() const;

signals:
    void frameParsed(const RawImuFrame &frame);
    void statisticsChanged(const ParserStatistics &statistics);

private:
    QByteArray buffer_;
    ParserStatistics statistics_;
};

}
