#pragma once

#include "imu_data.h"

#include <QObject>

class QTimer;

class DemoDataSource final : public QObject {
    Q_OBJECT

public:
    explicit DemoDataSource(QObject *parent = nullptr);

    void start();
    void stop();
    bool isActive() const;

signals:
    void dataUpdated(const ImuData &data);

private:
    void updateData();

    QTimer *timer_ = nullptr;
    ImuData data_;
};
