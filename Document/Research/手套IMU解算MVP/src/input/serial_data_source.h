#pragma once

#include "input/idata_source.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QString>

#include <QtTypes>

class QSerialPort;

namespace handstudio {

struct SerialPortDescriptor {
    QString portName;
    QString description;
    QString manufacturer;
    QString serialNumber;
};

class SerialDataSource final : public IDataSource {
    Q_OBJECT

public:
    explicit SerialDataSource(QObject *parent = nullptr);
    ~SerialDataSource() override;

    void setPortName(const QString &portName);
    void setBaudRate(qint32 baudRate);

    static QList<SerialPortDescriptor> availablePorts();
    bool isOpen() const;
    SourceState state() const;

public slots:
    void start() override;
    void stop() override;

private:
    void handleReadyRead();
    void handleSerialError(int errorValue);
    void setState(SourceState state, const Diagnostic &diagnostic);

    QSerialPort *serialPort_ = nullptr;
    QElapsedTimer monotonicTimer_;
    QString portName_;
    qint32 baudRate_ = 921600;
    SourceState state_ = SourceState::Idle;
    bool handlingError_ = false;
};

}
