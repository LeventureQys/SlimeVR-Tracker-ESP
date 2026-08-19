#include "main_window.h"

#include "imu/wit_ble_manager.h"
#include "imu/wit_protocol_parser.h"
#include "motion/single_imu_finger_controller.h"
#include "render/hand_render_widget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDebug>
#include <QtMath>

namespace handdemo::ui {

namespace {

QString vectorText(const QVector3D &value)
{
    return QStringLiteral("X %1°  Y %2°  Z %3°")
        .arg(value.x(), 0, 'f', 1).arg(value.y(), 0, 'f', 1).arg(value.z(), 0, 'f', 1);
}

}

MainWindow::MainWindow(std::shared_ptr<const handrig::RiggedModel> model,
                       std::unique_ptr<motion::PoseSolver> solver,
                       std::unique_ptr<motion::ImuPoseMapper> mapper,
                       const QStringList &warnings, QWidget *parent)
    : QMainWindow(parent), model_(std::move(model)), solver_(std::move(solver)), mapper_(std::move(mapper))
{
    if (solver_) {
        imuController_ = std::make_unique<motion::SingleImuFingerController>(*solver_);
    }
    bleManager_ = new imu::WitBleManager(this);
    protocolParser_ = new imu::WitProtocolParser(this);

    buildInterface();
    setWindowTitle(QStringLiteral("可活动骨架手模 Demo"));
    resize(1420, 900);
    if (model_ && solver_) {
        pose_ = solver_->solve(solver_->bindPose());
        renderWidget_->setModel(model_);
        applyPose(pose_);
        populateBoneTree();
        setMode(0);
        modelStatus_->setText(QStringLiteral("模型：%1 网格 / %2 骨骼")
                                  .arg(model_->meshes.size()).arg(model_->bones.size()));
        if (!warnings.isEmpty()) {
            inputStatus_->setText(QStringLiteral("警告：%1").arg(warnings.join(QStringLiteral("；"))));
        }
    } else {
        modelStatus_->setText(QStringLiteral("模型未加载"));
        modeCombo_->setEnabled(false);
    }
    frameTimer_.start();
}

MainWindow::~MainWindow()
{
    simulationTimer_->stop();
    if (imuUpdateTimer_) imuUpdateTimer_->stop();
    if (bleManager_) bleManager_->disconnectDevice();
}

void MainWindow::buildInterface()
{
    renderWidget_ = new render::HandRenderWidget(this);
    setCentralWidget(renderWidget_);

    auto *treeDock = new QDockWidget(QStringLiteral("骨骼树"), this);
    boneTree_ = new QTreeWidget(treeDock);
    boneTree_->setHeaderLabels({QStringLiteral("骨骼"), QStringLiteral("状态")});
    treeDock->setWidget(boneTree_);
    addDockWidget(Qt::LeftDockWidgetArea, treeDock);

    auto *propertiesDock = new QDockWidget(QStringLiteral("姿态与约束"), this);
    auto *properties = new QWidget(propertiesDock);
    auto *propertiesLayout = new QVBoxLayout(properties);
    modeCombo_ = new QComboBox(properties);
    modeCombo_->addItems({QStringLiteral("手动关节"), QStringLiteral("六路 IMU 模拟"), QStringLiteral("真实单 IMU")});
    propertiesLayout->addWidget(modeCombo_);

    manualPanel_ = new QGroupBox(QStringLiteral("关节角度"), properties);
    auto *manualLayout = new QFormLayout(manualPanel_);
    jointNameLabel_ = new QLabel(QStringLiteral("未选择"), manualPanel_);
    manualLayout->addRow(QStringLiteral("关节"), jointNameLabel_);
    const std::array<QString, 3> axisNames{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    for (int axis = 0; axis < 3; ++axis) {
        angleEditors_[axis] = new QDoubleSpinBox(manualPanel_);
        angleEditors_[axis]->setRange(-360.0, 360.0);
        angleEditors_[axis]->setSuffix(QStringLiteral("°"));
        angleEditors_[axis]->setSingleStep(1.0);
        manualLayout->addRow(axisNames[axis], angleEditors_[axis]);
        connect(angleEditors_[axis], &QDoubleSpinBox::valueChanged, this, [this] { applyManualEditors(); });
    }
    limitLabel_ = new QLabel(QStringLiteral("限位：--"), manualPanel_);
    limitLabel_->setWordWrap(true);
    constraintLabel_ = new QLabel(QStringLiteral("状态：--"), manualPanel_);
    manualLayout->addRow(limitLabel_);
    manualLayout->addRow(constraintLabel_);
    propertiesLayout->addWidget(manualPanel_);

    simulationPanel_ = new QGroupBox(QStringLiteral("六路姿态模拟"), properties);
    auto *simulationLayout = new QVBoxLayout(simulationPanel_);
    auto *notice = new QLabel(QStringLiteral("六路姿态耦合近似，非真实关节角、非医学用途"), simulationPanel_);
    notice->setWordWrap(true);
    simulationLayout->addWidget(notice);
    const std::array<QString, 5> fingerNames{QStringLiteral("拇指"), QStringLiteral("食指"),
                                             QStringLiteral("中指"), QStringLiteral("无名指"),
                                             QStringLiteral("小指")};
    for (int finger = 0; finger < 5; ++finger) {
        auto *row = new QWidget(simulationPanel_);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(new QLabel(fingerNames[finger], row));
        curlSliders_[finger] = new QSlider(Qt::Horizontal, row);
        curlSliders_[finger]->setRange(0, 100);
        rowLayout->addWidget(curlSliders_[finger], 1);
        rowLayout->addWidget(new QLabel(QStringLiteral("张合"), row));
        abductionSliders_[finger] = new QSlider(Qt::Horizontal, row);
        abductionSliders_[finger]->setRange(-100, 100);
        abductionSliders_[finger]->setValue(0);
        rowLayout->addWidget(abductionSliders_[finger], 1);
        invalidChecks_[finger + 1] = new QCheckBox(QStringLiteral("无效"), row);
        rowLayout->addWidget(invalidChecks_[finger + 1]);
        simulationLayout->addWidget(row);
        connect(curlSliders_[finger], &QSlider::valueChanged, this, [this] { updateSimulation(); });
        connect(abductionSliders_[finger], &QSlider::valueChanged, this, [this] { updateSimulation(); });
        connect(invalidChecks_[finger + 1], &QCheckBox::toggled, this, [this] { updateSimulation(); });
    }
    invalidChecks_[0] = new QCheckBox(QStringLiteral("注入无效掌心样本"), simulationPanel_);
    playingCheck_ = new QCheckBox(QStringLiteral("播放 60 Hz 模拟"), simulationPanel_);
    simulationLayout->addWidget(invalidChecks_[0]);
    simulationLayout->addWidget(playingCheck_);
    propertiesLayout->addWidget(simulationPanel_);
    buildRealImuPanel();
    propertiesLayout->addWidget(imuPanel_);
    propertiesLayout->addStretch();
    propertiesDock->setWidget(properties);
    addDockWidget(Qt::RightDockWidgetArea, propertiesDock);

    auto *toolbar = addToolBar(QStringLiteral("操作"));
    auto *resetAction = toolbar->addAction(QStringLiteral("重置姿态"));
    auto *cameraAction = toolbar->addAction(QStringLiteral("适配视角"));

    modelStatus_ = new QLabel(this);
    inputStatus_ = new QLabel(QStringLiteral("输入：就绪"), this);
    fpsStatus_ = new QLabel(QStringLiteral("FPS：--"), this);
    statusBar()->addWidget(modelStatus_, 1);
    statusBar()->addPermanentWidget(inputStatus_, 2);
    statusBar()->addPermanentWidget(fpsStatus_);

    simulationTimer_ = new QTimer(this);
    simulationTimer_->setInterval(16);
    connect(simulationTimer_, &QTimer::timeout, this, [this] {
        if (playingCheck_->isChecked()) {
            for (int finger = 0; finger < 5; ++finger) {
                const int phase = static_cast<int>((simulationTimestamp_ / 16667 + finger * 13) % 200);
                curlSliders_[finger]->setValue(phase <= 100 ? phase : 200 - phase);
            }
        }
        updateSimulation();
    });
    connect(bleManager_, &imu::WitBleManager::deviceDiscovered, this, [this](const imu::DiscoveredDevice &device) {
        const QString label = QStringLiteral("%1 (%2)").arg(device.name, device.address);
        imuDeviceCombo_->addItem(label);
        imuDeviceCombo_->setItemData(imuDeviceCombo_->count() - 1, QVariant::fromValue(device.info), Qt::UserRole);
    });
    connect(bleManager_, &imu::WitBleManager::scanReset, this, [this] { imuDeviceCombo_->clear(); });
    connect(bleManager_, &imu::WitBleManager::stateChanged, this,
            [this](imu::BleConnectionState state, const QString &message) {
                imuStateLabel_->setText(message);
                if (state == imu::BleConnectionState::Idle && imuController_) {
                    imuController_->setConnected(false);
                    refreshImuControls();
                }
                if (state == imu::BleConnectionState::Connected) {
                    if (imuController_) imuController_->setConnected(true);
                    refreshImuControls();
                }
            });
    connect(bleManager_, &imu::WitBleManager::notificationReceived, protocolParser_,
            &imu::WitProtocolParser::appendBytes);
    connect(protocolParser_, &imu::WitProtocolParser::dataUpdated, this,
            [this](const imu::ImuData &data) {
                latestImuData_ = data;
                imuAngleLabel_->setText(QStringLiteral("角度 X%1° Y%2° Z%3°")
                    .arg(data.angleX, 0, 'f', 1).arg(data.angleY, 0, 'f', 1).arg(data.angleZ, 0, 'f', 1));
                imuBatteryLabel_->setText(QStringLiteral("电量 %1%  温度 %2°C").arg(data.batteryPercent, 0, 'f', 0)
                    .arg(data.temperatureCelsius, 0, 'f', 1));
                imuFrameLabel_->setText(QStringLiteral("帧 %1 (姿态 %2)").arg(data.frameCount).arg(data.motionFrameCount));
            });
    connect(modeCombo_, &QComboBox::currentIndexChanged, this, &MainWindow::setMode);

    imuUpdateTimer_ = new QTimer(this);
    imuUpdateTimer_->setInterval(16);
    connect(imuUpdateTimer_, &QTimer::timeout, this, &MainWindow::updateRealImu);
    connect(boneTree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item) { if (item) updateSelection(item->data(0, Qt::UserRole).toInt()); });
    connect(renderWidget_, &render::HandRenderWidget::boneSelected, this, &MainWindow::updateSelection);
    connect(renderWidget_, &render::HandRenderWidget::localRotationDeltaRequested, this,
            [this](int boneIndex, const QVector3D &delta) {
                if (!solver_ || modeCombo_->currentIndex() != 0 || boneIndex < 0) return;
                const QVector3D next = pose_.pose.localPoses[boneIndex].eulerDegrees + delta;
                applyPose(solver_->applyJoint(pose_.pose, model_->bones[boneIndex].name, next));
            });
    connect(renderWidget_, &render::HandRenderWidget::renderFailed, this, &MainWindow::showRenderError);
    connect(renderWidget_, &render::HandRenderWidget::frameRendered, this, &MainWindow::updateFrameRate);
    connect(resetAction, &QAction::triggered, this, &MainWindow::resetPose);
    connect(cameraAction, &QAction::triggered, renderWidget_, &render::HandRenderWidget::resetCamera);
    connect(invalidChecks_[0], &QCheckBox::toggled, this, [this] { updateSimulation(); });
    manualPanel_->setVisible(true);
    simulationPanel_->setVisible(false);
    imuPanel_->setVisible(false);
}

void MainWindow::buildRealImuPanel()
{
    imuPanel_ = new QGroupBox(QStringLiteral("真实单 IMU 姿态"), nullptr);
    auto *layout = new QVBoxLayout(imuPanel_);

    auto *notice = new QLabel(QStringLiteral("单指尖 IMU 整链耦合为工程近似；校准后请保持掌心参考方向；非医学关节角"), imuPanel_);
    notice->setWordWrap(true);
    layout->addWidget(notice);

    imuScanButton_ = new QPushButton(QStringLiteral("扫描 WT 设备"), imuPanel_);
    imuDeviceCombo_ = new QComboBox(imuPanel_);
    imuConnectButton_ = new QPushButton(QStringLiteral("连接"), imuPanel_);
    imuDisconnectButton_ = new QPushButton(QStringLiteral("断开"), imuPanel_);
    auto *deviceRow = new QHBoxLayout;
    deviceRow->addWidget(imuScanButton_);
    deviceRow->addWidget(imuDeviceCombo_, 1);
    deviceRow->addWidget(imuConnectButton_);
    deviceRow->addWidget(imuDisconnectButton_);
    layout->addLayout(deviceRow);

    fingerBindingCombo_ = new QComboBox(imuPanel_);
    fingerBindingCombo_->addItems({QStringLiteral("拇指"), QStringLiteral("食指"), QStringLiteral("中指"),
                                   QStringLiteral("无名指"), QStringLiteral("小指")});
    calibrateButton_ = new QPushButton(QStringLiteral("临时校准"), imuPanel_);
    driveCheckbox_ = new QCheckBox(QStringLiteral("启用驱动"), imuPanel_);
    auto *controlRow = new QHBoxLayout;
    controlRow->addWidget(new QLabel(QStringLiteral("手指"), imuPanel_));
    controlRow->addWidget(fingerBindingCombo_, 1);
    controlRow->addWidget(calibrateButton_);
    controlRow->addWidget(driveCheckbox_);
    layout->addLayout(controlRow);

    imuStateLabel_ = new QLabel(QStringLiteral("未连接"), imuPanel_);
    imuAngleLabel_ = new QLabel(QStringLiteral("角度 --"), imuPanel_);
    imuFlexionLabel_ = new QLabel(QStringLiteral("屈伸/张合 --"), imuPanel_);
    imuBatteryLabel_ = new QLabel(QStringLiteral("电量/温度 --"), imuPanel_);
    imuFrameLabel_ = new QLabel(QStringLiteral("帧 --"), imuPanel_);
    layout->addWidget(imuStateLabel_);
    layout->addWidget(imuAngleLabel_);
    layout->addWidget(imuFlexionLabel_);
    layout->addWidget(imuBatteryLabel_);
    layout->addWidget(imuFrameLabel_);

    connect(imuScanButton_, &QPushButton::clicked, this, [this] { bleManager_->startScan(); });
    connect(imuConnectButton_, &QPushButton::clicked, this, [this] {
        const int index = imuDeviceCombo_->currentIndex();
        if (index < 0) return;
        const auto info = imuDeviceCombo_->itemData(index, Qt::UserRole).value<QBluetoothDeviceInfo>();
        bleManager_->stopScan();
        bleManager_->connectToDevice(info);
    });
    connect(imuDisconnectButton_, &QPushButton::clicked, this, [this] { bleManager_->disconnectDevice(); });
    connect(fingerBindingCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (imuController_) {
            imuController_->bindFinger(index);
            refreshImuControls();
        }
    });
    connect(calibrateButton_, &QPushButton::clicked, this, [this] {
        if (!imuController_) return;
        if (imuController_->state() == motion::SingleImuDriveState::Driving) {
            imuController_->setDriving(false);
            QSignalBlocker blocker(driveCheckbox_);
            driveCheckbox_->setChecked(false);
        }
        const QQuaternion raw = QQuaternion::fromEulerAngles(
            static_cast<float>(latestImuData_.angleX), static_cast<float>(latestImuData_.angleY),
            static_cast<float>(latestImuData_.angleZ));
        if (imuController_->calibrate(raw, latestImuData_.lastUpdated.toMSecsSinceEpoch() * 1000)) {
            applyPose(imuController_->output().pose);
            refreshImuControls();
        } else {
            auto &errors = imuController_->output().errors;
            inputStatus_->setText(QStringLiteral("校准失败：%1").arg(
                errors.isEmpty() ? QStringLiteral("未知") : errors.front().message));
        }
    });
    connect(driveCheckbox_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!imuController_) return;
        if (checked) {
            if (!imuController_->setDriving(true)) {
                driveCheckbox_->setChecked(false);
                auto &errors = imuController_->output().errors;
                inputStatus_->setText(QStringLiteral("无法启用驱动：%1").arg(
                    errors.isEmpty() ? QStringLiteral("未知") : errors.front().message));
            }
        } else {
            imuController_->setDriving(false);
        }
        refreshImuControls();
    });
    refreshImuControls();
}

void MainWindow::populateBoneTree()
{
    boneItems_.resize(model_->bones.size());
    QVector<QTreeWidgetItem *> roots;
    QVector<render::BoneInteraction> interactions;
    for (int index = 0; index < model_->bones.size(); ++index) {
        const auto configIterator = solver_->config().joints.constFind(model_->bones[index].name);
        const bool editable = configIterator != solver_->config().joints.constEnd() && configIterator->editable;
        const QString displayName = configIterator != solver_->config().joints.constEnd()
                                        ? configIterator->displayName : model_->bones[index].name;
        auto *item = new QTreeWidgetItem({displayName, editable ? QStringLiteral("可编辑") : QStringLiteral("固定")});
        item->setData(0, Qt::UserRole, index);
        item->setToolTip(0, model_->bones[index].name);
        boneItems_[index] = item;
        const int parent = model_->bones[index].parentIndex;
        if (parent >= 0) boneItems_[parent]->addChild(item); else roots.push_back(item);
        if (editable) {
            render::BoneInteraction interaction;
            interaction.boneIndex = index;
            if (configIterator->lockedAxes.x() < 0.5F) interaction.localAxes.push_back({1, 0, 0});
            if (configIterator->lockedAxes.y() < 0.5F) interaction.localAxes.push_back({0, 1, 0});
            if (configIterator->lockedAxes.z() < 0.5F) interaction.localAxes.push_back({0, 0, 1});
            interactions.push_back(interaction);
        }
    }
    boneTree_->addTopLevelItems(roots);
    boneTree_->expandAll();
    renderWidget_->setBoneInteractions(interactions);
}

void MainWindow::updateSelection(int boneIndex)
{
    if (!model_ || boneIndex < 0 || boneIndex >= model_->bones.size()) return;
    selectedBone_ = boneIndex;
    renderWidget_->setSelectedBone(boneIndex);
    if (boneIndex < boneItems_.size() && boneTree_->currentItem() != boneItems_[boneIndex]) {
        QSignalBlocker blocker(boneTree_);
        boneTree_->setCurrentItem(boneItems_[boneIndex]);
    }
    updateJointEditors();
}

void MainWindow::updateJointEditors()
{
    updatingEditors_ = true;
    const auto configIterator = selectedBone_ >= 0
        ? solver_->config().joints.constFind(model_->bones[selectedBone_].name)
        : solver_->config().joints.constEnd();
    const bool editable = configIterator != solver_->config().joints.constEnd() && configIterator->editable;
    jointNameLabel_->setText(configIterator != solver_->config().joints.constEnd()
                                 ? configIterator->displayName : QStringLiteral("固定骨骼"));
    for (int axis = 0; axis < 3; ++axis) {
        angleEditors_[axis]->setEnabled(editable && configIterator->lockedAxes[axis] < 0.5F
                                        && modeCombo_->currentIndex() == 0);
        angleEditors_[axis]->setValue(selectedBone_ >= 0 ? pose_.pose.localPoses[selectedBone_].eulerDegrees[axis] : 0.0F);
    }
    if (configIterator != solver_->config().joints.constEnd()) {
        limitLabel_->setText(QStringLiteral("限位：%1 至 %2")
                                 .arg(vectorText(configIterator->limits.minDegrees), vectorText(configIterator->limits.maxDegrees)));
        const bool constrained = selectedBone_ < pose_.joints.size() && pose_.joints[selectedBone_].constrained;
        constraintLabel_->setText(constrained ? QStringLiteral("状态：已截断到约束边界") : QStringLiteral("状态：约束内"));
    } else {
        limitLabel_->setText(QStringLiteral("限位：不可编辑"));
        constraintLabel_->setText(QStringLiteral("状态：固定"));
    }
    updatingEditors_ = false;
}

void MainWindow::applyManualEditors()
{
    if (updatingEditors_ || !solver_ || selectedBone_ < 0 || modeCombo_->currentIndex() != 0) return;
    const QVector3D requested(angleEditors_[0]->value(), angleEditors_[1]->value(), angleEditors_[2]->value());
    applyPose(solver_->applyJoint(pose_.pose, model_->bones[selectedBone_].name, requested));
}

void MainWindow::applyPose(const motion::PoseResult &pose)
{
    pose_ = pose;
    renderWidget_->setPoseResult(pose_);
    if (selectedBone_ >= 0) updateJointEditors();
}

void MainWindow::updateSimulation()
{
    if (!mapper_ || modeCombo_->currentIndex() != 1) return;
    motion::HandImuFrame frame;
    simulationTimestamp_ += 16667;
    for (int slot = 0; slot < 6; ++slot) {
        const auto imuSlot = static_cast<motion::ImuSlot>(slot);
        float angle = 0.0F;
        if (slot > 0) {
            const auto &finger = solver_->config().fingers[slot - 1];
            angle = finger.sensorMinDegrees + curlSliders_[slot - 1]->value() / 100.0F
                                                  * (finger.sensorMaxDegrees - finger.sensorMinDegrees);
            const float abduction = abductionSliders_[slot - 1]->value() / 100.0F
                * (abductionSliders_[slot - 1]->value() >= 0
                       ? finger.sensorAbductionMaxDegrees : -finger.sensorAbductionMinDegrees);
            const QQuaternion orientation = QQuaternion::fromAxisAndAngle(
                finger.sensorAbductionAxis, abduction) * QQuaternion::fromAxisAndAngle(
                finger.sensorFlexionAxis, angle);
            frame.samples[slot] = {imuSlot, orientation,
                                   simulationTimestamp_, !invalidChecks_[slot]->isChecked()};
        } else {
            frame.samples[slot] = {imuSlot, QQuaternion(), simulationTimestamp_, !invalidChecks_[0]->isChecked()};
        }
    }
    const motion::ImuMappingResult result = mapper_->update(frame);
    applyPose(result.pose);
    inputStatus_->setText(result.errors.isEmpty() ? QStringLiteral("输入：六路样本已应用")
                                                   : QStringLiteral("输入：%1 (%2)")
                                                         .arg(result.errors.front().message, result.errors.front().detail));
}

void MainWindow::resetPose()
{
    if (!solver_) return;
    mapper_->reset();
    if (imuController_) imuController_->resetCalibration();
    simulationTimestamp_ = 0;
    for (QSlider *slider : curlSliders_) slider->setValue(0);
    for (QSlider *slider : abductionSliders_) slider->setValue(0);
    for (QCheckBox *check : invalidChecks_) check->setChecked(false);
    applyPose(solver_->solve(solver_->bindPose()));
    inputStatus_->setText(QStringLiteral("输入：姿态已重置"));
    refreshImuControls();
}

void MainWindow::setMode(int index)
{
    manualPanel_->setVisible(index == 0);
    simulationPanel_->setVisible(index == 1);
    imuPanel_->setVisible(index == 2);
    if (simulationTimer_) {
        if (index == 1) simulationTimer_->start(); else simulationTimer_->stop();
    }
    if (imuUpdateTimer_) {
        if (index == 2) imuUpdateTimer_->start(); else imuUpdateTimer_->stop();
    }
    if (index == 2 && imuController_) {
        imuController_->setConnected(bleManager_ && bleManager_->state() == imu::BleConnectionState::Connected);
    }
    if (solver_) {
        mapper_->reset();
        applyPose(solver_->solve(solver_->bindPose(), index == 1 || index == 2));
    }
    if (index == 2) refreshImuControls();
    updateJointEditors();
    if (index == 0) inputStatus_->setText(QStringLiteral("输入：手动关节"));
    else if (index == 1) inputStatus_->setText(QStringLiteral("输入：六路耦合模拟"));
    else inputStatus_->setText(QStringLiteral("输入：真实单 IMU"));
}

void MainWindow::showStartupError(const QString &summary, const QString &detail)
{
    modelStatus_->setText(QStringLiteral("启动失败：%1").arg(summary));
    QTimer::singleShot(0, this, [this, summary, detail] {
        QMessageBox box(QMessageBox::Critical, QStringLiteral("启动错误"), summary, QMessageBox::Ok, this);
        box.setDetailedText(detail);
        box.exec();
    });
}

void MainWindow::showRenderError(const render::RenderError &error)
{
    inputStatus_->setText(QStringLiteral("渲染错误：%1").arg(error.message));
    if (smokeTest_) {
        qCritical().noquote() << "SMOKE_RENDER_ERROR:" << error.message << error.detail;
        smokeTest_ = false;
        QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(4); });
        return;
    }
    QMessageBox box(QMessageBox::Critical, QStringLiteral("渲染错误"), error.message, QMessageBox::Ok, this);
    box.setDetailedText(error.detail);
    box.exec();
}

void MainWindow::enableSmokeTest()
{
    smokeTest_ = true;
    smokeStep_ = 0;
}

void MainWindow::updateFrameRate()
{
    ++frameCount_;
    const qint64 elapsed = frameTimer_.elapsed();
    if (elapsed >= 500) {
        fpsStatus_->setText(QStringLiteral("FPS：%1").arg(frameCount_ * 1000.0 / elapsed, 0, 'f', 1));
        frameCount_ = 0;
        frameTimer_.restart();
    }
    if (smokeTest_) {
        if (smokeStep_ == 0) {
            qInfo().noquote() << "SMOKE_FIRST_FRAME_OK";
            renderWidget_->grabFramebuffer().save(QStringLiteral("outputs/render_regression_bind.png"));
            if (!runSmokeInteractionSequence()) {
                smokeTest_ = false;
                QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(5); });
                return;
            }
            smokeStep_ = 1;
            renderWidget_->update();
        } else {
            renderWidget_->grabFramebuffer().save(QStringLiteral("outputs/render_regression_moved.png"));
            smokeTest_ = false;
            qInfo().noquote() << "SMOKE_INTERACTION_SEQUENCE_OK";
            QTimer::singleShot(0, qApp, [] { QCoreApplication::exit(0); });
        }
    }
}

bool MainWindow::runSmokeInteractionSequence()
{
    if (!model_ || !solver_ || !mapper_) {
        qCritical().noquote() << "SMOKE_INTERACTION_ERROR: missing application components";
        return false;
    }
    for (const motion::FingerConfig &finger : solver_->config().fingers) {
        int editableBone = -1;
        motion::JointConfig jointConfig;
        for (auto boneName = finger.bones.crbegin(); boneName != finger.bones.crend(); ++boneName) {
            const auto config = solver_->config().joints.constFind(*boneName);
            if (config != solver_->config().joints.constEnd() && config->editable) {
                for (int index = 0; index < model_->bones.size(); ++index) {
                    if (model_->bones[index].name == *boneName) {
                        editableBone = index;
                        jointConfig = *config;
                        break;
                    }
                }
                break;
            }
        }
        if (editableBone < 0) {
            qCritical().noquote() << "SMOKE_INTERACTION_ERROR: no editable distal bone" << finger.name;
            return false;
        }
        const int parentIndex = model_->bones[editableBone].parentIndex;
        const motion::PoseResult before = pose_;
        updateSelection(editableBone);
        applyPose(solver_->applyJoint(pose_.pose, model_->bones[editableBone].name,
                                      jointConfig.limits.maxDegrees));
        if (editableBone >= pose_.joints.size()) {
            return false;
        }
        for (const QMatrix4x4 &matrix : pose_.globalMatrices) {
            const float *values = matrix.constData();
            for (int value = 0; value < 16; ++value) {
                if (!qIsFinite(values[value])) {
                    qCritical().noquote() << "SMOKE_INTERACTION_ERROR: non-finite matrix" << finger.name;
                    return false;
                }
            }
        }
        if (parentIndex >= 0) {
            const QVector3D beforeParent = before.globalMatrices[parentIndex].column(3).toVector3D();
            const QVector3D beforeBone = before.globalMatrices[editableBone].column(3).toVector3D();
            const QVector3D afterParent = pose_.globalMatrices[parentIndex].column(3).toVector3D();
            const QVector3D afterBone = pose_.globalMatrices[editableBone].column(3).toVector3D();
            if (qAbs((beforeBone - beforeParent).length() - (afterBone - afterParent).length()) > 1.0e-4F) {
                qCritical().noquote() << "SMOKE_INTERACTION_ERROR: changed bone length" << finger.name;
                return false;
            }
        }
    }
    modeCombo_->setCurrentIndex(1);
    for (int finger = 0; finger < 5; ++finger) {
        curlSliders_[finger]->setValue(20 + finger * 15);
        abductionSliders_[finger]->setValue(-60 + finger * 30);
    }
    invalidChecks_[1]->setChecked(true);
    updateSimulation();
    invalidChecks_[1]->setChecked(false);
    const bool simulationApplied = pose_.coupledApproximation;
    return simulationApplied;
}

void MainWindow::updateRealImu()
{
    if (!imuController_ || imuController_->state() != motion::SingleImuDriveState::Driving) return;
    const QQuaternion raw = QQuaternion::fromEulerAngles(
        static_cast<float>(latestImuData_.angleX), static_cast<float>(latestImuData_.angleY),
        static_cast<float>(latestImuData_.angleZ));
    const motion::SingleImuMappingOutput output = imuController_->update(
        raw, latestImuData_.lastUpdated.toMSecsSinceEpoch() * 1000);
    if (output.frameApplied) {
        applyPose(output.pose);
        const auto &finger = solver_->config().fingers[output.fingerIndex];
        imuFlexionLabel_->setText(QStringLiteral("屈伸 %1°  张合 %2°  curl %3  [%4]")
            .arg(output.flexionDegrees, 0, 'f', 1).arg(output.abductionDegrees, 0, 'f', 1)
            .arg(output.curl, 0, 'f', 2).arg(finger.name));
        inputStatus_->setText(QStringLiteral("输入：真实单 IMU 已应用 [%1]").arg(finger.name));
    }
    if (!output.errors.isEmpty()) {
        inputStatus_->setText(QStringLiteral("输入：%1 (%2)")
            .arg(output.errors.front().message, output.errors.front().detail));
    }
}

void MainWindow::refreshImuControls()
{
    if (!imuController_) return;
    const motion::SingleImuDriveState state = imuController_->state();
    const bool connected = state != motion::SingleImuDriveState::Disconnected;
    const bool bound = imuController_->output().fingerIndex >= 0;
    const bool driving = state == motion::SingleImuDriveState::Driving;
    const bool ready = state == motion::SingleImuDriveState::Ready;
    const bool hasData = latestImuData_.frameCount > 0;
    imuScanButton_->setEnabled(!connected && bleManager_->state() != imu::BleConnectionState::Scanning);
    imuConnectButton_->setEnabled(!connected && imuDeviceCombo_->count() > 0);
    imuDisconnectButton_->setEnabled(connected);
    fingerBindingCombo_->setEnabled(connected && !driving);
    calibrateButton_->setEnabled(connected && bound && hasData);
    driveCheckbox_->setEnabled(ready || driving);
    if (!driving && driveCheckbox_->isChecked()) {
        QSignalBlocker blocker(driveCheckbox_);
        driveCheckbox_->setChecked(false);
    }
    if (fingerBindingCombo_->currentIndex() >= 0 && imuController_->output().fingerIndex != fingerBindingCombo_->currentIndex()) {
        if (bound) fingerBindingCombo_->setCurrentIndex(imuController_->output().fingerIndex);
    }
}

}
