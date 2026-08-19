#pragma once

#include "core/model_data.h"
#include "imu/imu_data.h"
#include "motion/imu_pose.h"

#include <QElapsedTimer>
#include <QMainWindow>

#include <array>
#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

namespace handdemo::render {
class HandRenderWidget;
struct RenderError;
}

namespace handdemo::imu {
class WitBleManager;
class WitProtocolParser;
struct ImuData;
struct DiscoveredDevice;
enum class BleConnectionState;
}

namespace handdemo::motion {
class SingleImuFingerController;
}

namespace handdemo::ui {

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(std::shared_ptr<const handrig::RiggedModel> model,
               std::unique_ptr<motion::PoseSolver> solver,
               std::unique_ptr<motion::ImuPoseMapper> mapper,
               const QStringList &warnings,
               QWidget *parent = nullptr);
    ~MainWindow() override;

    void showStartupError(const QString &summary, const QString &detail);
    void enableSmokeTest();

private:
    void buildInterface();
    void buildRealImuPanel();
    void populateBoneTree();
    void updateSelection(int boneIndex);
    void updateJointEditors();
    void applyManualEditors();
    void applyPose(const motion::PoseResult &pose);
    void updateSimulation();
    void updateRealImu();
    void resetPose();
    void setMode(int index);
    void refreshImuControls();
    void showRenderError(const render::RenderError &error);
    void updateFrameRate();
    bool runSmokeInteractionSequence();

    std::shared_ptr<const handrig::RiggedModel> model_;
    std::unique_ptr<motion::PoseSolver> solver_;
    std::unique_ptr<motion::ImuPoseMapper> mapper_;
    std::unique_ptr<motion::SingleImuFingerController> imuController_;
    motion::PoseResult pose_;
    render::HandRenderWidget *renderWidget_{nullptr};
    QTreeWidget *boneTree_{nullptr};
    QComboBox *modeCombo_{nullptr};
    QWidget *manualPanel_{nullptr};
    QWidget *simulationPanel_{nullptr};
    QWidget *imuPanel_{nullptr};
    std::array<QDoubleSpinBox *, 3> angleEditors_{};
    QLabel *jointNameLabel_{nullptr};
    QLabel *limitLabel_{nullptr};
    QLabel *constraintLabel_{nullptr};
    std::array<QSlider *, 5> curlSliders_{};
    std::array<QSlider *, 5> abductionSliders_{};
    std::array<QCheckBox *, 6> invalidChecks_{};
    QCheckBox *playingCheck_{nullptr};
    QTimer *simulationTimer_{nullptr};
    QPushButton *imuScanButton_{nullptr};
    QComboBox *imuDeviceCombo_{nullptr};
    QPushButton *imuConnectButton_{nullptr};
    QPushButton *imuDisconnectButton_{nullptr};
    QComboBox *fingerBindingCombo_{nullptr};
    QPushButton *calibrateButton_{nullptr};
    QCheckBox *driveCheckbox_{nullptr};
    QLabel *imuStateLabel_{nullptr};
    QLabel *imuAngleLabel_{nullptr};
    QLabel *imuFlexionLabel_{nullptr};
    QLabel *imuBatteryLabel_{nullptr};
    QLabel *imuFrameLabel_{nullptr};
    handdemo::imu::WitBleManager *bleManager_{nullptr};
    handdemo::imu::WitProtocolParser *protocolParser_{nullptr};
    QTimer *imuUpdateTimer_{nullptr};
    handdemo::imu::ImuData latestImuData_{};
    QLabel *modelStatus_{nullptr};
    QLabel *inputStatus_{nullptr};
    QLabel *fpsStatus_{nullptr};
    QVector<QTreeWidgetItem *> boneItems_;
    int selectedBone_{-1};
    qint64 simulationTimestamp_{0};
    QElapsedTimer frameTimer_;
    int frameCount_{0};
    bool updatingEditors_{false};
    bool smokeTest_{false};
    int smokeStep_{0};
};

}
