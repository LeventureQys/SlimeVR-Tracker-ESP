#include "input/serial_data_source.h"

#include <QSerialPort>
#include <QSerialPortInfo>

#include <utility>

namespace handstudio {

namespace {

Diagnostic makeDiagnostic(DiagnosticSeverity severity, QString code, QString message, QString detail = {})
{
    return {severity, std::move(code), std::move(message), std::move(detail), 0};
}

}

SerialDataSource::SerialDataSource(QObject *parent)
    : IDataSource(parent)
    , serialPort_(new QSerialPort(this))
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
    stop();
}

void SerialDataSource::setPortName(const QString &portName)
{
    portName_ = portName;
}

void SerialDataSource::setBaudRate(qint32 baudRate)
{
    baudRate_ = baudRate;
}

QList<SerialPortDescriptor> SerialDataSource::availablePorts()
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

bool SerialDataSource::isOpen() const
{
    return serialPort_->isOpen();
}

SourceState SerialDataSource::state() const
{
    return state_;
}

void SerialDataSource::start()
{
    if (state_ == SourceState::Running || state_ == SourceState::Starting) {
        return;
    }

    if (portName_.trimmed().isEmpty()) {
        const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Error,
                                               QStringLiteral("serial.port.empty"),
                                               QStringLiteral("串口名称为空"));
        setState(SourceState::Error, diagnostic);
        return;
    }

    setState(SourceState::Starting,
             makeDiagnostic(DiagnosticSeverity::Info,
                            QStringLiteral("serial.opening"),
                            QStringLiteral("正在打开串口 %1").arg(portName_)));

    serialPort_->setPortName(portName_);
    serialPort_->setBaudRate(baudRate_);
    serialPort_->setDataBits(QSerialPort::Data8);
    serialPort_->setParity(QSerialPort::NoParity);
    serialPort_->setStopBits(QSerialPort::OneStop);
    serialPort_->setFlowControl(QSerialPort::NoFlowControl);

    if (!serialPort_->open(QIODevice::ReadOnly)) {
        const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Error,
                                               QStringLiteral("serial.open.failed"),
                                               QStringLiteral("打开串口 %1 失败: %2")
                                                   .arg(portName_, serialPort_->errorString()));
        setState(SourceState::Error, diagnostic);
        return;
    }

    setState(SourceState::Running,
             makeDiagnostic(DiagnosticSeverity::Info,
                            QStringLiteral("serial.open"),
                            QStringLiteral("串口 %1 已打开").arg(portName_)));
}

void SerialDataSource::stop()
{
    if (serialPort_->isOpen()) {
        setState(SourceState::Stopping,
                 makeDiagnostic(DiagnosticSeverity::Info,
                                QStringLiteral("serial.closing"),
                                QStringLiteral("正在关闭串口 %1").arg(portName_)));
        serialPort_->close();
    }
    if (state_ != SourceState::Idle) {
        setState(SourceState::Idle,
                 makeDiagnostic(DiagnosticSeverity::Info,
                                QStringLiteral("serial.closed"),
                                QStringLiteral("串口已关闭")));
    }
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
    if (state_ == SourceState::Starting) {
        return;
    }

    handlingError_ = true;
    const auto diagnostic = makeDiagnostic(DiagnosticSeverity::Error,
                                           QStringLiteral("serial.error"),
                                           QStringLiteral("串口错误: %1").arg(serialPort_->errorString()));
    if (serialPort_->isOpen() || state_ == SourceState::Running) {
        serialPort_->close();
    }
    setState(SourceState::Error, diagnostic);
    handlingError_ = false;
}

void SerialDataSource::setState(SourceState state, const Diagnostic &diagnostic)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    emit stateChanged(state_, diagnostic);
}

}
