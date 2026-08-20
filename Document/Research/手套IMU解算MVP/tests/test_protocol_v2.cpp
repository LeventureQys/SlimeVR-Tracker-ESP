#include "core/imu_frames.h"
#include "core/sensor_id.h"
#include "imu_test_support.h"
#include "protocol/crc16.h"
#include "protocol/frame_stream_parser.h"
#include "protocol/protocol_constants.h"
#include "protocol/sequence_grouper.h"

#include <QByteArrayView>
#include <QSignalSpy>
#include <QtTest>

#include <array>

using namespace handstudio;
using namespace handstudio::testutil;

namespace {

QByteArray documentFrame()
{
    return QByteArray::fromHex("AA5550E112027604F405B7FFFEFFF9FFF5000000030008CD75");
}

RawImuFrame capturedFrame(const QSignalSpy &spy, int index = 0)
{
    return qvariant_cast<RawImuFrame>(spy.at(index).at(0));
}

SixImuSampleGroup capturedGroup(const QSignalSpy &spy, int index = 0)
{
    return qvariant_cast<SixImuSampleGroup>(spy.at(index).at(0));
}

RawImuFrame makeRaw(quint8 address, quint8 sequence, qint16 marker = 0)
{
    RawImuFrame frame;
    frame.sensorId = sensorIdFromAddress(address).value();
    frame.address = address;
    frame.sequence = sequence;
    frame.accelerationRaw[0] = marker;
    frame.allZero = marker == 0;
    return frame;
}

}

class ProtocolV2Test final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<RawImuFrame>();
        qRegisterMetaType<SixImuSampleGroup>();
        qRegisterMetaType<ParserStatistics>();
        qRegisterMetaType<GroupStatistics>();
        qRegisterMetaType<GroupDropReason>();
    }

    void crcKnownVector()
    {
        const QByteArray standardVector("123456789");
        QCOMPARE(crc16Modbus(QByteArrayView(standardVector)), quint16(0x4B37));
        QCOMPARE(crc16Modbus(QByteArrayView(documentFrame()).first(23)), quint16(0x75CD));
    }

    void documentExampleFrame()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        parser.appendBytes(documentFrame(), 123456);

        QCOMPARE(frames.size(), 1);
        const RawImuFrame frame = capturedFrame(frames);
        QCOMPARE(frame.address, quint8(0x50));
        QCOMPARE(frame.sensorId, SensorId::Wrist);
        QCOMPARE(frame.sequence, quint8(0xE1));
        QCOMPARE(frame.accelerationRaw[0], qint16(630));
        QCOMPARE(frame.accelerationRaw[1], qint16(1268));
        QCOMPARE(frame.accelerationRaw[2], qint16(1463));
        QCOMPARE(frame.gyroscopeRaw[0], qint16(-2));
        QCOMPARE(frame.magnetometerRaw[2], qint16(8));
        QVERIFY(frame.crcValid);
        QVERIFY(!frame.allZero);
        QCOMPARE(frame.receivedMonotonicNs, qint64(123456));
        QCOMPARE(parser.statistics().validFrames, quint64(1));
        QCOMPARE(parser.statistics().emittedFrames, quint64(1));
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
        parser.appendBytes(makeRawFrame(0x50, 1, values) + makeRawFrame(0x51, 1, values), 1000);
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
    }

    void unknownAddressCountedNotEmitted()
    {
        FrameStreamParser parser;
        QSignalSpy frames(&parser, &FrameStreamParser::frameParsed);
        const std::array<qint16, 9> values{};
        parser.appendBytes(makeRawFrame(0x60, 5, values), 5);
        QCOMPARE(frames.size(), 0);
        QCOMPARE(parser.statistics().unknownAddressFrames, quint64(1));
        QCOMPARE(parser.statistics().validFrames, quint64(1));
        QCOMPARE(parser.statistics().emittedFrames, quint64(0));
    }

    void unknownAddressDoesNotEnterGrouper()
    {
        FrameStreamParser parser;
        SequenceGrouper grouper(1);
        QObject::connect(&parser, &FrameStreamParser::frameParsed,
                         &grouper, &SequenceGrouper::addFrame);
        const std::array<qint16, 9> values{};
        parser.appendBytes(makeRawFrame(0x60, 5, values), 5);
        QCOMPARE(grouper.pendingCount(), 0);
        for (quint64 count : grouper.statistics().sensorFrameCounts) {
            QCOMPARE(count, quint64(0));
        }
    }

    void arbitraryOrderCompletesGroup()
    {
        SequenceGrouper grouper;
        QSignalSpy complete(&grouper, &SequenceGrouper::groupReady);
        const quint8 addresses[]{0x53, 0x50, 0x55, 0x51, 0x54, 0x52};
        for (int i = 0; i < 6; ++i) {
            grouper.addFrame(makeRaw(addresses[i], 0x10, i + 1));
        }
        QCOMPARE(complete.size(), 1);
        const SixImuSampleGroup group = capturedGroup(complete);
        QVERIFY(group.complete);
        QCOMPARE(group.presentMask, quint8(0x3F));
        QCOMPARE(grouper.statistics().completeGroups, quint64(1));
    }

    void interleavedSequencesRemainIndependent()
    {
        SequenceGrouper grouper;
        QSignalSpy complete(&grouper, &SequenceGrouper::groupReady);
        for (quint8 address = 0x50; address <= 0x55; ++address) {
            grouper.addFrame(makeRaw(address, 1));
            grouper.addFrame(makeRaw(address, 2));
        }
        QCOMPARE(complete.size(), 2);
        QCOMPARE(capturedGroup(complete, 0).sequence, quint8(1));
        QCOMPARE(capturedGroup(complete, 1).sequence, quint8(2));
    }

    void duplicateKeepsFirstFrame()
    {
        SequenceGrouper grouper;
        QSignalSpy complete(&grouper, &SequenceGrouper::groupReady);
        grouper.addFrame(makeRaw(0x50, 3, 10));
        grouper.addFrame(makeRaw(0x50, 3, 20));
        for (quint8 address = 0x51; address <= 0x55; ++address) {
            grouper.addFrame(makeRaw(address, 3));
        }
        QCOMPARE(grouper.statistics().duplicateFrames, quint64(1));
        QCOMPARE(grouper.statistics().sensorDuplicateCounts[0], quint64(1));
        QCOMPARE(capturedGroup(complete).samples[0].accelerationRaw[0], qint16(10));
        QCOMPARE(grouper.statistics().sensorFrameCounts[0], quint64(2));
    }

    void pendingLimitDropsOldest()
    {
        SequenceGrouper grouper(8);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        for (quint8 sequence = 10; sequence < 19; ++sequence) {
            grouper.addFrame(makeRaw(0x50, sequence, sequence));
        }
        QCOMPARE(dropped.size(), 1);
        const auto args = dropped.at(0);
        const SixImuSampleGroup group = qvariant_cast<SixImuSampleGroup>(args.at(0));
        QCOMPARE(group.sequence, quint8(10));
        QVERIFY(!group.complete);
        QCOMPARE(group.presentMask, quint8(0x01));
        QCOMPARE(qvariant_cast<GroupDropReason>(args.at(1)), GroupDropReason::PendingOverflow);
        QCOMPARE(grouper.statistics().partialGroupsDropped, quint64(1));
        QCOMPARE(grouper.statistics().pendingOverflowDrops, quint64(1));
    }

    void pendingNeverExceedsLimit()
    {
        SequenceGrouper grouper(8);
        for (quint8 sequence = 0; sequence < 100; ++sequence) {
            grouper.addFrame(makeRaw(0x50, sequence, sequence));
            QVERIFY(grouper.pendingCount() <= 8);
        }
        QVERIFY(grouper.statistics().pendingPeakGroups <= 8);
    }

    void sequenceWrapAroundUsesArrivalOrder()
    {
        SequenceGrouper grouper(2);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        grouper.addFrame(makeRaw(0x50, 0xFF));
        grouper.addFrame(makeRaw(0x50, 0x00));
        grouper.addFrame(makeRaw(0x50, 0x01));
        QCOMPARE(dropped.size(), 1);
        QCOMPARE(qvariant_cast<SixImuSampleGroup>(dropped.at(0).at(0)).sequence, quint8(0xFF));
    }

    void resetClearsStatistics()
    {
        SequenceGrouper grouper(1);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        grouper.addFrame(makeRaw(0x50, 1));
        grouper.reset();
        QCOMPARE(dropped.size(), 0);
        QCOMPARE(grouper.statistics().sensorFrameCounts[0], quint64(0));
    }
};

QTEST_MAIN(ProtocolV2Test)
#include "test_protocol_v2.moc"
