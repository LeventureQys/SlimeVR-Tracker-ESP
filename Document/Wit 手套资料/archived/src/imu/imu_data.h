#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

namespace handdemo::imu {

struct ImuData {
    double accelerationX{0.0};
    double accelerationY{0.0};
    double accelerationZ{0.0};
    double angularVelocityX{0.0};
    double angularVelocityY{0.0};
    double angularVelocityZ{0.0};
    double angleX{0.0};
    double angleY{0.0};
    double angleZ{0.0};
    double magneticX{0.0};
    double magneticY{0.0};
    double magneticZ{0.0};
    double batteryPercent{0.0};
    double temperatureCelsius{0.0};
    QString firmwareVersion;
    QDateTime lastUpdated;
    quint64 frameCount{0};
    quint64 motionFrameCount{0};
};

}

Q_DECLARE_METATYPE(handdemo::imu::ImuData)
