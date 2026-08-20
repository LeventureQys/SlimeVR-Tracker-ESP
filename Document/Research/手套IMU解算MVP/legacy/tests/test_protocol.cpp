#include "crc16.h"
#include "frame_stream_parser.h"

#include <QSignalSpy>
#include <QtTest>

#include <array>

namespace {

QByteArray documentFrame()
{
    return QByteArray::fromHex("AA5550E112027604F405B7FFFEFFF9FFF5000000030008CD75");
}

QByteArray makeFrame(quint8 address, quint8 sequence, const std::array<qint16, 9> &values)
{
    QByteArray frame;
    frame.reserve(25);
    frame.append(char(0xAA));
    frame.append(char(0x55));
    frame.append(char(address));
    frame.append(char(sequence));
    frame.append(char(0x12));
    for (qint16 value : values) {
        const quint16 encoded = static_cast<quint16>(value);
        frame.append(char((encoded >> 8U) & 0xFFU));
        frame.append(char(encoded & 0xFFU));
    }
    const quint16 crc = SixImuProtocol::crc16Modbus(QByteArrayView(frame));
    frame.append(char(crc & 0xFFU));
    frame.append(char((crc >> 8U) & 0xFFU));
    return frame;
}

ImuFrame capturedFrame(const QSignalSpy &spy, int index = 0)
{
    return qvariant_cast<ImuFrame>(spy.at(index).at(0));
}

}

class ProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ImuFrame>();
        qRegisterMetaType<ParserStatistics>();
    }

    void crcKnownVector()
    {
        const QByteArray standardVector("123456789");
        QCOMPARE(SixImuProtocol::crc16Modbus(QByteArrayView(standardVector)), quint16(0x4B37));
        QCOMPARE(SixImuProtocol::crc16Modbus(QByteArrayView(documentFrame()).first(23)), quint16(0x75CD));
    }

    void documentExampleFrame()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(documentFrame(), 123456);

        QCOMPARE(frames.size(), 1);
        const ImuFrame frame = capturedFrame(frames);
        QCOMPARE(frame.address, quint8(0x50));
        QCOMPARE(frame.sensorId, std::optional<SensorId>(SensorId::Wrist));
        QCOMPARE(frame.sequence, quint8(0xE1));
        QCOMPARE(frame.acceleration.x, qint16(630));
        QCOMPARE(frame.acceleration.y, qint16(1268));
        QCOMPARE(frame.acceleration.z, qint16(1463));
        QCOMPARE(frame.gyroscope.x, qint16(-2));
        QCOMPARE(frame.gyroscope.y, qint16(-7));
        QCOMPARE(frame.gyroscope.z, qint16(-11));
        QCOMPARE(frame.magnetometer.x, qint16(0));
        QCOMPARE(frame.magnetometer.y, qint16(3));
        QCOMPARE(frame.magnetometer.z, qint16(8));
        QVERIFY(!frame.allZero);
        QCOMPARE(frame.receivedMonotonicNs, qint64(123456));
        QCOMPARE(parser.statistics().validFrames, quint64(1));
    }

    void signedBoundariesAndBigEndian()
    {
        const std::array<qint16, 9> values{0, 1, -1, 0x1234, qint16(-0x1234),
                                           std::numeric_limits<qint16>::min(),
                                           std::numeric_limits<qint16>::max(), 256, -256};
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(makeFrame(0x51, 7, values), 77);

        const ImuFrame frame = capturedFrame(frames);
        QCOMPARE(frame.acceleration.x, values[0]);
        QCOMPARE(frame.acceleration.y, values[1]);
        QCOMPARE(frame.acceleration.z, values[2]);
        QCOMPARE(frame.gyroscope.x, values[3]);
        QCOMPARE(frame.gyroscope.y, values[4]);
        QCOMPARE(frame.gyroscope.z, values[5]);
        QCOMPARE(frame.magnetometer.x, values[6]);
        QCOMPARE(frame.magnetometer.y, values[7]);
        QCOMPARE(frame.magnetometer.z, values[8]);
    }

    void byteByByteChunks()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        const QByteArray frame = documentFrame();
        for (char byte : frame) {
            parser.appendBytes(QByteArray(1, byte), 900);
        }
        QCOMPARE(frames.size(), 1);
        QCOMPARE(parser.statistics().inputBytes, quint64(25));
    }

    void multipleFramesInOneChunk()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        const std::array<qint16, 9> values{};
        parser.appendBytes(makeFrame(0x50, 1, values) + makeFrame(0x51, 1, values), 1000);
        QCOMPARE(frames.size(), 2);
        QCOMPARE(parser.statistics().validFrames, quint64(2));
    }

    void leadingNoiseAndTrailingHeaderByte()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(QByteArray::fromHex("0102FFAA"), 1);
        QCOMPARE(parser.statistics().discardedBytes, quint64(3));
        parser.appendBytes(documentFrame().mid(1), 2);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(capturedFrame(frames).receivedMonotonicNs, qint64(2));
    }

    void invalidLengthRecovers()
    {
        QByteArray bad = documentFrame();
        bad[4] = char(0x11);
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(bad + documentFrame(), 3);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(parser.statistics().invalidLength, quint64(1));
        QCOMPARE(parser.statistics().discardedBytes, quint64(25));
    }

    void invalidCrcRecovers()
    {
        QByteArray bad = documentFrame();
        bad[23] ^= char(0x01);
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(bad + documentFrame(), 4);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(parser.statistics().invalidCrc, quint64(1));
        QCOMPARE(parser.statistics().discardedBytes, quint64(25));
    }

    void unknownAddressIsCountedNotEmitted()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(makeFrame(0x60, 5, {}), 5);
        QCOMPARE(frames.size(), 0);
        QCOMPARE(parser.statistics().unknownAddressFrames, quint64(1));
        QCOMPARE(parser.statistics().validFrames, quint64(1));
    }

    void allZeroFrameIsValid()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(makeFrame(0x55, 6, {}), 6);
        QCOMPARE(frames.size(), 1);
        QVERIFY(capturedFrame(frames).allZero);
        QCOMPARE(parser.statistics().validFrames, quint64(1));
    }

    void resetClearsBufferAndStatistics()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(documentFrame().left(12), 7);
        parser.reset();
        QCOMPARE(parser.statistics().inputBytes, quint64(0));
        parser.appendBytes(documentFrame().mid(12), 8);
        QCOMPARE(frames.size(), 0);
        QCOMPARE(parser.statistics().discardedBytes, quint64(13));
        parser.appendBytes(documentFrame(), 9);
        QCOMPARE(frames.size(), 1);
    }
};

QTEST_MAIN(ProtocolTest)
#include "test_protocol.moc"
