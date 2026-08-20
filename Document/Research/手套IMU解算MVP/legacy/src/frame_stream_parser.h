#pragma once

#include "imu_types.h"
#include "protocol/frame_stream_parser.h"

#include <QByteArray>
#include <QObject>

// Legacy adaptation layer over handstudio::FrameStreamParser. Retained so the
// existing six_imu_core / legacy tests keep compiling; all parsing delegates to
// the single canonical implementation in src/protocol/ (no duplicate logic).
class FrameStreamParser final : public QObject {
    Q_OBJECT

public:
    explicit FrameStreamParser(QObject *parent = nullptr);

    void appendBytes(const QByteArray &bytes, qint64 monotonicNs);
    void reset();
    ParserStatistics statistics() const;

signals:
    void frameParsed(const ImuFrame &frame);
    void statisticsChanged(const ParserStatistics &statistics);

private:
    handstudio::FrameStreamParser impl_;
};
