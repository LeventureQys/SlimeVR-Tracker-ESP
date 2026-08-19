#include "demo_data_source.h"

#include <QDateTime>
#include <QTimer>

#include <cmath>

DemoDataSource::DemoDataSource(QObject *parent)
    : QObject(parent), timer_(new QTimer(this))
{
    timer_->setInterval(100);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &DemoDataSource::updateData);
}

void DemoDataSource::start()
{
    if (timer_->isActive()) {
        return;
    }
    data_ = {};
    data_.firmwareVersion = QStringLiteral("Demo 1.0.0");
    timer_->start();
}

void DemoDataSource::stop()
{
    timer_->stop();
}

bool DemoDataSource::isActive() const
{
    return timer_->isActive();
}

void DemoDataSource::updateData()
{
    ++data_.frameCount;
    const double time = static_cast<double>(data_.frameCount) * 0.1;

    data_.accelerationX = 0.35 * std::sin(time * 0.9);
    data_.accelerationY = 0.28 * std::cos(time * 0.7);
    data_.accelerationZ = 1.0 + 0.06 * std::sin(time * 0.4);
    data_.angularVelocityX = 75.0 * std::sin(time * 0.8);
    data_.angularVelocityY = 55.0 * std::cos(time * 0.6);
    data_.angularVelocityZ = 40.0 * std::sin(time * 0.5);
    data_.angleX = 30.0 * std::sin(time * 0.35);
    data_.angleY = 22.0 * std::cos(time * 0.3);
    data_.angleZ = 90.0 * std::sin(time * 0.2);
    data_.magneticX = 0.32 + 0.08 * std::sin(time * 0.25);
    data_.magneticY = -0.18 + 0.07 * std::cos(time * 0.3);
    data_.magneticZ = 0.46 + 0.05 * std::sin(time * 0.4);
    data_.batteryPercent = 85.0 + 5.0 * std::sin(time * 0.02);
    data_.temperatureCelsius = 24.0 + 1.5 * std::sin(time * 0.05);
    data_.lastUpdated = QDateTime::currentDateTime();

    emit dataUpdated(data_);
}
