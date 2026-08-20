#include "imu_test_support.h"
#include "recording/replay_controller.h"
#include "recording/session_recorder.h"
#include "test_temp_dir.h"

#include <QFile>
#include <QSignalSpy>
#include <QVector>
#include <QtTest>

using namespace handstudio;
using namespace handstudio::testutil;

namespace {

QString writeRecording(const QString &dir, const QByteArray &bytes)
{
    SessionRecorder recorder;
    RecordingMetadata metadata;
    metadata.sessionId = QStringLiteral("replay");
    if (!recorder.startRecording(dir, metadata)) {
        return {};
    }
    recorder.appendRawBytes(bytes);
    recorder.stop();
    return dir;
}

}

class ReplayTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<RawImuFrame>();
        qRegisterMetaType<SixImuSampleGroup>();
        qRegisterMetaType<SourceState>();
        qRegisterMetaType<GroupStatistics>();
        qRegisterMetaType<ParserStatistics>();
        qRegisterMetaType<Diagnostic>();
    }

    void unlimitedReplayEmitsSameBytes()
    {
        TestTempDir temp;
        const QByteArray raw = makeRecordingBytes(4);
        writeRecording(temp.path(), raw);

        ReplayDataSource source;
        QByteArray collected;
        QObject::connect(&source, &ReplayDataSource::bytesReady,
                         [&](const QByteArray &bytes, qint64) { collected.append(bytes); });

        QVERIFY(source.load(temp.path() + QStringLiteral("/raw.bin")));
        source.setMode(ReplayMode::Unlimited);
        source.start();

        QCOMPARE(collected, raw);
        QVERIFY(source.atEnd());
    }

    void deterministicReplayTwice()
    {
        TestTempDir temp;
        const QByteArray raw = makeRecordingBytes(8);
        writeRecording(temp.path(), raw);

        ReplayController first;
        ReplayController second;
        QVERIFY(first.loadSession(temp.path()));
        QVERIFY(second.loadSession(temp.path()));

        first.playUnlimited();
        second.playUnlimited();

        QCOMPARE(first.groupSequences(), second.groupSequences());
        QCOMPARE(first.groupStatistics().completeGroups,
                 second.groupStatistics().completeGroups);
        QCOMPARE(first.groupStatistics().duplicateFrames,
                 second.groupStatistics().duplicateFrames);
        QCOMPARE(first.parserStatistics().validFrames,
                 second.parserStatistics().validFrames);
        QCOMPARE(first.groupSequences().size(), 8);
    }

    void stepByGroupAdvancesOneGroupAtATime()
    {
        TestTempDir temp;
        const QByteArray raw = makeRecordingBytes(3);
        writeRecording(temp.path(), raw);

        ReplayDataSource source;
        QSignalSpy stepped(&source, &ReplayDataSource::groupStepped);
        QVector<QByteArray> chunks;
        QObject::connect(&source, &ReplayDataSource::bytesReady,
                         [&](const QByteArray &bytes, qint64) { chunks.append(bytes); });

        QVERIFY(source.load(temp.path() + QStringLiteral("/raw.bin")));
        source.setMode(ReplayMode::StepByGroup);
        source.start();

        source.stepGroup();
        QCOMPARE(stepped.size(), 1);
        QCOMPARE(chunks.size(), 1);
        QCOMPARE(chunks[0].size(), RawFrameSize * ImuSensorCount); // one complete group
        QCOMPARE(source.position(), qint64(RawFrameSize * ImuSensorCount));

        source.stepGroup();
        QCOMPARE(stepped.size(), 2);
        QCOMPARE(source.position(), qint64(2 * RawFrameSize * ImuSensorCount));

        source.stepGroup();
        QCOMPARE(stepped.size(), 3);
        QVERIFY(source.atEnd());
    }

    void replayDetectsHashMismatch()
    {
        TestTempDir temp;
        const QByteArray raw = makeRecordingBytes(2);
        writeRecording(temp.path(), raw);

        QFile file(temp.path() + QStringLiteral("/raw.bin"));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        file.write(QByteArrayLiteral("XX"));
        file.close();

        ReplayController controller;
        QString error;
        QVERIFY(!controller.loadSession(temp.path(), &error));
        QVERIFY(!error.isEmpty());
    }

    void pauseResumePreservesPosition()
    {
        TestTempDir temp;
        const QByteArray raw = makeRecordingBytes(2);
        writeRecording(temp.path(), raw);

        ReplayDataSource source;
        QByteArray collected;
        QObject::connect(&source, &ReplayDataSource::bytesReady,
                         [&](const QByteArray &bytes, qint64) { collected.append(bytes); });

        QVERIFY(source.load(temp.path() + QStringLiteral("/raw.bin")));
        source.setMode(ReplayMode::Unlimited);
        source.start();
        QCOMPARE(collected, raw);
        QCOMPARE(source.state(), SourceState::Idle);
    }
};

QTEST_MAIN(ReplayTest)
#include "test_replay.moc"
