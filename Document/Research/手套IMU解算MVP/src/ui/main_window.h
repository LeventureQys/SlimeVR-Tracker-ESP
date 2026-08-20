#pragma once

#include "app/runtime_controller.h"

#include <QMainWindow>

#include <array>

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextEdit;
class QDoubleSpinBox;
class QTimer;

namespace handstudio {

class HandRenderWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(RuntimeController *controller, QWidget *parent = nullptr);
    ~MainWindow() override;

    HandRenderWidget *renderWidget() const noexcept;
    RuntimeController *controller() const noexcept;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void applySource();
    void refreshSerialPorts();
    void updateSourceWidgets();
    void updateSourceState(SourceState state, const Diagnostic &diagnostic);
    void updateRecorderState(RecorderState state, const Diagnostic &diagnostic);
    void updateFusedPoses(const std::array<FusedImuPose, 6> &poses);
    void updateObservation(const HandObservationFrame &observation);
    void appendDiagnostic(const Diagnostic &diagnostic);
    void chooseRecordingDirectory();
    void toggleRecordingPause();

private:
    void buildUi();

    RuntimeController *controller_ = nullptr;
    HandRenderWidget *renderWidget_ = nullptr;
    QComboBox *sourceCombo_ = nullptr;
    QComboBox *portCombo_ = nullptr;
    QLineEdit *sourceDetailEdit_ = nullptr;
    QDoubleSpinBox *replaySpeedSpin_ = nullptr;
    QPushButton *connectButton_ = nullptr;
    QPushButton *refreshPortsButton_ = nullptr;
    QPushButton *pauseReplayButton_ = nullptr;
    QPushButton *stepButton_ = nullptr;
    QPushButton *recordButton_ = nullptr;
    QPushButton *recordPauseButton_ = nullptr;
    QPushButton *recordStopButton_ = nullptr;
    QLabel *sourceStateLabel_ = nullptr;
    QLabel *recordingStateLabel_ = nullptr;
    QLabel *statisticsLabel_ = nullptr;
    QTableWidget *sensorTable_ = nullptr;
    QTableWidget *fingerTable_ = nullptr;
    QTextEdit *diagnosticsEdit_ = nullptr;
    QString recordingDirectory_;
    QTimer *portRefreshTimer_ = nullptr;
    QFormLayout *sourceLayout_ = nullptr;
    int portRowIndex_ = -1;
    int detailRowIndex_ = -1;
    int speedRowIndex_ = -1;
};

}
