#include "app/runtime_controller.h"

#include "app/demo_data_source.h"
#include "calibration/calibration_pipeline.h"
#include "fusion/fusion_bank.h"
#include "hand/hand_observation_solver.h"
#include "input/replay_data_source.h"
#include "input/serial_data_source.h"
#include "model/model_importer.h"
#include "protocol/frame_stream_parser.h"
#include "protocol/sequence_grouper.h"
#include "skeleton/kinematic_skeleton.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>

#include <optional>
#include <utility>

namespace handstudio {
namespace {

QString replayRawPath(const QString &path)
{
    const QFileInfo info(path);
    return info.isDir() ? QDir(path).filePath(QStringLiteral("raw.bin")) : path;
}

QJsonArray quaternionJson(const QQuaternion &value)
{
    return {value.scalar(), value.x(), value.y(), value.z()};
}

QJsonArray matrixJson(const QMatrix4x4 &value)
{
    QJsonArray output;
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) output.append(value(row, column));
    }
    return output;
}

QByteArray jsonLine(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

}

class RuntimeWorker final : public QObject {
    Q_OBJECT
public:
    explicit RuntimeWorker(RuntimeOptions options)
        : options_(std::move(options))
    {
        connect(&parser_, &FrameStreamParser::frameParsed, &grouper_, &SequenceGrouper::addFrame);
        connect(&grouper_, &SequenceGrouper::groupReady, this, &RuntimeWorker::processGroup);
        connect(&recorder_, &SessionRecorder::stateChanged, this, &RuntimeWorker::recorderStateChanged);
    }

public slots:
    void initialize()
    {
        ModelImporter importer;
        auto loaded = importer.load(options_.modelPath);
        if (!loaded.hasValue()) {
            const QString message = loaded.error ? loaded.error->message : QStringLiteral("模型加载失败");
            emit failed(message);
            return;
        }
        QFile rigFile(options_.rigPath);
        if (!rigFile.open(QIODevice::ReadOnly)) {
            emit failed(QStringLiteral("无法打开骨骼配置: %1").arg(options_.rigPath));
            return;
        }
        auto rigResult = loadHandRigConfig(rigFile.readAll(), *loaded.value);
        if (!rigResult.config) {
            emit failed(QStringLiteral("骨骼配置无效: %1").arg(rigResult.errors.join(QStringLiteral("; "))));
            return;
        }
        model_ = std::make_shared<RiggedModel>(*loaded.value);
        std::array<FingerObservationConfig, 5> fingerConfigs;
        for (int index = 0; index < 5; ++index) {
            fingerConfigs[std::size_t(index)].sensorId = AllSensorIds[std::size_t(index + 1)];
            fingerConfigs[std::size_t(index)].mountOrientation = rigResult.config->mountOrientations[std::size_t(index)];
        }
        observationSolver_ = std::make_unique<HandObservationSolver>(fingerConfigs, rigResult.config->handSide);
        skeleton_ = std::make_unique<KinematicSkeleton>(*model_, *rigResult.config);
        if (!skeleton_->isValid()) {
            emit failed(skeleton_->errorString());
            return;
        }
        emit modelReady(model_);
        selectSource(options_.source, options_.source == QStringLiteral("serial") ? options_.port : options_.replayPath);
        emit initialized();
    }

    void start()
    {
        if (source_) source_->start();
    }

    void stop()
    {
        if (source_) {
            disconnect(source_, nullptr, this, nullptr);
            source_->stop();
            delete source_;
            source_ = nullptr;
        }
        recorder_.stop();
        emit stopped();
    }

    void selectSource(QString sourceName, QString detail)
    {
        if (source_) {
            disconnect(source_, nullptr, this, nullptr);
            source_->stop();
            delete source_;
            source_ = nullptr;
        }
        parser_.reset();
        grouper_.reset();
        calibration_.reset();
        fusion_.reset();
        if (observationSolver_) observationSolver_->reset();

        if (sourceName == QStringLiteral("serial")) {
            auto *serial = new SerialDataSource;
            serial->setPortName(detail);
            source_ = serial;
        } else if (sourceName == QStringLiteral("replay")) {
            auto *replay = new ReplayDataSource;
            QString error;
            if (!replay->load(replayRawPath(detail), &error)) {
                delete replay;
                emit failed(error);
                return;
            }
            replay->setMode(ReplayMode::Unlimited);
            source_ = replay;
        } else {
            sourceName = QStringLiteral("demo");
            source_ = new DemoDataSource;
        }
        source_->setParent(this);
        activeSourceName_ = sourceName;
        connect(source_, &IDataSource::bytesReady, this, &RuntimeWorker::consumeBytes);
        connect(source_, &IDataSource::stateChanged, this, &RuntimeWorker::sourceStateChanged);
        emit sourceSelected(sourceName);
    }

    void setReplayPaused(bool paused)
    {
        if (auto *replay = qobject_cast<ReplayDataSource *>(source_)) paused ? replay->pause() : replay->resume();
    }

    void stepReplayGroup()
    {
        if (auto *replay = qobject_cast<ReplayDataSource *>(source_)) {
            replay->setMode(ReplayMode::StepByGroup);
            replay->stepGroup();
        }
    }

    void beginRestBiasCalibration() { calibration_.beginRestBiasEstimation(); }

    void zeroHandPose()
    {
        if (!observationSolver_ || !latestPoses_.has_value()) {
            emit diagnosticReady({DiagnosticSeverity::Warning, QStringLiteral("calibration.neutral.failed"),
                                  QStringLiteral("调零失败：尚无有效姿态"), {}});
            return;
        }
        if (observationSolver_->setNeutral(*latestPoses_)) {
            emit diagnosticReady({DiagnosticSeverity::Info, QStringLiteral("calibration.neutral"),
                                  QStringLiteral("调零成功：当前姿态已设为中心位"), {}});
        } else {
            emit diagnosticReady({DiagnosticSeverity::Warning, QStringLiteral("calibration.neutral.failed"),
                                  QStringLiteral("调零失败：六路姿态未全部有效"), {}});
        }
    }

    void startRecording(QString directory)
    {
        RecordingMetadata metadata;
        metadata.sessionId = QFileInfo(directory).fileName();
        metadata.devicePort = options_.port;
        metadata.baudRate = 921600;
        metadata.nominalSampleRateHz = 200.0;
        metadata.configVersion = QStringLiteral("v2.0.0");
        metadata.actionDescription = activeSourceName_;
        Diagnostic error;
        if (!recorder_.startRecording(directory, metadata, &error)) emit diagnosticReady(error);
    }

    void pauseRecording() { recorder_.pause(); }
    void resumeRecording() { recorder_.resume(); }
    void stopRecording() { recorder_.stop(); }

signals:
    void initialized();
    void failed(const QString &message);
    void modelReady(std::shared_ptr<const RiggedModel> model);
    void sourceSelected(const QString &source);
    void sourceStateChanged(SourceState state, const Diagnostic &diagnostic);
    void recorderStateChanged(RecorderState state, const Diagnostic &diagnostic);
    void fusedPosesReady(const std::array<FusedImuPose, 6> &poses);
    void observationReady(const HandObservationFrame &observation);
    void skeletonFrameReady(const HandSkeletonFrame &frame);
    void diagnosticReady(const Diagnostic &diagnostic);
    void statisticsChanged(quint64 groups, qint64 latencyNs, quint64 droppedUiFrames);
    void stopped();

private slots:
    void consumeBytes(const QByteArray &bytes, qint64 monotonicNs)
    {
        recorder_.appendRawBytes(bytes);
        parser_.appendBytes(bytes, monotonicNs);
    }

    void processGroup(const SixImuSampleGroup &group)
    {
        const auto calibrated = calibration_.calibrateGroup(group);
        const auto poses = fusion_.processGroup(calibrated);
        latestPoses_ = poses;
        const HandObservationFrame observation = observationSolver_->solve(poses);
        HandSkeletonFrame frame = skeleton_->solve(observation);
        ++groupCount_;
        const qint64 latency = std::max<qint64>(0, frame.timestampNs - group.emittedMonotonicNs);
        recorder_.appendFusedPoses(serializePoses(poses));
        recorder_.appendObservation(serializeObservation(observation));
        recorder_.appendSkeletonFrame(serializeSkeleton(frame));
        for (const Diagnostic &diagnostic : calibration_.takeDiagnostics()) publishDiagnostic(diagnostic);
        for (const Diagnostic &diagnostic : fusion_.takeDiagnostics()) publishDiagnostic(diagnostic);
        for (const Diagnostic &diagnostic : frame.diagnostics) publishDiagnostic(diagnostic);
        emit fusedPosesReady(poses);
        emit observationReady(observation);
        emit skeletonFrameReady(frame);
        emit statisticsChanged(groupCount_, latency, 0);
    }

private:
    QByteArray serializePoses(const std::array<FusedImuPose, 6> &poses) const
    {
        QJsonArray items;
        for (const auto &pose : poses) {
            QJsonObject object;
            object.insert(QStringLiteral("sensor"), int(pose.sensorId));
            object.insert(QStringLiteral("sequence"), int(pose.sequence));
            object.insert(QStringLiteral("timestampNs"), double(pose.timestampNs));
            object.insert(QStringLiteral("orientationWxyz"), quaternionJson(pose.worldOrientation));
            object.insert(QStringLiteral("valid"), pose.valid);
            object.insert(QStringLiteral("confidence"), pose.confidence);
            items.append(object);
        }
        return jsonLine({{QStringLiteral("poses"), items}});
    }

    QByteArray serializeObservation(const HandObservationFrame &observation) const
    {
        QJsonArray fingers;
        for (const auto &finger : observation.fingers) {
            fingers.append(QJsonObject{{QStringLiteral("sensor"), int(finger.sensorId)},
                                       {QStringLiteral("flexionDegrees"), finger.flexionDegrees},
                                       {QStringLiteral("abductionDegrees"), finger.abductionDegrees},
                                       {QStringLiteral("twistDegrees"), finger.twistDegrees},
                                       {QStringLiteral("valid"), finger.valid},
                                       {QStringLiteral("confidence"), finger.confidence}});
        }
        return jsonLine({{QStringLiteral("sequence"), int(observation.sequence)},
                         {QStringLiteral("timestampNs"), double(observation.timestampNs)},
                         {QStringLiteral("wristWxyz"), quaternionJson(observation.wristWorldOrientation)},
                         {QStringLiteral("fingers"), fingers}});
    }

    QByteArray serializeSkeleton(const HandSkeletonFrame &frame) const
    {
        QJsonArray bones;
        for (const auto &bone : frame.bones) {
            bones.append(QJsonObject{{QStringLiteral("name"), bone.boneName},
                                     {QStringLiteral("globalMatrixRowMajor"), matrixJson(bone.globalMatrix)},
                                     {QStringLiteral("skinMatrixRowMajor"), matrixJson(bone.skinMatrix)},
                                     {QStringLiteral("source"), int(bone.source)},
                                     {QStringLiteral("confidence"), bone.confidence}});
        }
        return jsonLine({{QStringLiteral("sequence"), int(frame.sequence)},
                         {QStringLiteral("timestampNs"), double(frame.timestampNs)},
                         {QStringLiteral("rootTransformRowMajor"), matrixJson(frame.rootTransform)},
                         {QStringLiteral("bones"), bones}});
    }

    void publishDiagnostic(const Diagnostic &diagnostic)
    {
        recorder_.appendDiagnostic(diagnostic);
        emit diagnosticReady(diagnostic);
    }

    RuntimeOptions options_;
    IDataSource *source_ = nullptr;
    QString activeSourceName_;
    FrameStreamParser parser_;
    SequenceGrouper grouper_;
    CalibrationPipeline calibration_;
    FusionBank fusion_;
    std::unique_ptr<HandObservationSolver> observationSolver_;
    std::unique_ptr<KinematicSkeleton> skeleton_;
    std::shared_ptr<RiggedModel> model_;
    SessionRecorder recorder_;
    quint64 groupCount_ = 0;
    std::optional<std::array<FusedImuPose, 6>> latestPoses_;
};

RuntimeController::RuntimeController(QObject *parent)
    : QObject(parent)
{
}

RuntimeController::~RuntimeController() { shutdownWorker(); }

bool RuntimeController::initialize(const RuntimeOptions &options, QString *errorMessage)
{
    shutdownWorker();
    options_ = options;
    modelPath_ = options.modelPath;
    worker_ = new RuntimeWorker(options_);
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &RuntimeWorker::modelReady, this, &RuntimeController::modelReady, Qt::QueuedConnection);
    connect(worker_, &RuntimeWorker::sourceSelected, this, [this](const QString &source) { sourceName_ = source; });
    connect(worker_, &RuntimeWorker::sourceStateChanged, this, [this](SourceState state, const Diagnostic &diagnostic) {
        sourceState_ = state;
        if (state == SourceState::Error) qWarning().noquote() << diagnostic.code << diagnostic.message << diagnostic.detail;
        emit sourceStateChanged(state, diagnostic);
    });
    connect(worker_, &RuntimeWorker::recorderStateChanged, this, [this](RecorderState state, const Diagnostic &diagnostic) {
        recorderState_ = state;
        emit recorderStateChanged(state, diagnostic);
    });
    connect(worker_, &RuntimeWorker::fusedPosesReady, this, &RuntimeController::fusedPosesReady, Qt::QueuedConnection);
    connect(worker_, &RuntimeWorker::observationReady, this, &RuntimeController::observationReady, Qt::QueuedConnection);
    connect(worker_, &RuntimeWorker::skeletonFrameReady, this, &RuntimeController::skeletonFrameReady, Qt::QueuedConnection);
    connect(worker_, &RuntimeWorker::diagnosticReady, this, &RuntimeController::diagnosticsReady, Qt::QueuedConnection);
    connect(worker_, &RuntimeWorker::statisticsChanged, this, &RuntimeController::statisticsChanged, Qt::QueuedConnection);
    connect(worker_, &RuntimeWorker::failed, this, [this](const QString &message) { lastError_ = message; });
    workerThread_.start();
    QMetaObject::invokeMethod(worker_, "initialize", Qt::BlockingQueuedConnection);
    if (!lastError_.isEmpty()) {
        if (errorMessage) *errorMessage = lastError_;
        shutdownWorker();
        return false;
    }
    return true;
}

void RuntimeController::start() { if (worker_) QMetaObject::invokeMethod(worker_, "start", Qt::QueuedConnection); }

void RuntimeController::stop()
{
    shutdownWorker();
    emit stopped();
}

bool RuntimeController::isWorkerRunning() const noexcept { return workerThread_.isRunning(); }
SourceState RuntimeController::sourceState() const noexcept { return sourceState_; }
RecorderState RuntimeController::recorderState() const noexcept { return recorderState_; }
QString RuntimeController::sourceName() const { return sourceName_; }
QString RuntimeController::modelPath() const { return modelPath_; }
QString RuntimeController::lastError() const { return lastError_; }

void RuntimeController::selectSource(const QString &source, const QString &detail)
{
    if (!worker_) return;
    QMetaObject::invokeMethod(worker_, "selectSource", Qt::QueuedConnection,
                              Q_ARG(QString, source), Q_ARG(QString, detail));
}

void RuntimeController::setReplayPaused(bool paused)
{
    if (worker_) QMetaObject::invokeMethod(worker_, "setReplayPaused", Qt::QueuedConnection, Q_ARG(bool, paused));
}

void RuntimeController::stepReplayGroup() { if (worker_) QMetaObject::invokeMethod(worker_, "stepReplayGroup", Qt::QueuedConnection); }
void RuntimeController::beginRestBiasCalibration() { if (worker_) QMetaObject::invokeMethod(worker_, "beginRestBiasCalibration", Qt::QueuedConnection); }
void RuntimeController::zeroHandPose() { if (worker_) QMetaObject::invokeMethod(worker_, "zeroHandPose", Qt::QueuedConnection); }

bool RuntimeController::startRecording(const QString &directory)
{
    if (!worker_) return false;
    QMetaObject::invokeMethod(worker_, "startRecording", Qt::QueuedConnection, Q_ARG(QString, directory));
    return true;
}
void RuntimeController::pauseRecording() { if (worker_) QMetaObject::invokeMethod(worker_, "pauseRecording", Qt::QueuedConnection); }
void RuntimeController::resumeRecording() { if (worker_) QMetaObject::invokeMethod(worker_, "resumeRecording", Qt::QueuedConnection); }
void RuntimeController::stopRecording() { if (worker_) QMetaObject::invokeMethod(worker_, "stopRecording", Qt::QueuedConnection); }

void RuntimeController::shutdownWorker()
{
    if (!workerThread_.isRunning()) {
        worker_ = nullptr;
        return;
    }
    if (worker_) QMetaObject::invokeMethod(worker_, "stop", Qt::BlockingQueuedConnection);
    workerThread_.quit();
    workerThread_.wait();
    worker_ = nullptr;
    sourceState_ = SourceState::Idle;
    recorderState_ = RecorderState::Idle;
}

}

#include "runtime_controller.moc"
