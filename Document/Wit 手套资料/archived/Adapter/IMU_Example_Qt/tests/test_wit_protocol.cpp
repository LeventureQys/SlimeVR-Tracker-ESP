#include "wit_protocol_parser.h"

#include <QSignalSpy>
#include <QtTest>

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

class TestWitProtocol final : public QObject {
    Q_OBJECT

private slots:
    void parsesMotionValues();
    void handlesSplitAndMergedFrames();
    void resynchronizesAfterNoise();
    void parsesRegisterFrames();
    void mapsBatteryThresholds_data();
    void mapsBatteryThresholds();
    void parsesFirmwareVersion();
    void ignoresUnknownRegisterAndResets();
};

void TestWitProtocol::parsesMotionValues()
{
    WitProtocolParser parser;
    QSignalSpy spy(&parser, &WitProtocolParser::dataUpdated);
    QByteArray frame = motionFrame();
    setRaw(frame, 2, 16384);
    setRaw(frame, 4, -16384);
    setRaw(frame, 8, 8192);
    setRaw(frame, 14, -8192);
    setRaw(frame, 18, 32767);
    parser.appendBytes(frame);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(parser.data().frameCount, quint64(1));
    QVERIFY(qAbs(parser.data().accelerationX - 8.0) < 0.000001);
    QVERIFY(qAbs(parser.data().accelerationY + 8.0) < 0.000001);
    QVERIFY(qAbs(parser.data().angularVelocityX - 500.0) < 0.000001);
    QVERIFY(qAbs(parser.data().angleX + 45.0) < 0.000001);
    QVERIFY(qAbs(parser.data().angleZ - (32767.0 / 32768.0 * 180.0)) < 0.000001);
    QVERIFY(parser.data().lastUpdated.isValid());
}

void TestWitProtocol::handlesSplitAndMergedFrames()
{
    WitProtocolParser parser;
    QSignalSpy spy(&parser, &WitProtocolParser::dataUpdated);
    const QByteArray first = motionFrame();
    const QByteArray second = registerFrame(0x40);
    parser.appendBytes(first.left(7));
    QCOMPARE(spy.count(), 0);
    parser.appendBytes(first.mid(7) + second);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(parser.data().frameCount, quint64(2));
}

void TestWitProtocol::resynchronizesAfterNoise()
{
    WitProtocolParser parser;
    QSignalSpy spy(&parser, &WitProtocolParser::dataUpdated);
    parser.appendBytes(QByteArray::fromHex("001255"));
    QCOMPARE(spy.count(), 0);
    parser.appendBytes(motionFrame().mid(1));
    QCOMPARE(spy.count(), 1);
}

void TestWitProtocol::parsesRegisterFrames()
{
    WitProtocolParser parser;
    QByteArray magnetic = registerFrame(0x3a);
    setRaw(magnetic, 4, 120);
    setRaw(magnetic, 6, -240);
    setRaw(magnetic, 8, 60);
    parser.appendBytes(magnetic);
    QCOMPARE(parser.data().magneticX, 1.0);
    QCOMPARE(parser.data().magneticY, -2.0);
    QCOMPARE(parser.data().magneticZ, 0.5);

    QByteArray temperature = registerFrame(0x40);
    setRaw(temperature, 4, 2534);
    parser.appendBytes(temperature);
    QCOMPARE(parser.data().temperatureCelsius, 25.34);
}

void TestWitProtocol::mapsBatteryThresholds_data()
{
    QTest::addColumn<double>("voltage");
    QTest::addColumn<double>("percent");
    QTest::newRow("above-full") << 3.97 << 100.0;
    QTest::newRow("equal-full") << 3.96 << 90.0;
    QTest::newRow("equal-90") << 3.93 << 75.0;
    QTest::newRow("equal-75") << 3.87 << 60.0;
    QTest::newRow("equal-60") << 3.82 << 50.0;
    QTest::newRow("equal-50") << 3.79 << 40.0;
    QTest::newRow("equal-40") << 3.77 << 30.0;
    QTest::newRow("equal-30") << 3.73 << 20.0;
    QTest::newRow("equal-20") << 3.70 << 15.0;
    QTest::newRow("equal-15") << 3.68 << 10.0;
    QTest::newRow("equal-10") << 3.50 << 5.0;
    QTest::newRow("equal-5") << 3.40 << 0.0;
}

void TestWitProtocol::mapsBatteryThresholds()
{
    QFETCH(double, voltage);
    QFETCH(double, percent);
    QCOMPARE(WitProtocolParser::batteryPercentForVoltage(voltage), percent);
}

void TestWitProtocol::parsesFirmwareVersion()
{
    WitProtocolParser parser;
    const quint32 encoded = 0x80000000U | (123U << 14) | (45U << 8) | 67U;
    QByteArray version = registerFrame(0x2e);
    setRaw(version, 4, static_cast<qint16>(encoded & 0xffffU));
    setRaw(version, 6, static_cast<qint16>((encoded >> 16) & 0xffffU));
    parser.appendBytes(version);
    QCOMPARE(parser.data().firmwareVersion, QStringLiteral("123.45.67"));

    QByteArray invalid = registerFrame(0x2e);
    parser.appendBytes(invalid);
    QCOMPARE(parser.data().firmwareVersion, QStringLiteral("123.45.67"));
}

void TestWitProtocol::ignoresUnknownRegisterAndResets()
{
    WitProtocolParser parser;
    QSignalSpy spy(&parser, &WitProtocolParser::dataUpdated);
    parser.appendBytes(registerFrame(0x11));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(parser.data().frameCount, quint64(0));
    parser.appendBytes(motionFrame().left(5));
    parser.reset();
    parser.appendBytes(motionFrame().mid(5));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(parser.data().frameCount, quint64(0));
}

QTEST_GUILESS_MAIN(TestWitProtocol)
#include "test_wit_protocol.moc"
