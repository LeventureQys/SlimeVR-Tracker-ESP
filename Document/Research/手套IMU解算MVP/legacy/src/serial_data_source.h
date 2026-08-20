#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include <QtTypes>

class QSerialPort;

enum class SourceState {
    Closed,
    Opening,
    Open,
    Closing,
    Error
};

struct SerialPortDescriptor {
    QString portName;
    QString description;
    QString manufacturer;
    QString serialNumber;
};

class SerialDataSource final : public QObject {
    Q_OBJECT

public:
    explicit SerialDataSource(QObject *parent = nullptr);
    ~SerialDataSource() override;

    QList<SerialPortDescriptor> availablePorts() const;
    void openPort(const QString &portName, qint32 baudRate = 921600);
    void closePort();
    bool isOpen() const;
    SourceState state() const;

signals:
    void bytesReady(const QByteArray &bytes, qint64 monotonicNs);
    void stateChanged(SourceState state, const QString &message);
    void errorOccurred(const QString &message);

private:
    void handleReadyRead();
    void handleSerialError(int errorValue);
    void setState(SourceState state, const QString &message);
    void closeDevice(bool publishTransitions);

    QSerialPort *serialPort_ = nullptr;
    QElapsedTimer monotonicTimer_;
    SourceState state_ = SourceState::Closed;
    bool handlingError_ = false;
};
