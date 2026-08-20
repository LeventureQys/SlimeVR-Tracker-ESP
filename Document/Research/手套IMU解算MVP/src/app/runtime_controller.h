#pragma once

#include "core/fusion_types.h"
#include "core/hand_observation_types.h"
#include "core/hand_skeleton_frame.h"
#include "input/idata_source.h"
#include "model/model_data.h"
#include "recording/session_recorder.h"

#include <QObject>
#include <QThread>

#include <array>
#include <memory>

namespace handstudio {

struct RuntimeOptions {
    QString source = QStringLiteral("demo");
    QString port;
    QString replayPath;
    QString modelPath;
    QString rigPath;
    QString configPath;
};

class RuntimeWorker;

class RuntimeController final : public QObject {
    Q_OBJECT

public:
    explicit RuntimeController(QObject *parent = nullptr);
    ~RuntimeController() override;

    bool initialize(const RuntimeOptions &options, QString *errorMessage = nullptr);
    void start();
    void stop();
    bool isWorkerRunning() const noexcept;
    SourceState sourceState() const noexcept;
    RecorderState recorderState() const noexcept;
    QString sourceName() const;
    QString modelPath() const;
    QString lastError() const;

public slots:
    void selectSource(const QString &source, const QString &detail = {});
    void setReplayPaused(bool paused);
    void stepReplayGroup();
    void beginRestBiasCalibration();
    void zeroHandPose();
    bool startRecording(const QString &directory);
    void pauseRecording();
    void resumeRecording();
    void stopRecording();

signals:
    void modelReady(std::shared_ptr<const RiggedModel> model);
    void sourceStateChanged(SourceState state, const Diagnostic &diagnostic);
    void recorderStateChanged(RecorderState state, const Diagnostic &diagnostic);
    void fusedPosesReady(const std::array<FusedImuPose, 6> &poses);
    void observationReady(const HandObservationFrame &observation);
    void skeletonFrameReady(const HandSkeletonFrame &frame);
    void diagnosticsReady(const Diagnostic &diagnostic);
    void statisticsChanged(quint64 groups, qint64 latencyNs, quint64 droppedUiFrames);
    void stopped();

private:
    void shutdownWorker();

    RuntimeOptions options_;
    QThread workerThread_;
    RuntimeWorker *worker_ = nullptr;
    SourceState sourceState_ = SourceState::Idle;
    RecorderState recorderState_ = RecorderState::Idle;
    QString sourceName_;
    QString modelPath_;
    QString lastError_;
};

}
