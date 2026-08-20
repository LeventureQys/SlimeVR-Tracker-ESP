#pragma once

#include <QDialog>
#include <QQuaternion>

#include <array>

class QDoubleSpinBox;
class QPushButton;

// Edits the six per-sensor fixed mounting rotations (w,x,y,z quaternion).
// Strictly validates unit length on accept; per-row reset to identity.
class SlimeVrMountDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SlimeVrMountDialog(const std::array<QQuaternion, 6> &mountings, QWidget *parent = nullptr);

    std::array<QQuaternion, 6> mountings() const;

private:
    void resetRow(int index);
    void accept() override;

    std::array<QDoubleSpinBox *, 6> wSpins_{};
    std::array<QDoubleSpinBox *, 6> xSpins_{};
    std::array<QDoubleSpinBox *, 6> ySpins_{};
    std::array<QDoubleSpinBox *, 6> zSpins_{};
    std::array<QPushButton *, 6> resetButtons_{};
};
