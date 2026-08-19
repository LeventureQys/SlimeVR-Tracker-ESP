#pragma once

#include "imu_data.h"

#include <QByteArray>
#include <QObject>

class WitProtocolParser final : public QObject {
    Q_OBJECT

public:
    explicit WitProtocolParser(QObject *parent = nullptr);

    void appendBytes(const QByteArray &bytes);
    void reset();
    const ImuData &data() const;
    static double batteryPercentForVoltage(double voltage);

signals:
    void dataUpdated(const ImuData &data);

private:
    bool parseFrame(const QByteArray &frame);
    static qint16 signedValue(char low, char high);

    QByteArray buffer_;
    ImuData data_;
};
