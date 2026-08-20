#include "slimevr_mount_dialog.h"

#include "imu_types.h"
#include "slimevr_settings.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace {
const std::array<QString, 6> RowNames{
    QStringLiteral("手腕"), QStringLiteral("拇指"), QStringLiteral("食指"),
    QStringLiteral("中指"), QStringLiteral("无名指"), QStringLiteral("小指")};

QDoubleSpinBox *makeComponentSpin(QWidget *parent, const QString &prefix, int index, double value)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setObjectName(QStringLiteral("mountSpin%1%2").arg(prefix).arg(index));
    spin->setRange(-1.0, 1.0);
    spin->setDecimals(4);
    spin->setSingleStep(0.01);
    spin->setValue(value);
    return spin;
}

bool isUnit(const QQuaternion &q)
{
    const double norm = std::sqrt(double(q.scalar()) * q.scalar() + double(q.x()) * q.x()
                                  + double(q.y()) * q.y() + double(q.z()) * q.z());
    return std::abs(norm - 1.0) <= 1.0e-3;
}
} // namespace

SlimeVrMountDialog::SlimeVrMountDialog(const std::array<QQuaternion, 6> &mountings, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("六路安装旋转"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);
    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(QStringLiteral("传感器"), this), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("w (标量)"), this), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("x"), this), 0, 2);
    grid->addWidget(new QLabel(QStringLiteral("y"), this), 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("z"), this), 0, 4);
    grid->addWidget(new QLabel(QString(), this), 0, 5);

    for (int index = 0; index < 6; ++index) {
        const QQuaternion &mounting = mountings[size_t(index)];
        grid->addWidget(new QLabel(RowNames[size_t(index)], this), index + 1, 0);
        wSpins_[size_t(index)] = makeComponentSpin(this, QStringLiteral("W"), index, double(mounting.scalar()));
        xSpins_[size_t(index)] = makeComponentSpin(this, QStringLiteral("X"), index, double(mounting.x()));
        ySpins_[size_t(index)] = makeComponentSpin(this, QStringLiteral("Y"), index, double(mounting.y()));
        zSpins_[size_t(index)] = makeComponentSpin(this, QStringLiteral("Z"), index, double(mounting.z()));
        grid->addWidget(wSpins_[size_t(index)], index + 1, 1);
        grid->addWidget(xSpins_[size_t(index)], index + 1, 2);
        grid->addWidget(ySpins_[size_t(index)], index + 1, 3);
        grid->addWidget(zSpins_[size_t(index)], index + 1, 4);
        resetButtons_[size_t(index)] = new QPushButton(QStringLiteral("恢复默认"), this);
        resetButtons_[size_t(index)]->setObjectName(QStringLiteral("mountResetButton%1").arg(index));
        grid->addWidget(resetButtons_[size_t(index)], index + 1, 5);
        connect(resetButtons_[size_t(index)], &QPushButton::clicked, this, [this, index] {
            resetRow(index);
        });
    }
    layout->addLayout(grid);
    layout->addWidget(new QLabel(
        QStringLiteral("安装旋转为节点坐标到传感器坐标的固定单位四元数；默认单位值表示无偏置。"),
        this));

    auto *buttonLayout = new QHBoxLayout;
    auto *okButton = new QPushButton(QStringLiteral("确定"), this);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    okButton->setObjectName(QStringLiteral("mountOkButton"));
    cancelButton->setObjectName(QStringLiteral("mountCancelButton"));
    connect(okButton, &QPushButton::clicked, this, &SlimeVrMountDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &SlimeVrMountDialog::reject);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(okButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);
}

std::array<QQuaternion, 6> SlimeVrMountDialog::mountings() const
{
    std::array<QQuaternion, 6> result{};
    for (int index = 0; index < 6; ++index) {
        result[size_t(index)] = QQuaternion(
            float(wSpins_[size_t(index)]->value()),
            float(xSpins_[size_t(index)]->value()),
            float(ySpins_[size_t(index)]->value()),
            float(zSpins_[size_t(index)]->value()));
    }
    return result;
}

void SlimeVrMountDialog::resetRow(int index)
{
    wSpins_[size_t(index)]->setValue(1.0);
    xSpins_[size_t(index)]->setValue(0.0);
    ySpins_[size_t(index)]->setValue(0.0);
    zSpins_[size_t(index)]->setValue(0.0);
}

void SlimeVrMountDialog::accept()
{
    QString error;
    const auto candidate = mountings();
    for (int index = 0; index < 6; ++index) {
        if (!isUnit(candidate[size_t(index)])) {
            error = QStringLiteral("%1的安装旋转不是单位四元数（范数必须为 1）。").arg(RowNames[size_t(index)]);
            break;
        }
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("安装旋转无效"), error);
        return;
    }
    QDialog::accept();
}
