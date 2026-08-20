#include "sequence_grouper.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

ImuFrame makeFrame(quint8 address, quint8 sequence, qint16 marker = 0, qint64 timestamp = 0)
{
    ImuFrame frame;
    frame.address = address;
    frame.sensorId = sensorIdFromAddress(address);
    frame.sequence = sequence;
    frame.acceleration.x = marker;
    frame.allZero = marker == 0;
    frame.receivedMonotonicNs = timestamp;
    return frame;
}

ImuSampleGroup capturedGroup(const QSignalSpy &spy, int index = 0)
{
    return qvariant_cast<ImuSampleGroup>(spy.at(index).at(0));
}

}

class SequenceGrouperTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ImuSampleGroup>();
        qRegisterMetaType<GroupStatistics>();
    }

    void arbitraryOrderCompletesGroup()
    {
        SequenceGrouper grouper;
        QSignalSpy complete(&grouper, &SequenceGrouper::completeGroupReady);
        const quint8 addresses[]{0x53, 0x50, 0x55, 0x51, 0x54, 0x52};
        for (int index = 0; index < 6; ++index) {
            grouper.addFrame(makeFrame(addresses[index], 0x10, index + 1, 100 + index));
        }

        QCOMPARE(complete.size(), 1);
        const ImuSampleGroup group = capturedGroup(complete);
        QVERIFY(group.complete);
        QCOMPARE(group.presentMask, quint8(0x3F));
        QCOMPARE(group.emittedMonotonicNs, qint64(105));
        for (const auto &frame : group.frames) {
            QVERIFY(frame.has_value());
        }
        QCOMPARE(grouper.statistics().completeGroups, quint64(1));
        for (quint64 count : grouper.statistics().sensorFrameCounts) {
            QCOMPARE(count, quint64(1));
        }
    }

    void interleavedSequencesRemainIndependent()
    {
        SequenceGrouper grouper;
        QSignalSpy complete(&grouper, &SequenceGrouper::completeGroupReady);
        for (quint8 address = 0x50; address <= 0x55; ++address) {
            grouper.addFrame(makeFrame(address, 1));
            grouper.addFrame(makeFrame(address, 2));
        }
        QCOMPARE(complete.size(), 2);
        QCOMPARE(capturedGroup(complete, 0).sequence, quint8(1));
        QCOMPARE(capturedGroup(complete, 1).sequence, quint8(2));
    }

    void duplicateAddressKeepsFirstFrame()
    {
        SequenceGrouper grouper;
        QSignalSpy complete(&grouper, &SequenceGrouper::completeGroupReady);
        grouper.addFrame(makeFrame(0x50, 3, 10));
        grouper.addFrame(makeFrame(0x50, 3, 20));
        for (quint8 address = 0x51; address <= 0x55; ++address) {
            grouper.addFrame(makeFrame(address, 3));
        }
        QCOMPARE(grouper.statistics().duplicateFrames, quint64(1));
        QCOMPARE(capturedGroup(complete).frames[0]->acceleration.x, qint16(10));
        QCOMPARE(grouper.statistics().sensorFrameCounts[0], quint64(2));
    }

    void pendingLimitDropsOldestArrival()
    {
        SequenceGrouper grouper(8);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        for (quint8 sequence = 10; sequence < 19; ++sequence) {
            grouper.addFrame(makeFrame(0x50, sequence, sequence, sequence));
        }
        QCOMPARE(dropped.size(), 1);
        const ImuSampleGroup group = capturedGroup(dropped);
        QCOMPARE(group.sequence, quint8(10));
        QVERIFY(!group.complete);
        QCOMPARE(group.presentMask, quint8(0x01));
        QCOMPARE(group.emittedMonotonicNs, qint64(18));
        QCOMPARE(grouper.statistics().partialGroups, quint64(1));
    }

    void droppedGroupReportsMissingMask()
    {
        SequenceGrouper grouper(1);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        grouper.addFrame(makeFrame(0x50, 1));
        grouper.addFrame(makeFrame(0x52, 1));
        grouper.addFrame(makeFrame(0x51, 2));
        QCOMPARE(dropped.size(), 1);
        QCOMPARE(capturedGroup(dropped).presentMask, quint8(0x05));
    }

    void sequenceWrapAroundUsesArrivalOrder()
    {
        SequenceGrouper grouper(2);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        grouper.addFrame(makeFrame(0x50, 0xFF));
        grouper.addFrame(makeFrame(0x50, 0x00));
        grouper.addFrame(makeFrame(0x50, 0x01));
        QCOMPARE(dropped.size(), 1);
        QCOMPARE(capturedGroup(dropped).sequence, quint8(0xFF));
    }

    void unknownAddressIsIgnored()
    {
        SequenceGrouper grouper(0);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        ImuFrame unknown = makeFrame(0x60, 4);
        QVERIFY(!unknown.sensorId.has_value());
        grouper.addFrame(unknown);
        QCOMPARE(dropped.size(), 0);
        QCOMPARE(grouper.statistics().partialGroups, quint64(0));
        for (quint64 count : grouper.statistics().sensorFrameCounts) {
            QCOMPARE(count, quint64(0));
        }
    }

    void resetClearsPendingAndStatisticsWithoutDrop()
    {
        SequenceGrouper grouper(1);
        QSignalSpy complete(&grouper, &SequenceGrouper::completeGroupReady);
        QSignalSpy dropped(&grouper, &SequenceGrouper::partialGroupDropped);
        grouper.addFrame(makeFrame(0x50, 1));
        grouper.reset();
        QCOMPARE(dropped.size(), 0);
        QCOMPARE(grouper.statistics().sensorFrameCounts[0], quint64(0));
        for (quint8 address = 0x50; address <= 0x55; ++address) {
            grouper.addFrame(makeFrame(address, 1));
        }
        QCOMPARE(complete.size(), 1);
        QCOMPARE(grouper.statistics().completeGroups, quint64(1));
    }
};

QTEST_MAIN(SequenceGrouperTest)
#include "test_sequence_grouper.moc"
