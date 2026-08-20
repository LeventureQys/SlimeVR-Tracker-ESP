#include "input/replay_data_source.h"

#include "core/imu_frames.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace handstudio {

namespace {

Diagnostic makeDiagnostic(DiagnosticSeverity severity, QString code, QString message, QString detail = {})
{
    return {severity, std::move(code), std::move(message), std::move(detail), 0};
}

}

ReplayDataSource::ReplayDataSource(QObject *parent)
    : IDataSource(parent)
    , stepParser_(this)
    , stepGrouper_(DefaultPendingGroupLimit, this)
{
    connect(&stepParser_, &FrameStreamParser::frameParsed,
            &stepGrouper_, &SequenceGrouper::addFrame);
    connect(&stepGrouper_, &SequenceGrouper::groupReady,
            this, [this](const SixImuSampleGroup &group) {
                if (stepping_) {
                    stepping_ = false;
                    stepGroupBoundary_ = position_;
                    stepGroupSequence_ = group.sequence;
                }
            });

    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this, &ReplayDataSource::onOriginalSpeedTick);
}

ReplayDataSource::~ReplayDataSource()
{
    stop();
}

bool ReplayDataSource::load(const QString &rawBinPath, QString *errorMessage)
{
    QFile file(rawBinPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取 raw.bin: %1").arg(file.errorString());
        }
        return false;
    }
    rawBytes_ = file.readAll();
    file.close();
    resetReplay();
    return true;
}

void ReplayDataSource::setMode(ReplayMode mode)
{
    mode_ = mode;
}

ReplayMode ReplayDataSource::mode() const
{
    return mode_;
}

void ReplayDataSource::setChunkBytes(qint64 bytes)
{
    chunkBytes_ = std::max<qint64>(1, bytes);
}

void ReplayDataSource::setBytesPerSecond(qint64 bytesPerSecond)
{
    bytesPerSecond_ = std::max<qint64>(1, bytesPerSecond);
    nsPerByte_ = 1'000'000'000LL / bytesPerSecond_;
}

qint64 ReplayDataSource::position() const
{
    return position_;
}

qint64 ReplayDataSource::totalBytes() const
{
    return static_cast<qint64>(rawBytes_.size());
}

bool ReplayDataSource::atEnd() const
{
    return position_ >= totalBytes();
}

SourceState ReplayDataSource::state() const
{
    return state_;
}

void ReplayDataSource::start()
{
    if (rawBytes_.isEmpty()) {
        setState(SourceState::Error, QStringLiteral("replay.empty"),
                 QStringLiteral("回放数据为空，请先加载 raw.bin"));
        return;
    }
    if (state() == SourceState::Running) {
        return;
    }

    wallClock_.start();
    resetReplay();

    setState(SourceState::Running, QStringLiteral("replay.started"),
             QStringLiteral("回放已开始"));

    if (mode_ == ReplayMode::Unlimited) {
        emitUnlimited();
    } else if (mode_ == ReplayMode::OriginalSpeed) {
        timer_.start(10);
    }
    // StepByGroup mode waits for explicit stepGroup() calls.
}

void ReplayDataSource::stop()
{
    timer_.stop();
    if (state() == SourceState::Running || state() == SourceState::Paused) {
        setState(SourceState::Stopping, QStringLiteral("replay.stopping"),
                 QStringLiteral("回放正在停止"));
    }
    resetReplay();
    if (state() != SourceState::Idle) {
        setState(SourceState::Idle, QStringLiteral("replay.stopped"),
                 QStringLiteral("回放已停止"));
    }
}

void ReplayDataSource::pause()
{
    if (state() != SourceState::Running) {
        return;
    }
    if (mode_ == ReplayMode::OriginalSpeed) {
        timer_.stop();
    }
    setState(SourceState::Paused, QStringLiteral("replay.paused"),
             QStringLiteral("回放已暂停"));
}

void ReplayDataSource::resume()
{
    if (state() != SourceState::Paused) {
        return;
    }
    wallClock_.restart();
    if (mode_ == ReplayMode::OriginalSpeed) {
        timer_.start(10);
    } else if (mode_ == ReplayMode::Unlimited) {
        setState(SourceState::Running, QStringLiteral("replay.resumed"),
                 QStringLiteral("回放已恢复"));
        emitUnlimited();
        return;
    }
    setState(SourceState::Running, QStringLiteral("replay.resumed"),
             QStringLiteral("回放已恢复"));
}

void ReplayDataSource::stepGroup()
{
    if (mode_ != ReplayMode::StepByGroup) {
        return;
    }
    if (state() != SourceState::Running) {
        setState(SourceState::Running, QStringLiteral("replay.step"),
                 QStringLiteral("逐组回放进行中"));
    }
    if (!stepToNextGroupBoundary()) {
        emit replayFinished();
    }
}

void ReplayDataSource::emitChunk(qint64 begin, qint64 end)
{
    if (end <= begin) {
        return;
    }
    const QByteArray chunk = rawBytes_.mid(begin, end - begin);
    position_ = end;
    emit bytesReady(chunk, begin * nsPerByte_);
}

void ReplayDataSource::emitUnlimited()
{
    while (position_ < totalBytes()) {
        const qint64 end = std::min(position_ + chunkBytes_, totalBytes());
        emitChunk(position_, end);
    }
    if (mode_ == ReplayMode::OriginalSpeed) {
        timer_.stop();
    }
    setState(SourceState::Idle, QStringLiteral("replay.finished"),
             QStringLiteral("回放结束"));
    emit replayFinished();
}

void ReplayDataSource::onOriginalSpeedTick()
{
    if (state() != SourceState::Running || mode_ != ReplayMode::OriginalSpeed) {
        return;
    }
    const qint64 elapsedNs = wallClock_.nsecsElapsed();
    const qint64 targetPosition = elapsedNs * bytesPerSecond_ / 1'000'000'000LL;
    if (targetPosition > position_) {
        const qint64 end = std::min(targetPosition, totalBytes());
        emitChunk(position_, end);
    }
    if (position_ >= totalBytes()) {
        timer_.stop();
        setState(SourceState::Idle, QStringLiteral("replay.finished"),
                 QStringLiteral("回放结束"));
        emit replayFinished();
    }
}

bool ReplayDataSource::stepToNextGroupBoundary()
{
    if (position_ >= totalBytes()) {
        return false;
    }
    const qint64 begin = position_;
    stepping_ = true;
    stepGroupBoundary_ = -1;

    while (stepping_ && position_ < totalBytes()) {
        const char byte = rawBytes_.at(position_);
        ++position_;
        stepParser_.appendBytes(QByteArray(1, byte), position_ * nsPerByte_);
    }

    if (stepGroupBoundary_ >= 0 && stepGroupBoundary_ > begin) {
        emit bytesReady(rawBytes_.mid(begin, stepGroupBoundary_ - begin), begin * nsPerByte_);
        emit groupStepped(stepGroupSequence_);
        return true;
    }

    if (position_ > begin) {
        // Trailing bytes without a complete group: emit them so nothing is lost.
        emit bytesReady(rawBytes_.mid(begin, position_ - begin), begin * nsPerByte_);
        return true;
    }

    return false;
}

void ReplayDataSource::setState(SourceState state, QString code, QString message)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_, makeDiagnostic(DiagnosticSeverity::Info,
                                             std::move(code), std::move(message)));
}

void ReplayDataSource::resetReplay()
{
    position_ = 0;
    timer_.stop();
    stepping_ = false;
    stepGroupBoundary_ = -1;
    stepParser_.reset();
    stepGrouper_.reset();
}

}
