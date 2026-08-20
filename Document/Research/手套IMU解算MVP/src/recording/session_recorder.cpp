#include "recording/session_recorder.h"

#include "core/schema_version.h"

#include <QDateTime>
#include <QDir>
#include <QJsonDocument>

#include <algorithm>
#include <utility>

namespace handstudio {
namespace {

Diagnostic makeDiagnostic(DiagnosticSeverity severity, QString code, QString message, QString detail = {})
{
    return {severity, std::move(code), std::move(message), std::move(detail), 0};
}

QString utcNowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

}

SessionRecorder::SessionRecorder(QObject *parent)
    : QObject(parent)
{
}

SessionRecorder::~SessionRecorder()
{
    if (state_ == RecorderState::Recording || state_ == RecorderState::Paused
        || state_ == RecorderState::Stopping) {
        stop();
    }
}

void SessionRecorder::setMaxWriteQueueItems(int maxItems)
{
    rawQueue_.setMaxItems(maxItems);
    fusedPosesQueue_.setMaxItems(maxItems);
    observationsQueue_.setMaxItems(maxItems);
    skeletonFramesQueue_.setMaxItems(maxItems);
}

void SessionRecorder::setAutoFlush(bool enabled)
{
    autoFlush_ = enabled;
}

bool SessionRecorder::startRecording(const QString &outputDir, const RecordingMetadata &metadata,
                                     Diagnostic *error)
{
    if (state_ != RecorderState::Idle) {
        const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Error,
                                               QStringLiteral("recorder.already-started"),
                                               QStringLiteral("录制器已处于活动状态，不能二次 start"));
        if (error) {
            *error = diagnostic;
        }
        setState(RecorderState::Error, diagnostic);
        return false;
    }

    if (!QDir().mkpath(outputDir)) {
        const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Error,
                                               QStringLiteral("recorder.mkdir"),
                                               QStringLiteral("无法创建录制目录"), outputDir);
        if (error) {
            *error = diagnostic;
        }
        setState(RecorderState::Error, diagnostic);
        return false;
    }

    outputDir_ = outputDir;
    metadata_ = metadata;
    metadata_.applicationVersion = ApplicationVersion.toString();
    if (metadata_.startedUtc.isEmpty()) {
        metadata_.startedUtc = utcNowIso();
    }
    statistics_ = {};
    rawQueue_.clear();
    fusedPosesQueue_.clear();
    observationsQueue_.clear();
    skeletonFramesQueue_.clear();

    rawFile_.setFileName(outputDir_ + QStringLiteral("/raw.bin"));
    if (!rawFile_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Error,
                                               QStringLiteral("recorder.raw.open"),
                                               QStringLiteral("无法打开 raw.bin"), rawFile_.errorString());
        if (error) {
            *error = diagnostic;
        }
        setState(RecorderState::Error, diagnostic);
        return false;
    }

    fusedPosesFile_.setFileName(outputDir_ + QStringLiteral("/fused_poses.jsonl"));
    observationsFile_.setFileName(outputDir_ + QStringLiteral("/observations.jsonl"));
    skeletonFramesFile_.setFileName(outputDir_ + QStringLiteral("/skeleton_frames.jsonl"));
    for (QFile *file : {&fusedPosesFile_, &observationsFile_, &skeletonFramesFile_}) {
        if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Warning,
                                                   QStringLiteral("recorder.derived.open"),
                                                   QStringLiteral("无法打开派生 JSONL"), file->errorString());
            emit stateChanged(state_, diagnostic);
        }
    }

    diagnosticsFile_.setFileName(outputDir_ + QStringLiteral("/diagnostics.jsonl"));
    if (!diagnosticsFile_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Warning,
                                               QStringLiteral("recorder.diagnostics.open"),
                                               QStringLiteral("无法打开 diagnostics.jsonl"),
                                               diagnosticsFile_.errorString());
        emit stateChanged(state_, diagnostic);
    }

    const auto writeResult = writeMetadataJson(outputDir_ + QStringLiteral("/metadata.json"), metadata_);
    if (!writeResult.success) {
        const auto diagnostic = writeResult.diagnostics.isEmpty()
                                    ? makeDiagnostic(DiagnosticSeverity::Error,
                                                     QStringLiteral("recorder.metadata.write"),
                                                     QStringLiteral("无法写入 metadata.json"))
                                    : writeResult.diagnostics.first();
        if (error) {
            *error = diagnostic;
        }
        rawFile_.close();
        diagnosticsFile_.close();
        setState(RecorderState::Error, diagnostic);
        return false;
    }

    setState(RecorderState::Recording,
             makeDiagnostic(DiagnosticSeverity::Info,
                            QStringLiteral("recorder.started"),
                            QStringLiteral("录制已开始"), outputDir_));
    return true;
}

void SessionRecorder::pause()
{
    if (state_ == RecorderState::Recording) {
        setState(RecorderState::Paused,
                 makeDiagnostic(DiagnosticSeverity::Info,
                                QStringLiteral("recorder.paused"),
                                QStringLiteral("录制已暂停")));
        return;
    }
    const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Warning,
                                           QStringLiteral("recorder.pause.invalid"),
                                           QStringLiteral("仅在录制中可暂停"));
    emit stateChanged(state_, diagnostic);
}

void SessionRecorder::resume()
{
    if (state_ == RecorderState::Paused) {
        setState(RecorderState::Recording,
                 makeDiagnostic(DiagnosticSeverity::Info,
                                QStringLiteral("recorder.resumed"),
                                QStringLiteral("录制已恢复")));
        return;
    }
    const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Warning,
                                           QStringLiteral("recorder.resume.invalid"),
                                           QStringLiteral("仅在暂停中可恢复"));
    emit stateChanged(state_, diagnostic);
}

void SessionRecorder::stop()
{
    if (state_ != RecorderState::Recording && state_ != RecorderState::Paused) {
        return;
    }
    setState(RecorderState::Stopping,
             makeDiagnostic(DiagnosticSeverity::Info,
                            QStringLiteral("recorder.stopping"),
                            QStringLiteral("录制正在停止")));
    finalize();
}

RecorderState SessionRecorder::state() const
{
    return state_;
}

void SessionRecorder::appendRawBytes(const QByteArray &bytes)
{
    if (state_ == RecorderState::Paused) {
        statistics_.pausedSkippedBytes += static_cast<quint64>(bytes.size());
        return;
    }
    if (state_ != RecorderState::Recording) {
        return;
    }
    if (bytes.isEmpty()) {
        return;
    }

    if (!rawQueue_.enqueue(bytes)) {
        // Queue full: raw.bin is the highest-priority stream, so write it
        // directly and record the overflow for observability.
        statistics_.writeQueueOverflowBytes += static_cast<quint64>(bytes.size());
        rawFile_.write(bytes);
        statistics_.rawBytesWritten += static_cast<quint64>(bytes.size());
        emit rawBytesWritten(statistics_.rawBytesWritten);
        return;
    }

    statistics_.writeQueuePeakItems = std::max(statistics_.writeQueuePeakItems,
                                               rawQueue_.peakItems());
    if (autoFlush_) {
        Diagnostic error;
        flushRawQueue(&error);
    }
}

void SessionRecorder::appendFusedPoses(const QByteArray &jsonLine)
{
    appendDerived(fusedPosesFile_, fusedPosesQueue_, jsonLine, statistics_.fusedPosesWritten);
}

void SessionRecorder::appendObservation(const QByteArray &jsonLine)
{
    appendDerived(observationsFile_, observationsQueue_, jsonLine, statistics_.observationsWritten);
}

void SessionRecorder::appendSkeletonFrame(const QByteArray &jsonLine)
{
    appendDerived(skeletonFramesFile_, skeletonFramesQueue_, jsonLine, statistics_.skeletonFramesWritten);
}

void SessionRecorder::appendDiagnostic(const Diagnostic &diagnostic)
{
    if (state_ != RecorderState::Recording && state_ != RecorderState::Paused
        && state_ != RecorderState::Stopping) {
        return;
    }
    if (!diagnosticsFile_.isOpen()) {
        return;
    }
    QJsonObject object;
    object.insert(QStringLiteral("severity"), static_cast<int>(diagnostic.severity));
    object.insert(QStringLiteral("code"), diagnostic.code);
    object.insert(QStringLiteral("message"), diagnostic.message);
    object.insert(QStringLiteral("detail"), diagnostic.detail);
    object.insert(QStringLiteral("timestampNs"), static_cast<double>(diagnostic.timestampNs));
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    if (diagnosticsFile_.write(line) >= 0) {
        ++statistics_.diagnosticsWritten;
    }
}

RecordingStatistics SessionRecorder::statistics() const
{
    return statistics_;
}

QString SessionRecorder::outputDir() const
{
    return outputDir_;
}

bool SessionRecorder::flushRawQueue(Diagnostic *error)
{
    while (auto item = rawQueue_.dequeue()) {
        if (rawFile_.write(*item) < 0) {
            const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Error,
                                                   QStringLiteral("recorder.raw.write"),
                                                   QStringLiteral("写入 raw.bin 失败"), rawFile_.errorString());
            if (error) {
                *error = diagnostic;
            }
            setState(RecorderState::Error, diagnostic);
            return false;
        }
        statistics_.rawBytesWritten += static_cast<quint64>(item->size());
    }
    emit rawBytesWritten(statistics_.rawBytesWritten);
    return true;
}

void SessionRecorder::appendDerived(QFile &file, BoundedWriteQueue &queue, const QByteArray &line,
                                    quint64 &writtenCounter)
{
    if (state_ != RecorderState::Recording || line.isEmpty() || !file.isOpen()) return;
    if (!queue.enqueue(line)) {
        ++statistics_.derivedQueueDroppedItems;
        return;
    }
    statistics_.writeQueuePeakItems = std::max(statistics_.writeQueuePeakItems, queue.peakItems());
    if (autoFlush_) flushDerived(file, queue, writtenCounter);
}

void SessionRecorder::flushDerived(QFile &file, BoundedWriteQueue &queue, quint64 &writtenCounter)
{
    while (auto item = queue.dequeue()) {
        if (file.write(*item) >= 0) ++writtenCounter;
    }
}

void SessionRecorder::finalize()
{
    Diagnostic error;
    flushRawQueue(&error);
    flushDerived(fusedPosesFile_, fusedPosesQueue_, statistics_.fusedPosesWritten);
    flushDerived(observationsFile_, observationsQueue_, statistics_.observationsWritten);
    flushDerived(skeletonFramesFile_, skeletonFramesQueue_, statistics_.skeletonFramesWritten);

    rawFile_.flush();
    rawFile_.close();
    fusedPosesFile_.close();
    observationsFile_.close();
    skeletonFramesFile_.close();
    diagnosticsFile_.close();

    metadata_.rawBytes = static_cast<qint64>(statistics_.rawBytesWritten);
    metadata_.stoppedUtc = utcNowIso();
    metadata_.rawSha256 = computeFileSha256Hex(outputDir_ + QStringLiteral("/raw.bin"));
    writeMetadataJson(outputDir_ + QStringLiteral("/metadata.json"), metadata_);

    setState(RecorderState::Idle,
             makeDiagnostic(DiagnosticSeverity::Info,
                            QStringLiteral("recorder.stopped"),
                            QStringLiteral("录制已停止"), outputDir_));
    emit stopped(outputDir_, metadata_);
}

void SessionRecorder::setState(RecorderState state, const Diagnostic &diagnostic)
{
    state_ = state;
    emit stateChanged(state_, diagnostic);
}

}
