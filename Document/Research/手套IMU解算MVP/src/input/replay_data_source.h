#pragma once

#include "input/idata_source.h"
#include "protocol/frame_stream_parser.h"
#include "protocol/sequence_grouper.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QTimer>

namespace handstudio {

enum class ReplayMode {
    OriginalSpeed,
    Unlimited,
    StepByGroup
};

// Reads a recorded raw.bin and re-emits it through bytesReady, so replay walks
// the exact same parse path as a live serial source. Supports original speed,
// unlimited (as-fast-as-possible), pause/resume, and stepping one complete
// group at a time. Timestamps are deterministic (derived from byte position),
// never wall-clock, so identical raw.bin inputs produce identical outputs.
class ReplayDataSource final : public IDataSource {
    Q_OBJECT

public:
    explicit ReplayDataSource(QObject *parent = nullptr);
    ~ReplayDataSource() override;

    bool load(const QString &rawBinPath, QString *errorMessage = nullptr);
    void setMode(ReplayMode mode);
    ReplayMode mode() const;
    void setChunkBytes(qint64 bytes);
    void setBytesPerSecond(qint64 bytesPerSecond);

    qint64 position() const;
    qint64 totalBytes() const;
    bool atEnd() const;
    SourceState state() const;

public slots:
    void start() override;
    void stop() override;
    void pause();
    void resume();
    void stepGroup();

signals:
    void replayFinished();
    void groupStepped(quint8 sequence);

private:
    void emitChunk(qint64 begin, qint64 end);
    void emitUnlimited();
    void onOriginalSpeedTick();
    bool stepToNextGroupBoundary();
    void setState(SourceState state, QString code, QString message);
    void resetReplay();

    QByteArray rawBytes_;
    ReplayMode mode_ = ReplayMode::Unlimited;
    qint64 position_ = 0;
    qint64 chunkBytes_ = 4096;
    qint64 bytesPerSecond_ = RawBytesPerSecond;
    qint64 nsPerByte_ = 1'000'000'000LL / RawBytesPerSecond;

    SourceState state_ = SourceState::Idle;

    QTimer timer_;
    QElapsedTimer wallClock_;

    // Reuses the canonical parser/grouper (single implementation) solely to
    // detect complete-group boundaries for step mode. Bytes are still emitted
    // raw and re-parsed downstream.
    FrameStreamParser stepParser_;
    SequenceGrouper stepGrouper_;
    bool stepping_ = false;
    qint64 stepGroupBoundary_ = -1;
    quint8 stepGroupSequence_ = 0;
};

}
