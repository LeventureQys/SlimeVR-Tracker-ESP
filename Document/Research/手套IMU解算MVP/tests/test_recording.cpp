#include "recording/bounded_write_queue.h"
#include "recording/recording_schema.h"
#include "recording/session_recorder.h"
#include "test_temp_dir.h"

#include <QFile>
#include <QSignalSpy>
#include <QtTest>

using namespace handstudio;
using namespace handstudio::testutil;

namespace {

QByteArray readAllBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

}

class RecordingTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qRegisterMetaType<RecorderState>();
        qRegisterMetaType<Diagnostic>();
        qRegisterMetaType<RecordingStatistics>();
    }

    void boundedQueueOverflowCounts()
    {
        BoundedWriteQueue queue(2);
        QVERIFY(queue.enqueue(QByteArray("a")));
        QVERIFY(queue.enqueue(QByteArray("bb")));
        QVERIFY(!queue.enqueue(QByteArray("ccc")));
        QCOMPARE(queue.size(), 2);
        QCOMPARE(queue.overflowCount(), quint64(1));
        QCOMPARE(queue.droppedBytes(), quint64(3));
        QCOMPARE(queue.peakItems(), quint64(2));
        QCOMPARE(queue.dequeue().value(), QByteArray("a"));
        QCOMPARE(queue.size(), 1);
    }

    void stateMachineTransitions()
    {
        TestTempDir temp;
        SessionRecorder recorder;
        QSignalSpy states(&recorder, &SessionRecorder::stateChanged);

        RecordingMetadata metadata;
        metadata.sessionId = QStringLiteral("s1");
        QVERIFY(recorder.startRecording(temp.path(), metadata));
        QCOMPARE(recorder.state(), RecorderState::Recording);

        recorder.pause();
        QCOMPARE(recorder.state(), RecorderState::Paused);

        recorder.resume();
        QCOMPARE(recorder.state(), RecorderState::Recording);

        recorder.stop();
        QCOMPARE(recorder.state(), RecorderState::Idle);
    }

    void pauseDoesNotWrite()
    {
        TestTempDir temp;
        SessionRecorder recorder;
        RecordingMetadata metadata;
        metadata.sessionId = QStringLiteral("s2");
        QVERIFY(recorder.startRecording(temp.path(), metadata));

        recorder.appendRawBytes(QByteArrayLiteral("AAAA"));
        recorder.pause();
        recorder.appendRawBytes(QByteArrayLiteral("BBBB"));
        recorder.resume();
        recorder.appendRawBytes(QByteArrayLiteral("CCCC"));
        recorder.stop();

        QCOMPARE(readAllBytes(temp.path() + QStringLiteral("/raw.bin")),
                 QByteArrayLiteral("AAAACCCC"));
        QCOMPARE(recorder.statistics().pausedSkippedBytes, quint64(4));
        QCOMPARE(recorder.statistics().rawBytesWritten, quint64(8));
    }

    void secondStartReturnsError()
    {
        TestTempDir temp;
        SessionRecorder recorder;
        RecordingMetadata metadata;
        metadata.sessionId = QStringLiteral("s3");
        QVERIFY(recorder.startRecording(temp.path(), metadata));

        Diagnostic error;
        QVERIFY(!recorder.startRecording(temp.path(), metadata, &error));
        QCOMPARE(error.code, QStringLiteral("recorder.already-started"));
        QCOMPARE(recorder.state(), RecorderState::Error);

        recorder.stop();
    }

    void diskFailureDiagnostic()
    {
        TestTempDir temp;
        // Create a file, then ask the recorder to make a directory under it -> mkpath fails.
        QFile existingFile(temp.path() + QStringLiteral("/not-a-dir"));
        QVERIFY(existingFile.open(QIODevice::WriteOnly));
        existingFile.close();

        SessionRecorder recorder;
        RecordingMetadata metadata;
        metadata.sessionId = QStringLiteral("s4");
        Diagnostic error;
        QVERIFY(!recorder.startRecording(existingFile.fileName() + QStringLiteral("/sub"), metadata, &error));
        QCOMPARE(recorder.state(), RecorderState::Error);
        QVERIFY(!error.code.isEmpty());
    }

    void rawBytesFidelity()
    {
        TestTempDir temp;
        SessionRecorder recorder;
        RecordingMetadata metadata;
        metadata.sessionId = QStringLiteral("s5");
        QVERIFY(recorder.startRecording(temp.path(), metadata));

        const QByteArray payload = QByteArray::fromHex("AA55DEADBEEF");
        recorder.appendRawBytes(payload.left(4));
        recorder.appendRawBytes(payload.mid(4));
        recorder.stop();

        QCOMPARE(readAllBytes(temp.path() + QStringLiteral("/raw.bin")), payload);
        QCOMPARE(recorder.statistics().rawBytesWritten, quint64(payload.size()));

        const auto metaResult = readMetadataJson(temp.path() + QStringLiteral("/metadata.json"));
        QVERIFY(metaResult.success);
        QCOMPARE(metaResult.metadata.rawBytes, qint64(payload.size()));
        QCOMPARE(metaResult.metadata.rawSha256, computeSha256Hex(payload));
    }

    void queueOverflowWritesDirectly()
    {
        TestTempDir temp;
        SessionRecorder recorder;
        recorder.setMaxWriteQueueItems(0); // every enqueue overflows
        RecordingMetadata metadata;
        metadata.sessionId = QStringLiteral("s6");
        QVERIFY(recorder.startRecording(temp.path(), metadata));

        recorder.appendRawBytes(QByteArrayLiteral("RAW"));
        recorder.stop();

        QCOMPARE(readAllBytes(temp.path() + QStringLiteral("/raw.bin")), QByteArrayLiteral("RAW"));
        QCOMPARE(recorder.statistics().writeQueueOverflowBytes, quint64(3));
    }

    void derivedStreamsAreWrittenAndBounded()
    {
        TestTempDir temp;
        SessionRecorder recorder;
        recorder.setMaxWriteQueueItems(1);
        recorder.setAutoFlush(false);
        RecordingMetadata metadata;
        metadata.sessionId = QStringLiteral("s7");
        QVERIFY(recorder.startRecording(temp.path(), metadata));

        recorder.appendFusedPoses(QByteArrayLiteral("{\"f\":1}\n"));
        recorder.appendFusedPoses(QByteArrayLiteral("{\"f\":2}\n"));
        recorder.appendObservation(QByteArrayLiteral("{\"o\":1}\n"));
        recorder.appendSkeletonFrame(QByteArrayLiteral("{\"s\":1}\n"));
        recorder.stop();

        QCOMPARE(readAllBytes(temp.path() + QStringLiteral("/fused_poses.jsonl")), QByteArrayLiteral("{\"f\":1}\n"));
        QCOMPARE(readAllBytes(temp.path() + QStringLiteral("/observations.jsonl")), QByteArrayLiteral("{\"o\":1}\n"));
        QCOMPARE(readAllBytes(temp.path() + QStringLiteral("/skeleton_frames.jsonl")), QByteArrayLiteral("{\"s\":1}\n"));
        QCOMPARE(recorder.statistics().derivedQueueDroppedItems, quint64(1));
        QCOMPARE(recorder.statistics().fusedPosesWritten, quint64(1));
        QCOMPARE(recorder.statistics().observationsWritten, quint64(1));
        QCOMPARE(recorder.statistics().skeletonFramesWritten, quint64(1));
    }
};

QTEST_MAIN(RecordingTest)
#include "test_recording.moc"
