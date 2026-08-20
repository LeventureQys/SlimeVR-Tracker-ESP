#pragma once

#include "core/imu_frames.h"
#include "input/replay_data_source.h"
#include "protocol/frame_stream_parser.h"
#include "protocol/sequence_grouper.h"
#include "recording/recording_schema.h"

#include <QObject>
#include <QVector>

namespace handstudio {

// Loads a recording session (metadata.json + raw.bin), validates the raw hash,
// and replays through the exact parser/grouper chain used by live input. Exposes
// the collected group sequence and statistics for determinism checks.
class ReplayController final : public QObject {
    Q_OBJECT

public:
    explicit ReplayController(QObject *parent = nullptr);

    bool loadSession(const QString &recordingDir, QString *errorMessage = nullptr);
    bool hasSession() const;
    RecordingMetadata metadata() const;

    ReplayDataSource *dataSource();
    const ReplayDataSource *dataSource() const;

    void playOriginalSpeed();
    void playUnlimited();
    void pause();
    void resume();
    void stop();
    void stepGroup();

    QVector<quint8> groupSequences() const;
    GroupStatistics groupStatistics() const;
    ParserStatistics parserStatistics() const;

signals:
    void groupReady(const SixImuSampleGroup &group);
    void replayFinished();

private:
    ReplayDataSource dataSource_;
    FrameStreamParser parser_;
    SequenceGrouper grouper_;
    RecordingMetadata metadata_;
    bool hasSession_ = false;
    QVector<quint8> groupSequences_;
};

}
