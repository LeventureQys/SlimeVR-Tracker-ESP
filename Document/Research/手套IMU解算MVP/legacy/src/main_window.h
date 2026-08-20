#pragma once

#include "demo_data_source.h"
#include "frame_stream_parser.h"
#include "sequence_grouper.h"
#include "serial_data_source.h"
#include "six_imu_solver.h"
#include "slimevr_pose_sender.h"
#include "slimevr_protocol.h"
#include "slimevr_settings.h"
#include "slimevr_udp_client.h"
#include "solver_settings.h"

#include <QElapsedTimer>
#include <QMainWindow>

#include <array>
#include <memory>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSettings;
class QSpinBox;
class QTableWidget;
class QTimer;
class SensorPanel;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QSettings *settingsOverride = nullptr, QWidget *parent = nullptr);
    ~MainWindow() override;
    void setDemoModeEnabled(bool enabled);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void buildInterface();
    void connectSignals();
    void refreshPorts();
    void openSelectedPort();
    void handleDemoMode(bool enabled);
    void openSettingsDialog();
    void applySettings(const SolverSettings &settings);
    void calibrateZero();
    void refreshUi();
    void resetPipeline(const QString &reason);
    void updateControlStates();
    void closeSerialInput();
    bool latestPosesAreValid() const;

    void applySlimeSettingsFromUi();
    void openSlimeMountDialog();
    void applySlimeSettings(const SlimeVrSettings &settings);
    SlimeVrProtocol::HandshakeIdentity buildSlimeIdentity() const;
    void updateSlimeLabels();

    FrameStreamParser parser_;
    SequenceGrouper grouper_;
    SixImuSolver solver_;
    SerialDataSource serialSource_;
    DemoDataSource demoSource_;
    SlimeVrUdpClient slimeClient_;
    SlimeVrPoseSender slimeSender_;
    SlimeVrSettings slimeSettings_;
    std::unique_ptr<QSettings> ownedSettings_;
    QSettings *settings_ = nullptr;
    SolverSettings currentSettings_;
    std::optional<SixImuSnapshot> pendingSnapshot_;
    QElapsedTimer uiClock_;
    qint64 lastSnapshotUiNs_ = 0;

    QComboBox *portComboBox_ = nullptr;
    QPushButton *refreshPortsButton_ = nullptr;
    QPushButton *openPortButton_ = nullptr;
    QPushButton *closePortButton_ = nullptr;
    QCheckBox *demoModeCheckBox_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QPushButton *calibrateZeroButton_ = nullptr;
    QPushButton *clearCalibrationButton_ = nullptr;
    QPushButton *resetStatisticsButton_ = nullptr;
    QLabel *statusLabel_ = nullptr;
    QLabel *latestSequenceLabel_ = nullptr;
    QTableWidget *statisticsTableWidget_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    std::array<SensorPanel *, SixImuProtocol::SensorCount> sensorPanels_{};

    QCheckBox *slimeEnableCheckBox_ = nullptr;
    QComboBox *slimeModeComboBox_ = nullptr;
    QLineEdit *slimeHostLineEdit_ = nullptr;
    QSpinBox *slimePortSpinBox_ = nullptr;
    QComboBox *slimeSideComboBox_ = nullptr;
    QSpinBox *slimeRateSpinBox_ = nullptr;
    QPushButton *slimeApplyButton_ = nullptr;
    QPushButton *slimeMountButton_ = nullptr;
    QLabel *slimeStatusLabel_ = nullptr;
    QLabel *slimeStatsLabel_ = nullptr;
};
