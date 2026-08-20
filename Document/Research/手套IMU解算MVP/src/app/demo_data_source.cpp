#include "app/demo_data_source.h"

#include "protocol/crc16.h"

#include <QtEndian>

namespace handstudio {
namespace {

QByteArray frameBytes(quint8 address, quint8 sequence, qint16 phase)
{
    QByteArray frame(25, '\0');
    frame[0] = char(0xAA);
    frame[1] = char(0x55);
    frame[2] = char(address);
    frame[3] = char(sequence);
    frame[4] = char(18);
    const qint16 values[9] = {phase, qint16(phase / 2), 16384, 0, qint16(phase / 8), 0,
                              32, 4, 41};
    for (int index = 0; index < 9; ++index) {
        qToBigEndian<qint16>(values[index], reinterpret_cast<uchar *>(frame.data() + 5 + index * 2));
    }
    const quint16 crc = crc16Modbus(frame.left(23));
    qToLittleEndian<quint16>(crc, reinterpret_cast<uchar *>(frame.data() + 23));
    return frame;
}

Diagnostic stateDiagnostic(QString code, QString message)
{
    return {DiagnosticSeverity::Info, std::move(code), std::move(message), {}, 0};
}

}

DemoDataSource::DemoDataSource(QObject *parent)
    : IDataSource(parent)
{
    timer_.setInterval(5);
    connect(&timer_, &QTimer::timeout, this, &DemoDataSource::emitGroup);
}

SourceState DemoDataSource::state() const noexcept { return state_; }

void DemoDataSource::start()
{
    if (state_ == SourceState::Running) return;
    elapsed_.restart();
    state_ = SourceState::Running;
    emit stateChanged(state_, stateDiagnostic(QStringLiteral("demo.running"), QStringLiteral("演示数据源已启动")));
    timer_.start();
}

void DemoDataSource::stop()
{
    timer_.stop();
    if (state_ == SourceState::Idle) return;
    state_ = SourceState::Idle;
    emit stateChanged(state_, stateDiagnostic(QStringLiteral("demo.idle"), QStringLiteral("演示数据源已停止")));
}

void DemoDataSource::emitGroup()
{
    QByteArray bytes;
    bytes.reserve(150);
    const qint16 phase = qint16((int(sequence_) % 80 - 40) * 30);
    for (quint8 offset = 0; offset < 6; ++offset) {
        bytes.append(frameBytes(quint8(0x50 + offset), sequence_, qint16(phase + offset * 8)));
    }
    emit bytesReady(bytes, elapsed_.nsecsElapsed());
    ++sequence_;
}

}
