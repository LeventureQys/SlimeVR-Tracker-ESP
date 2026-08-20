#include "recording/replay_controller.h"

#include <QDir>

namespace handstudio {

ReplayController::ReplayController(QObject *parent)
    : QObject(parent)
    , dataSource_(this)
    , parser_(this)
    , grouper_(DefaultPendingGroupLimit, this)
{
    connect(&dataSource_, &ReplayDataSource::bytesReady,
            &parser_, &FrameStreamParser::appendBytes);
    connect(&parser_, &FrameStreamParser::frameParsed,
            &grouper_, &SequenceGrouper::addFrame);
    connect(&grouper_, &SequenceGrouper::groupReady,
            this, [this](const SixImuSampleGroup &group) {
                groupSequences_.append(group.sequence);
                emit groupReady(group);
            });
    connect(&dataSource_, &ReplayDataSource::replayFinished,
            this, &ReplayController::replayFinished);
}

bool ReplayController::loadSession(const QString &recordingDir, QString *errorMessage)
{
    groupSequences_.clear();
    parser_.reset();
    grouper_.reset();

    const auto metadataResult = readMetadataJson(recordingDir + QStringLiteral("/metadata.json"));
    if (!metadataResult.success) {
        if (errorMessage && !metadataResult.diagnostics.isEmpty()) {
            *errorMessage = metadataResult.diagnostics.first().message;
        } else if (errorMessage) {
            *errorMessage = QStringLiteral("无法读取 metadata.json");
        }
        hasSession_ = false;
        return false;
    }
    metadata_ = metadataResult.metadata;

    if (!dataSource_.load(recordingDir + QStringLiteral("/raw.bin"), errorMessage)) {
        hasSession_ = false;
        return false;
    }

    if (!metadata_.rawSha256.isEmpty()) {
        const QString actualHash = computeFileSha256Hex(recordingDir + QStringLiteral("/raw.bin"));
        if (actualHash.compare(metadata_.rawSha256, Qt::CaseInsensitive) != 0) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("raw.bin 哈希不匹配 metadata.json");
            }
            hasSession_ = false;
            return false;
        }
    }

    hasSession_ = true;
    return true;
}

bool ReplayController::hasSession() const
{
    return hasSession_;
}

RecordingMetadata ReplayController::metadata() const
{
    return metadata_;
}

ReplayDataSource *ReplayController::dataSource()
{
    return &dataSource_;
}

const ReplayDataSource *ReplayController::dataSource() const
{
    return &dataSource_;
}

void ReplayController::playOriginalSpeed()
{
    if (!hasSession_) {
        return;
    }
    dataSource_.setMode(ReplayMode::OriginalSpeed);
    dataSource_.start();
}

void ReplayController::playUnlimited()
{
    if (!hasSession_) {
        return;
    }
    dataSource_.setMode(ReplayMode::Unlimited);
    dataSource_.start();
}

void ReplayController::pause()
{
    dataSource_.pause();
}

void ReplayController::resume()
{
    dataSource_.resume();
}

void ReplayController::stop()
{
    dataSource_.stop();
}

void ReplayController::stepGroup()
{
    if (!hasSession_) {
        return;
    }
    dataSource_.setMode(ReplayMode::StepByGroup);
    dataSource_.stepGroup();
}

QVector<quint8> ReplayController::groupSequences() const
{
    return groupSequences_;
}

GroupStatistics ReplayController::groupStatistics() const
{
    return grouper_.statistics();
}

ParserStatistics ReplayController::parserStatistics() const
{
    return parser_.statistics();
}

}
