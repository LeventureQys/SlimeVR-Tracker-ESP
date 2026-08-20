#pragma once

#include "core/diagnostic.h"
#include "recording/bounded_write_queue.h"
#include "recording/recording_schema.h"

#include <QFile>
#include <QObject>
#include <QString>

namespace handstudio {

enum class RecorderState {
    Idle,
    Recording,
    Paused,
    Stopping,
    Error
};

struct RecordingStatistics {
    quint64 rawBytesWritten = 0;
    quint64 pausedSkippedBytes = 0;
    quint64 diagnosticsWritten = 0;
    quint64 fusedPosesWritten = 0;
    quint64 observationsWritten = 0;
    quint64 skeletonFramesWritten = 0;
    quint64 writeQueuePeakItems = 0;
    quint64 writeQueueOverflowBytes = 0;
    quint64 derivedQueueDroppedItems = 0;
};

// Records the raw byte stream (raw.bin, byte 1:1 including bad frames),
// metadata.json (schema/version/hash) and diagnostics.jsonl.
//
// State machine: Idle -> Recording <-> Paused -> Stopping -> Idle.
// Paused state does not write raw bytes.
class SessionRecorder final : public QObject {
    Q_OBJECT

public:
    explicit SessionRecorder(QObject *parent = nullptr);
    ~SessionRecorder() override;

    void setMaxWriteQueueItems(int maxItems);
    // Test hook: when false, raw bytes accumulate in the bounded queue instead
    // of being flushed immediately, exposing the overflow policy.
    void setAutoFlush(bool enabled);

    bool startRecording(const QString &outputDir, const RecordingMetadata &metadata,
                        Diagnostic *error = nullptr);
    void pause();
    void resume();
    void stop();
    RecorderState state() const;

    void appendRawBytes(const QByteArray &bytes);
    void appendDiagnostic(const Diagnostic &diagnostic);
    void appendFusedPoses(const QByteArray &jsonLine);
    void appendObservation(const QByteArray &jsonLine);
    void appendSkeletonFrame(const QByteArray &jsonLine);

    RecordingStatistics statistics() const;
    QString outputDir() const;

signals:
    void stateChanged(RecorderState state, const Diagnostic &diagnostic);
    void rawBytesWritten(qint64 totalBytes);
    void stopped(const QString &outputDir, const RecordingMetadata &finalMetadata);

private:
    bool flushRawQueue(Diagnostic *error);
    void appendDerived(QFile &file, BoundedWriteQueue &queue, const QByteArray &line,
                       quint64 &writtenCounter);
    void flushDerived(QFile &file, BoundedWriteQueue &queue, quint64 &writtenCounter);
    void finalize();
    void setState(RecorderState state, const Diagnostic &diagnostic);

    RecorderState state_ = RecorderState::Idle;
    RecordingStatistics statistics_;
    RecordingMetadata metadata_;
    QString outputDir_;
    QFile rawFile_;
    QFile diagnosticsFile_;
    QFile fusedPosesFile_;
    QFile observationsFile_;
    QFile skeletonFramesFile_;
    BoundedWriteQueue rawQueue_;
    BoundedWriteQueue fusedPosesQueue_;
    BoundedWriteQueue observationsQueue_;
    BoundedWriteQueue skeletonFramesQueue_;
    bool autoFlush_ = true;
};

}

Q_DECLARE_METATYPE(handstudio::RecorderState)
Q_DECLARE_METATYPE(handstudio::RecordingStatistics)
