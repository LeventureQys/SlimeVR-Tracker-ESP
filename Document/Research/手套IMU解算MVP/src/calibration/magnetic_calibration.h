#pragma once

#include <QString>
#include <QVector>
#include <QVector3D>

#include <array>

namespace handstudio {

// Hard-iron / soft-iron magnetometer calibration. Collects raw magnetic
// samples and derives a hard-iron offset (sample mean) plus a diagonal soft-iron
// scaling that normalizes each axis to unit RMS radius. This is a simple,
// deterministic ellipsoid-sphere correction; real accuracy conclusions are
// deferred to SubStage 2's real dataset.
class MagneticCalibration {
public:
    struct Result {
        bool valid = false;
        QVector3D hardIronMicroTesla;
        std::array<double, 9> softIron{{1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}};
        QString error;
    };

    struct Config {
        int minSamples = 20;
        double minRadiusMicroTesla = 1.0;

        bool isValid(QString *reason = nullptr) const;
    };

    explicit MagneticCalibration(const Config &config = {});
    void reset();
    void addSample(const QVector3D &magneticMicroTesla);
    int sampleCount() const;
    Result compute() const;

    static QVector3D apply(const QVector3D &magnetic, const QVector3D &hardIron,
                           const std::array<double, 9> &softIron);

private:
    Config config_;
    QVector<QVector3D> samples_;
};

}
