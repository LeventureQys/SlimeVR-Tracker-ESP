#pragma once

#include "core/diagnostic.h"
#include "core/fusion_types.h"
#include "imu_types.h"
#include "madgwick_filter.h"
#include "solver_settings.h"

#include <QObject>
#include <QQuaternion>
#include <QString>
#include <QVector>

#include <array>
#include <optional>

struct EulerAngles {
    double rollDegrees = 0.0;
    double pitchDegrees = 0.0;
    double yawDegrees = 0.0;
};

struct SensorPose {
    SensorId sensorId = SensorId::Wrist;
    QQuaternion worldOrientation;
    QQuaternion relativeOrientation;
    EulerAngles relativeEuler;
    FusionMode mode = FusionMode::Invalid;
    bool valid = false;
    bool calibrated = false;
    bool sourceAllZero = false;
    quint8 sequence = 0;
    qint64 updatedMonotonicNs = 0;
    QString status;
};

struct SixImuSnapshot {
    quint8 sequence = 0;
    std::array<ImuFrame, 6> rawFrames;
    std::array<SensorPose, 6> poses;
    qint64 updatedMonotonicNs = 0;
};

class SixImuSolver final : public QObject {
    Q_OBJECT
public:
    explicit SixImuSolver(QObject *parent = nullptr);

    SolverSettings settings() const;
    void applySettings(const SolverSettings &settings);
    void processCompleteGroup(const ImuSampleGroup &group);
    bool calibrateZero(QString *errorMessage = nullptr);
    void clearCalibration();
    void reset();
    std::optional<SixImuSnapshot> latestSnapshot() const;

    // Structured diagnostics for rejected settings / groups (no silent return).
    QVector<handstudio::Diagnostic> takeDiagnostics();

    // Orchestration adapter: expose the latest snapshot in the unified
    // FusedImuPose contract consumed by downstream layers.
    std::optional<std::array<handstudio::FusedImuPose, 6>> latestFusedPoses() const;

signals:
    void snapshotReady(const SixImuSnapshot &snapshot);
    void stateReset(const QString &reason);

private:
    void resetState(const QString &reason);

    SolverSettings settings_;
    std::array<MadgwickFilter, 6> filters_;
    std::array<QQuaternion, 6> zeroOrientations_;
    bool calibrated_ = false;
    qint64 lastGroupMonotonicNs_ = 0;
    std::optional<SixImuSnapshot> latestSnapshot_;
    QVector<handstudio::Diagnostic> diagnostics_;
};
