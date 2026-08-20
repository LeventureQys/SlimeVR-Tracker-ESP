#include "madgwick_filter.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double MinimumNorm = 1.0e-12;

bool finite(const Vector3d &value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double norm(const Vector3d &value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}
}

MadgwickFilter::MadgwickFilter(double beta)
    : beta_(std::isfinite(beta) && beta >= 0.0 ? beta : 0.10)
    , quaternion_(1.0f, 0.0f, 0.0f, 0.0f)
{
}

void MadgwickFilter::reset()
{
    quaternion_ = QQuaternion(1.0f, 0.0f, 0.0f, 0.0f);
    mode_ = FusionMode::Invalid;
}

void MadgwickFilter::setBeta(double beta)
{
    if (std::isfinite(beta) && beta >= 0.0) {
        beta_ = beta;
    }
}

bool MadgwickFilter::update(const Vector3d &gyro, const Vector3d &acceleration,
                            const std::optional<Vector3d> &magnetic, double dtSeconds)
{
    if (!std::isfinite(dtSeconds) || dtSeconds <= 0.0 || dtSeconds > 0.1
        || !finite(gyro) || !finite(acceleration)) {
        return false;
    }

    const double accelerationNorm = norm(acceleration);
    if (!std::isfinite(accelerationNorm) || accelerationNorm < MinimumNorm) {
        return false;
    }

    double q1 = quaternion_.scalar();
    double q2 = quaternion_.x();
    double q3 = quaternion_.y();
    double q4 = quaternion_.z();
    const double reciprocalAccelerationNorm = 1.0 / accelerationNorm;
    const double ax = acceleration.x * reciprocalAccelerationNorm;
    const double ay = acceleration.y * reciprocalAccelerationNorm;
    const double az = acceleration.z * reciprocalAccelerationNorm;

    double s1 = 0.0;
    double s2 = 0.0;
    double s3 = 0.0;
    double s4 = 0.0;
    FusionMode nextMode = FusionMode::SixAxis;

    const bool validMagnetic = magnetic && finite(*magnetic) && norm(*magnetic) >= MinimumNorm;
    if (validMagnetic) {
        nextMode = FusionMode::NineAxis;
        const double reciprocalMagneticNorm = 1.0 / norm(*magnetic);
        const double mx = magnetic->x * reciprocalMagneticNorm;
        const double my = magnetic->y * reciprocalMagneticNorm;
        const double mz = magnetic->z * reciprocalMagneticNorm;

        const double twoQ1Mx = 2.0 * q1 * mx;
        const double twoQ1My = 2.0 * q1 * my;
        const double twoQ1Mz = 2.0 * q1 * mz;
        const double twoQ2Mx = 2.0 * q2 * mx;
        const double twoQ1 = 2.0 * q1;
        const double twoQ2 = 2.0 * q2;
        const double twoQ3 = 2.0 * q3;
        const double twoQ4 = 2.0 * q4;
        const double twoQ1Q3 = 2.0 * q1 * q3;
        const double twoQ3Q4 = 2.0 * q3 * q4;
        const double q1q1 = q1 * q1;
        const double q1q2 = q1 * q2;
        const double q1q3 = q1 * q3;
        const double q1q4 = q1 * q4;
        const double q2q2 = q2 * q2;
        const double q2q3 = q2 * q3;
        const double q2q4 = q2 * q4;
        const double q3q3 = q3 * q3;
        const double q3q4 = q3 * q4;
        const double q4q4 = q4 * q4;

        const double hx = mx * q1q1 - twoQ1My * q4 + twoQ1Mz * q3 + mx * q2q2
            + twoQ2 * my * q3 + twoQ2 * mz * q4 - mx * q3q3 - mx * q4q4;
        const double hy = twoQ1Mx * q4 + my * q1q1 - twoQ1Mz * q2 + twoQ2Mx * q3
            - my * q2q2 + my * q3q3 + twoQ3 * mz * q4 - my * q4q4;
        const double twoBx = std::sqrt(hx * hx + hy * hy);
        const double twoBz = -twoQ1Mx * q3 + twoQ1My * q2 + mz * q1q1
            + twoQ2Mx * q4 - mz * q2q2 + twoQ3 * my * q4 - mz * q3q3 + mz * q4q4;
        const double fourBx = 2.0 * twoBx;
        const double fourBz = 2.0 * twoBz;

        s1 = -twoQ3 * (2.0 * q2q4 - twoQ1Q3 - ax)
            + twoQ2 * (2.0 * q1q2 + twoQ3Q4 - ay)
            - twoBz * q3 * (twoBx * (0.5 - q3q3 - q4q4) + twoBz * (q2q4 - q1q3) - mx)
            + (-twoBx * q4 + twoBz * q2) * (twoBx * (q2q3 - q1q4) + twoBz * (q1q2 + q3q4) - my)
            + twoBx * q3 * (twoBx * (q1q3 + q2q4) + twoBz * (0.5 - q2q2 - q3q3) - mz);
        s2 = twoQ4 * (2.0 * q2q4 - twoQ1Q3 - ax)
            + twoQ1 * (2.0 * q1q2 + twoQ3Q4 - ay)
            - 4.0 * q2 * (1.0 - 2.0 * q2q2 - 2.0 * q3q3 - az)
            + twoBz * q4 * (twoBx * (0.5 - q3q3 - q4q4) + twoBz * (q2q4 - q1q3) - mx)
            + (twoBx * q3 + twoBz * q1) * (twoBx * (q2q3 - q1q4) + twoBz * (q1q2 + q3q4) - my)
            + (twoBx * q4 - fourBz * q2) * (twoBx * (q1q3 + q2q4) + twoBz * (0.5 - q2q2 - q3q3) - mz);
        s3 = -twoQ1 * (2.0 * q2q4 - twoQ1Q3 - ax)
            + twoQ4 * (2.0 * q1q2 + twoQ3Q4 - ay)
            - 4.0 * q3 * (1.0 - 2.0 * q2q2 - 2.0 * q3q3 - az)
            + (-fourBx * q3 - twoBz * q1) * (twoBx * (0.5 - q3q3 - q4q4) + twoBz * (q2q4 - q1q3) - mx)
            + (twoBx * q2 + twoBz * q4) * (twoBx * (q2q3 - q1q4) + twoBz * (q1q2 + q3q4) - my)
            + (twoBx * q1 - fourBz * q3) * (twoBx * (q1q3 + q2q4) + twoBz * (0.5 - q2q2 - q3q3) - mz);
        s4 = twoQ2 * (2.0 * q2q4 - twoQ1Q3 - ax)
            + twoQ3 * (2.0 * q1q2 + twoQ3Q4 - ay)
            + (-fourBx * q4 + twoBz * q2) * (twoBx * (0.5 - q3q3 - q4q4) + twoBz * (q2q4 - q1q3) - mx)
            + (-twoBx * q1 + twoBz * q3) * (twoBx * (q2q3 - q1q4) + twoBz * (q1q2 + q3q4) - my)
            + twoBx * q2 * (twoBx * (q1q3 + q2q4) + twoBz * (0.5 - q2q2 - q3q3) - mz);
    } else {
        const double twoQ1 = 2.0 * q1;
        const double twoQ2 = 2.0 * q2;
        const double twoQ3 = 2.0 * q3;
        const double twoQ4 = 2.0 * q4;
        const double fourQ1 = 4.0 * q1;
        const double fourQ2 = 4.0 * q2;
        const double fourQ3 = 4.0 * q3;
        const double eightQ2 = 8.0 * q2;
        const double eightQ3 = 8.0 * q3;
        const double q1q1 = q1 * q1;
        const double q2q2 = q2 * q2;
        const double q3q3 = q3 * q3;
        const double q4q4 = q4 * q4;

        s1 = fourQ1 * q3q3 + twoQ3 * ax + fourQ1 * q2q2 - twoQ2 * ay;
        s2 = fourQ2 * q4q4 - twoQ4 * ax + 4.0 * q1q1 * q2 - twoQ1 * ay
            - fourQ2 + eightQ2 * q2q2 + eightQ2 * q3q3 + fourQ2 * az;
        s3 = 4.0 * q1q1 * q3 + twoQ1 * ax + fourQ3 * q4q4 - twoQ4 * ay
            - fourQ3 + eightQ3 * q2q2 + eightQ3 * q3q3 + fourQ3 * az;
        s4 = 4.0 * q2q2 * q4 - twoQ2 * ax + 4.0 * q3q3 * q4 - twoQ3 * ay;
    }

    const double gradientNorm = std::sqrt(s1 * s1 + s2 * s2 + s3 * s3 + s4 * s4);
    if (std::isfinite(gradientNorm) && gradientNorm >= MinimumNorm) {
        const double reciprocalGradientNorm = 1.0 / gradientNorm;
        s1 *= reciprocalGradientNorm;
        s2 *= reciprocalGradientNorm;
        s3 *= reciprocalGradientNorm;
        s4 *= reciprocalGradientNorm;
    } else {
        s1 = s2 = s3 = s4 = 0.0;
    }

    const double qDot1 = 0.5 * (-q2 * gyro.x - q3 * gyro.y - q4 * gyro.z) - beta_ * s1;
    const double qDot2 = 0.5 * (q1 * gyro.x + q3 * gyro.z - q4 * gyro.y) - beta_ * s2;
    const double qDot3 = 0.5 * (q1 * gyro.y - q2 * gyro.z + q4 * gyro.x) - beta_ * s3;
    const double qDot4 = 0.5 * (q1 * gyro.z + q2 * gyro.y - q3 * gyro.x) - beta_ * s4;

    q1 += qDot1 * dtSeconds;
    q2 += qDot2 * dtSeconds;
    q3 += qDot3 * dtSeconds;
    q4 += qDot4 * dtSeconds;
    const double quaternionNorm = std::sqrt(q1 * q1 + q2 * q2 + q3 * q3 + q4 * q4);
    if (!std::isfinite(quaternionNorm) || quaternionNorm < MinimumNorm) {
        return false;
    }

    const double reciprocalQuaternionNorm = 1.0 / quaternionNorm;
    quaternion_ = QQuaternion(float(q1 * reciprocalQuaternionNorm), float(q2 * reciprocalQuaternionNorm),
                              float(q3 * reciprocalQuaternionNorm), float(q4 * reciprocalQuaternionNorm));
    mode_ = nextMode;
    return true;
}

QQuaternion MadgwickFilter::quaternion() const
{
    return quaternion_;
}

FusionMode MadgwickFilter::mode() const
{
    return mode_;
}
