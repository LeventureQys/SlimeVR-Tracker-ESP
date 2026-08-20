#include "serial_data_source.h"

#include <QSerialPort>
#include <QSerialPortInfo>

SerialDataSource::SerialDataSource(QObject *parent)
    : QObject(parent), serialPort_(new QSerialPort(this))
{
    monotonicTimer_.start();

    connect(serialPort_, &QSerialPort::readyRead,
            this, &SerialDataSource::handleReadyRead);
    connect(serialPort_, &QSerialPort::errorOccurred,
            this, [this](QSerialPort::SerialPortError error) {
                handleSerialError(static_cast<int>(error));
            });
}

SerialDataSource::~SerialDataSource()
{
    closeDevice(false);
}

QList<SerialPortDescriptor> SerialDataSource::availablePorts() const
{
    QList<SerialPortDescriptor> descriptors;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    descriptors.reserve(ports.size());

    for (const QSerialPortInfo &port : ports) {
        descriptors.append({port.portName(),
                            port.description(),
                            port.manufacturer(),
                            port.serialNumber()});
    }

    return descriptors;
}

void SerialDataSource::openPort(const QString &portName, qint32 baudRate)
{
    closeDevice(serialPort_->isOpen() || state_ == SourceState::Open ||
                state_ == SourceState::Opening || state_ == SourceState::Closing);

    if (portName.trimmed().isEmpty()) {
        const QString message = QStringLiteral("Serial port name is empty.");
        setState(SourceState::Error, message);
        emit errorOccurred(message);
        return;
    }

    setState(SourceState::Opening,
             QStringLiteral("Opening serial port %1.").arg(portName));

    serialPort_->setPortName(portName);
    serialPort_->setBaudRate(baudRate);
    serialPort_->setDataBits(QSerialPort::Data8);
    serialPort_->setParity(QSerialPort::NoParity);
    serialPort_->setStopBits(QSerialPort::OneStop);
    serialPort_->setFlowControl(QSerialPort::NoFlowControl);

    if (!serialPort_->open(QIODevice::ReadOnly)) {
        const QString message = QStringLiteral("Failed to open serial port %1: %2")
                                    .arg(portName, serialPort_->errorString());
        setState(SourceState::Error, message);
        emit errorOccurred(message);
        return;
    }

    setState(SourceState::Open,
             QStringLiteral("Serial port %1 is open.").arg(portName));
}

void SerialDataSource::closePort()
{
    closeDevice(true);
}

bool SerialDataSource::isOpen() const
{
    return serialPort_->isOpen();
}

SourceState SerialDataSource::state() const
{
    return state_;
}

void SerialDataSource::handleReadyRead()
{
    const QByteArray bytes = serialPort_->readAll();
    if (!bytes.isEmpty()) {
        emit bytesReady(bytes, monotonicTimer_.nsecsElapsed());
    }
}

void SerialDataSource::handleSerialError(int errorValue)
{
    const auto error = static_cast<QSerialPort::SerialPortError>(errorValue);
    if (error == QSerialPort::NoError || handlingError_) {
        return;
    }

    if (state_ == SourceState::Opening) {
        return;
    }

    handlingError_ = true;
    const QString message = QStringLiteral("Serial port error: %1")
                                .arg(serialPort_->errorString());

    if (serialPort_->isOpen() || state_ == SourceState::Open) {
        serialPort_->close();
        setState(SourceState::Error, message);
    }

    emit errorOccurred(message);
    handlingError_ = false;
}

void SerialDataSource::setState(SourceState state, const QString &message)
{
    if (state_ == state) {
        return;
    }

    state_ = state;
    emit stateChanged(state_, message);
}

void SerialDataSource::closeDevice(bool publishTransitions)
{
    if (!serialPort_->isOpen()) {
        if (publishTransitions && state_ != SourceState::Closed) {
            setState(SourceState::Closed, QStringLiteral("Serial port is closed."));
        }
        return;
    }

    if (publishTransitions) {
        setState(SourceState::Closing, QStringLiteral("Closing serial port."));
    }

    serialPort_->close();

    if (publishTransitions) {
        setState(SourceState::Closed, QStringLiteral("Serial port is closed."));
    } else {
        state_ = SourceState::Closed;
    }
}
