#include "imu/wit_protocol_parser.h"

#include <QSignalSpy>
#include <QtTest>

using handdemo::imu::WitProtocolParser;

namespace {
void setRaw(QByteArray &frame, int offset, qint16 value)
{
    const auto raw = static_cast<quint16>(value);
    frame[offset] = static_cast<char>(raw & 0xff);
    frame[offset + 1] = static_cast<char>((raw >> 8) & 0xff);
}
QByteArray motionFrame()
{
    QByteArray frame(20, 0);
    frame[0] = static_cast<char>(0x55);
    frame[1] = static_cast<char>(0x61);
    return frame;
}
QByteArray registerFrame(quint8 reg)
{
    QByteArray frame(20, 0);
    frame[0] = static_cast<char>(0x55);
    frame[1] = static_cast<char>(0x71);
    frame[2] = static_cast<char>(reg);
    return frame;
}
}

class WitProtocolTest final : public QObject {
    Q_OBJECT
private slots:
    void parsesMotionAndCountsFrames();
    void handlesSplitMergedAndNoise();
    void parsesRegistersAndReset();
};

void WitProtocolTest::parsesMotionAndCountsFrames()
{
    WitProtocolParser parser;
    QByteArray frame = motionFrame();
    setRaw(frame, 2, 16384);
    setRaw(frame, 8, -8192);
    setRaw(frame, 14, 8192);
    parser.appendBytes(frame);
    QCOMPARE(parser.data().accelerationX, 8.0);
    QCOMPARE(parser.data().angularVelocityX, -500.0);
    QCOMPARE(parser.data().angleX, 45.0);
    QCOMPARE(parser.data().motionFrameCount, quint64(1));
}

void WitProtocolTest::handlesSplitMergedAndNoise()
{
    WitProtocolParser parser;
    QSignalSpy spy(&parser, &WitProtocolParser::dataUpdated);
    const QByteArray first = motionFrame();
    parser.appendBytes(QByteArray::fromHex("001255") + first.mid(1, 5));
    QCOMPARE(spy.count(), 0);
    parser.appendBytes(first.mid(6) + registerFrame(0x40));
    QCOMPARE(spy.count(), 2);
}

void WitProtocolTest::parsesRegistersAndReset()
{
    WitProtocolParser parser;
    QByteArray temperature = registerFrame(0x40);
    setRaw(temperature, 4, 2534);
    parser.appendBytes(temperature);
    QCOMPARE(parser.data().temperatureCelsius, 25.34);
    QCOMPARE(WitProtocolParser::batteryPercentForVoltage(3.97), 100.0);
    parser.reset();
    QCOMPARE(parser.data().frameCount, quint64(0));
}

QTEST_GUILESS_MAIN(WitProtocolTest)
#include "test_wit_protocol.moc"
