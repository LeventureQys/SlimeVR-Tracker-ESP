#include "magnetic_calibration.h"

#include <cmath>

namespace handstudio {

bool MagneticCalibration::Config::isValid(QString *reason) const
{
    if (minSamples < 1 || !(minRadiusMicroTesla > 0.0 && std::isfinite(minRadiusMicroTesla))) {
        if (reason) {
            *reason = QStringLiteral("磁校准参数非法");
        }
        return false;
    }
    if (reason) {
        reason->clear();
    }
    return true;
}

MagneticCalibration::MagneticCalibration(const Config &config)
    : config_(config)
{
}

void MagneticCalibration::reset()
{
    samples_.clear();
}

void MagneticCalibration::addSample(const QVector3D &magneticMicroTesla)
{
    if (!std::isfinite(double(magneticMicroTesla.x())) || !std::isfinite(double(magneticMicroTesla.y()))
        || !std::isfinite(double(magneticMicroTesla.z()))) {
        return;
    }
    samples_.append(magneticMicroTesla);
}

int MagneticCalibration::sampleCount() const
{
    return samples_.size();
}

MagneticCalibration::Result MagneticCalibration::compute() const
{
    Result result;
    if (samples_.size() < config_.minSamples) {
        result.error = QStringLiteral("磁校准样本不足：%1 < %2").arg(samples_.size()).arg(config_.minSamples);
        return result;
    }

    QVector3D sum;
    for (const QVector3D &sample : samples_) {
        sum += sample;
    }
    const QVector3D hardIron = sum / float(samples_.size());

    double sumSq[3] = {0.0, 0.0, 0.0};
    for (const QVector3D &sample : samples_) {
        const double dx = double(sample.x()) - double(hardIron.x());
        const double dy = double(sample.y()) - double(hardIron.y());
        const double dz = double(sample.z()) - double(hardIron.z());
        sumSq[0] += dx * dx;
        sumSq[1] += dy * dy;
        sumSq[2] += dz * dz;
    }
    double radius[3] = {0.0, 0.0, 0.0};
    for (int axis = 0; axis < 3; ++axis) {
        radius[axis] = std::sqrt(sumSq[axis] / double(samples_.size()));
        if (!(radius[axis] > config_.minRadiusMicroTesla)) {
            result.error = QStringLiteral("磁校准轴向半径过小：轴 %1 半径 %2 µT").arg(axis).arg(radius[axis]);
            return result;
        }
    }

    result.hardIronMicroTesla = hardIron;
    result.softIron = {1.0 / radius[0], 0.0, 0.0, 0.0, 1.0 / radius[1], 0.0, 0.0, 0.0, 1.0 / radius[2]};
    result.valid = true;
    return result;
}

QVector3D MagneticCalibration::apply(const QVector3D &magnetic, const QVector3D &hardIron,
                                     const std::array<double, 9> &softIron)
{
    const double in[3] = {double(magnetic.x()) - double(hardIron.x()),
                          double(magnetic.y()) - double(hardIron.y()),
                          double(magnetic.z()) - double(hardIron.z())};
    double out[3] = {0.0, 0.0, 0.0};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            out[row] += softIron[static_cast<std::size_t>(row * 3 + col)] * in[col];
        }
    }
    return QVector3D(float(out[0]), float(out[1]), float(out[2]));
}

}
