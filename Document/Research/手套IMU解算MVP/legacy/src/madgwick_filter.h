#pragma once

#include <QQuaternion>

#include <optional>

struct Vector3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

enum class FusionMode {
    Invalid,
    SixAxis,
    NineAxis
};

class MadgwickFilter final {
public:
    explicit MadgwickFilter(double beta = 0.10);

    void reset();
    void setBeta(double beta);
    bool update(const Vector3d &gyroRadPerSecond,
                const Vector3d &accelerationG,
                const std::optional<Vector3d> &magnetic,
                double dtSeconds);
    QQuaternion quaternion() const;
    FusionMode mode() const;

private:
    double beta_ = 0.10;
    QQuaternion quaternion_;
    FusionMode mode_ = FusionMode::Invalid;
};
