#include "fake_slimevr_server.h"
#include "slimevr_pose_adapter.h"
#include "slimevr_pose_sender.h"
#include "slimevr_protocol.h"
#include "slimevr_udp_client.h"

#include <QtTest>

#include <cmath>
#include <limits>

namespace {

SixImuSnapshot makeSnapshot(qint64 timestampNs)
{
    SixImuSnapshot snapshot;
    snapshot.sequence = 1;
    snapshot.updatedMonotonicNs = timestampNs;
    for (int index = 0; index < 6; ++index) {
        SensorPose &pose = snapshot.poses[size_t(index)];
        pose.sensorId = static_cast<SensorId>(int(SensorId::Wrist) + index);
        pose.valid = true;
        pose.updatedMonotonicNs = timestampNs;
        pose.worldOrientation = QQuaternion(1.0F, 0.0F, 0.0F, 0.0F);
        pose.relativeOrientation = QQuaternion(0.0F, 0.0F, 0.0F, 1.0F);
    }
    return snapshot;
}

void setPoseOrientation(SixImuSnapshot &snapshot, int index, float w, float x, float y, float z)
{
    SensorPose &pose = snapshot.poses[size_t(index)];
    pose.worldOrientation = QQuaternion(w, x, y, z);
    pose.relativeOrientation = QQuaternion(0.0F, 0.0F, 0.0F, 1.0F);
}

SlimeVrSettings fixedHostSettings(quint16 port)
{
    SlimeVrSettings settings;
    settings.enabled = true;
    settings.discoveryMode = SlimeVrDiscoveryMode::FixedHost;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = port;
    settings.handshakeIntervalMs = 100;
    settings.connectionTimeoutMs = 4000;
    return settings;
}

SlimeVrProtocol::HandshakeIdentity testIdentity()
{
    SlimeVrProtocol::HandshakeIdentity identity;
    identity.deviceId = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    return identity;
}

struct RotationPayload {
    quint8 sensorId = 0;
    quint8 dataType = 0;
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 0.0F;
    quint8 accuracy = 0;
};

RotationPayload parseRotation(const QByteArray &payload)
{
    RotationPayload result;
    if (payload.size() < 19) {
        return result;
    }
    result.sensorId = quint8(payload.at(0));
    result.dataType = quint8(payload.at(1));
    result.x = *SlimeVrProtocol::readFloat32(payload, 2);
    result.y = *SlimeVrProtocol::readFloat32(payload, 6);
    result.z = *SlimeVrProtocol::readFloat32(payload, 10);
    result.w = *SlimeVrProtocol::readFloat32(payload, 14);
    result.accuracy = quint8(payload.at(18));
    return result;
}

QVector<RotationPayload> rotationPayloadsForSensor(
    const FakeSlimeVrServer &server, quint8 sensorId)
{
    QVector<RotationPayload> result;
    for (const QByteArray &payload : server.rotationPayloads()) {
        const RotationPayload parsed = parseRotation(payload);
        if (parsed.sensorId == sensorId) {
            result.append(parsed);
        }
    }
    return result;
}

} // namespace

class SlimeVrPoseSenderTest final : public QObject {
    Q_OBJECT

private slots:
    void A01_adapterMapsSixValidSamples();
    void A02_adapterRejectsInvalidPose();
    void A03_adapterRejectsNonFiniteAndBadNorm();
    void A04_adapterUsesWorldOrientation();
    void S01_connectSendsSensorInfoAndRotations();
    void S02_rotationQuaternionWireFormat();
    void S03_invalidSensorIsSkippedIndependently();
    void S04_staleSnapshotStopsRotations();
    void S05_stopStopsTraffic();
    void S06_reconnectResendsSensorInfo();
    void S07_sensorInfoPeriodicResend();
    void S08_highRateInputIsThrottled();
};

void SlimeVrPoseSenderTest::A01_adapterMapsSixValidSamples()
{
    SlimeVrPoseAdapter adapter(GloveSide::Left);
    const SixImuSnapshot snapshot = makeSnapshot(1000);
    const auto samples = adapter.adapt(snapshot);
    for (int index = 0; index < 6; ++index) {
        QVERIFY2(samples[size_t(index)].valid, qPrintable(QString::number(index)));
        QCOMPARE(samples[size_t(index)].sensorId, quint8(index));
        QCOMPARE(samples[size_t(index)].orientation, QQuaternion(1.0F, 0.0F, 0.0F, 0.0F));
    }
}

void SlimeVrPoseSenderTest::A02_adapterRejectsInvalidPose()
{
    SlimeVrPoseAdapter adapter;
    SixImuSnapshot snapshot = makeSnapshot(1000);
    snapshot.poses[3].valid = false;
    const auto samples = adapter.adapt(snapshot);
    QVERIFY(!samples[3].valid);
    QVERIFY(samples[0].valid);
    QVERIFY(samples[5].valid);
}

void SlimeVrPoseSenderTest::A03_adapterRejectsNonFiniteAndBadNorm()
{
    SlimeVrPoseAdapter adapter;
    SixImuSnapshot snapshot = makeSnapshot(1000);
    snapshot.poses[1].worldOrientation = QQuaternion(
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F);
    snapshot.poses[2].worldOrientation = QQuaternion(0.0F, 0.0F, 0.0F, 0.0F);
    snapshot.poses[3].worldOrientation = QQuaternion(5.0F, 0.0F, 0.0F, 0.0F);
    const auto samples = adapter.adapt(snapshot);
    QVERIFY(!samples[1].valid);
    QVERIFY(!samples[2].valid);
    QVERIFY(!samples[3].valid);
    QVERIFY(samples[0].valid);
}

void SlimeVrPoseSenderTest::A04_adapterUsesWorldOrientation()
{
    SlimeVrPoseAdapter adapter;
    SixImuSnapshot snapshot = makeSnapshot(1000);
    setPoseOrientation(snapshot, 0, 0.5F, 0.5F, 0.5F, 0.5F);
    const auto samples = adapter.adapt(snapshot);
    QCOMPARE(samples[0].orientation, QQuaternion(0.5F, 0.5F, 0.5F, 0.5F));
}

void SlimeVrPoseSenderTest::S01_connectSendsSensorInfoAndRotations()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));

    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));

    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(60000);
    sender.setSendRateHz(100);
    sender.start();

    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.sensorInfoCount() >= 6, 2000);

    sender.submitSnapshot(makeSnapshot(1));
    QTRY_VERIFY_WITH_TIMEOUT(server.rotationDataCount() >= 6, 2000);

    // SensorInfo payload layout: sensorId, state, type, config(u16), rest, position...
    // The first registration batch must cover the six expected positions.
    const QVector<QByteArray> infos = server.sensorInfoPayloads();
    QVERIFY(infos.size() >= 6);
    const std::array<quint8, 6> expectedPositions{17, 23, 26, 29, 32, 35};
    for (int index = 0; index < 6; ++index) {
        const QByteArray &payload = infos[size_t(index)];
        QCOMPARE(payload.size(), 8);
        QCOMPARE(quint8(payload.at(0)), quint8(index)); // Wrist..Pinky order
        QCOMPARE(payload.at(1), char(1));              // SENSOR_OK
        QCOMPARE(payload.at(6), char(expectedPositions[size_t(index)]));
        QCOMPARE(payload.at(7), char(0));              // rotation data type
    }

    const auto wrist = rotationPayloadsForSensor(server, 0);
    QVERIFY(!wrist.isEmpty());
    QCOMPARE(wrist.constLast().dataType, quint8(1));
    QCOMPARE(wrist.constLast().w, 1.0F);
    QCOMPARE(wrist.constLast().x, 0.0F);

    sender.stop();
    client.stop();
}

void SlimeVrPoseSenderTest::S02_rotationQuaternionWireFormat()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(60000);
    sender.setSendRateHz(100);
    sender.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    SixImuSnapshot snapshot = makeSnapshot(1);
    setPoseOrientation(snapshot, 2, 0.5F, 0.5F, 0.5F, 0.5F);
    sender.submitSnapshot(snapshot);

    QTRY_VERIFY_WITH_TIMEOUT(!rotationPayloadsForSensor(server, 2).isEmpty(), 2000);
    const RotationPayload parsed = rotationPayloadsForSensor(server, 2).constLast();
    // QQuaternion(0.5, 0.5, 0.5, 0.5) => x,y,z,w all 0.5.
    QCOMPARE(parsed.x, 0.5F);
    QCOMPARE(parsed.y, 0.5F);
    QCOMPARE(parsed.z, 0.5F);
    QCOMPARE(parsed.w, 0.5F);

    // Raw bytes: x f32 BE = 3F000000.
    const QVector<RotationPayload> sensorTwo = rotationPayloadsForSensor(server, 2);
    QVERIFY(!sensorTwo.isEmpty());
    const QByteArray rawPayload = [&]() {
        for (const QByteArray &payload : server.rotationPayloads()) {
            if (parseRotation(payload).sensorId == 2) {
                return payload;
            }
        }
        return QByteArray();
    }();
    QCOMPARE(rawPayload.mid(2, 4), QByteArray::fromHex("3F000000"));

    sender.stop();
    client.stop();
}

void SlimeVrPoseSenderTest::S03_invalidSensorIsSkippedIndependently()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(60000);
    sender.setSendRateHz(100);
    sender.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    SixImuSnapshot snapshot = makeSnapshot(1);
    snapshot.poses[3].worldOrientation = QQuaternion(
        std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F, 0.0F);
    sender.submitSnapshot(snapshot);

    QTRY_VERIFY_WITH_TIMEOUT(!rotationPayloadsForSensor(server, 0).isEmpty(), 2000);
    QVERIFY(rotationPayloadsForSensor(server, 3).isEmpty());
    QVERIFY(!rotationPayloadsForSensor(server, 4).isEmpty());
    QVERIFY(sender.statistics().rotationSkipped[3] >= 1);
    QCOMPARE(sender.statistics().rotationSkipped[0], quint64(0));

    sender.stop();
    client.stop();
}

void SlimeVrPoseSenderTest::S04_staleSnapshotStopsRotations()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(200);
    sender.setSendRateHz(100);
    sender.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    sender.submitSnapshot(makeSnapshot(1));
    QTRY_VERIFY_WITH_TIMEOUT(server.rotationDataCount() >= 6, 2000);
    QTest::qWait(400); // let the snapshot age past maxAgeMs
    const int countBefore = server.rotationDataCount();
    QTest::qWait(300);
    QCOMPARE(server.rotationDataCount(), countBefore);

    sender.stop();
    client.stop();
}

void SlimeVrPoseSenderTest::S05_stopStopsTraffic()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(60000);
    sender.setSendRateHz(100);
    sender.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);
    sender.submitSnapshot(makeSnapshot(1));
    QTRY_VERIFY_WITH_TIMEOUT(server.rotationDataCount() >= 6, 2000);

    sender.stop();
    const int countBefore = server.rotationDataCount();
    QTest::qWait(300);
    QCOMPARE(server.rotationDataCount(), countBefore);

    client.stop();
}

void SlimeVrPoseSenderTest::S06_reconnectResendsSensorInfo()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(60000);
    sender.setSendRateHz(100);
    sender.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.sensorInfoCount() >= 6, 2000);
    const int countBefore = server.sensorInfoCount();

    client.stop();
    client.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.sensorInfoCount() >= countBefore + 6, 2000);

    sender.stop();
    client.stop();
}

void SlimeVrPoseSenderTest::S07_sensorInfoPeriodicResend()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(60000);
    sender.setSendRateHz(100);
    sender.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(server.sensorInfoCount() >= 6, 2000);

    // Without reconnect the sender re-registers every second.
    QTRY_VERIFY_WITH_TIMEOUT(server.sensorInfoCount() >= 12, 3000);

    sender.stop();
    client.stop();
}

void SlimeVrPoseSenderTest::S08_highRateInputIsThrottled()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    SlimeVrPoseSender sender(&client);
    sender.setMaxAgeMs(60000);
    sender.setSendRateHz(75);
    sender.start();
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    for (int i = 0; i < 40; ++i) {
        sender.submitSnapshot(makeSnapshot(qint64(i)));
    }
    QTest::qWait(400);
    // 40 snapshots would mean 240 rotations without throttling; at 75 Hz
    // over ~0.4 s the sender emits far fewer, but at least one full group.
    QVERIFY(server.rotationDataCount() >= 6);
    QVERIFY(server.rotationDataCount() < 240);

    sender.stop();
    client.stop();
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    SlimeVrPoseSenderTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_slimevr_pose_sender.moc"
