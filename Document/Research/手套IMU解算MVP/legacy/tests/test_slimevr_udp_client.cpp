#include "fake_slimevr_server.h"
#include "slimevr_protocol.h"
#include "slimevr_udp_client.h"

#include <QSignalSpy>
#include <QtTest>

#include <memory>

namespace {

SlimeVrSettings fixedHostSettings(quint16 port)
{
    SlimeVrSettings settings;
    settings.enabled = true;
    settings.discoveryMode = SlimeVrDiscoveryMode::FixedHost;
    settings.host = QStringLiteral("127.0.0.1");
    settings.port = port;
    settings.handshakeIntervalMs = 100;
    settings.connectionTimeoutMs = 500;
    return settings;
}

SlimeVrProtocol::HandshakeIdentity testIdentity()
{
    SlimeVrProtocol::HandshakeIdentity identity;
    identity.deviceId = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    return identity;
}

} // namespace

class SlimeVrUdpClientTest final : public QObject {
    Q_OBJECT

private slots:
    void C01_fixedHostHandshakeConnects();
    void C02_serverHeartbeatGetsReply();
    void C03_timeoutBacksOffAndReconnects();
    void C04_rogueSourceIsIgnored();
    void C05_stopStopsTrafficAndReleasesSocket();
    void C06_pingPongIsEchoedBack();
    void C07_invalidSettingsAreRejected();
    void C08_broadcastDiscoveryConnects();
    void C09_disabledSettingsKeepClientOff();
};

void SlimeVrUdpClientTest::C01_fixedHostHandshakeConnects()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));

    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));

    QCOMPARE(client.state(), SlimeVrConnectionState::Handshaking);
    QTRY_COMPARE_WITH_TIMEOUT(server.handshakeCount(), 1, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);
    QCOMPARE(client.statistics().handshakeSuccesses, quint64(1));
    QCOMPARE(client.serverPort(), server.port());
    QVERIFY(client.boundPort() != 0);

    client.stop();
    QCOMPARE(client.state(), SlimeVrConnectionState::Disabled);
}

void SlimeVrUdpClientTest::C02_serverHeartbeatGetsReply()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    server.sendHeartbeat();
    QTRY_COMPARE_WITH_TIMEOUT(server.heartbeatReplyCount(), 1, 2000);
    QCOMPARE(client.statistics().packetNumber, quint64(1));

    server.sendHeartbeat();
    QTRY_COMPARE_WITH_TIMEOUT(server.heartbeatReplyCount(), 2, 2000);
    QCOMPARE(client.statistics().packetNumber, quint64(2));

    client.stop();
}

void SlimeVrUdpClientTest::C03_timeoutBacksOffAndReconnects()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    // Server stops sending packets; the client must drop to Backoff after
    // connectionTimeoutMs and then re-handshake (the fake server still
    // answers handshakes, so the session recovers).
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Backoff, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 4000);
    QVERIFY(client.statistics().reconnects >= 1);
    QVERIFY(server.handshakeCount() >= 2);

    client.stop();
}

void SlimeVrUdpClientTest::C04_rogueSourceIsIgnored()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    QUdpSocket rogue;
    QVERIFY(rogue.bind(QHostAddress::AnyIPv4, 0));
    QByteArray heartbeat;
    heartbeat.append(char(0));
    heartbeat.append(char(0));
    heartbeat.append(char(0));
    heartbeat.append(char(1));
    heartbeat.append(QByteArray(8, char(0)));
    rogue.writeDatagram(heartbeat, QHostAddress(QHostAddress::LocalHost), client.boundPort());

    // waitForReadyRead blocks this thread's event loop, which would also
    // starve the client socket; use qWait so the client processes the
    // datagram, then assert it was rejected and nothing was sent back.
    QTest::qWait(150);
    QVERIFY(client.statistics().invalidDatagrams >= 1);
    QVERIFY(!rogue.hasPendingDatagrams());
    QCOMPARE(server.heartbeatReplyCount(), 0);

    client.stop();
}

void SlimeVrUdpClientTest::C05_stopStopsTrafficAndReleasesSocket()
{
    quint16 serverPort = 0;
    {
        FakeSlimeVrServer server;
        QVERIFY(server.listen(0));
        serverPort = server.port();
        SlimeVrUdpClient client;
        client.setIdentity(testIdentity());
        client.applySettings(fixedHostSettings(server.port()));
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

        const int handshakesBefore = server.handshakeCount();
        client.stop();
        QCOMPARE(client.state(), SlimeVrConnectionState::Disabled);
        QTest::qWait(400);
        QCOMPARE(server.handshakeCount(), handshakesBefore);
    }
    // After both sockets are destroyed the server port must be rebindable.
    QUdpSocket freshSocket;
    QVERIFY(freshSocket.bind(QHostAddress::AnyIPv4, serverPort));
}

void SlimeVrUdpClientTest::C06_pingPongIsEchoedBack()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(fixedHostSettings(server.port()));
    QTRY_COMPARE_WITH_TIMEOUT(client.state(), SlimeVrConnectionState::Connected, 2000);

    server.sendPingPong(QByteArray::fromHex("00112233445566778899AABBCCDDEEFF"));
    QTRY_COMPARE_WITH_TIMEOUT(server.pingPongEchoCount(), 1, 2000);
    QVERIFY(server.lastEcho().contains(QByteArray::fromHex("00112233445566778899AABBCCDDEEFF")));

    client.stop();
}

void SlimeVrUdpClientTest::C07_invalidSettingsAreRejected()
{
    SlimeVrUdpClient client;
    QSignalSpy errorSpy(&client, &SlimeVrUdpClient::protocolError);

    SlimeVrSettings settings = fixedHostSettings(6969);
    settings.host.clear();
    client.setIdentity(testIdentity());
    client.applySettings(settings);
    QCOMPARE(client.state(), SlimeVrConnectionState::Error);
    QCOMPARE(errorSpy.count(), 1);

    settings = fixedHostSettings(6969);
    settings.port = 0;
    client.applySettings(settings);
    QCOMPARE(client.state(), SlimeVrConnectionState::Error);
    QCOMPARE(errorSpy.count(), 2);

    QString error;
    settings = fixedHostSettings(6969);
    settings.sendRateHz = 250;
    QVERIFY(!validateSlimeVrSettings(settings, &error));
    QVERIFY(!error.isEmpty());

    client.stop();
}

void SlimeVrUdpClientTest::C08_broadcastDiscoveryConnects()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));

    SlimeVrSettings settings;
    settings.enabled = true;
    settings.discoveryMode = SlimeVrDiscoveryMode::Broadcast;
    settings.port = server.port();
    settings.handshakeIntervalMs = 100;
    settings.connectionTimeoutMs = 600;

    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(settings);
    QCOMPARE(client.state(), SlimeVrConnectionState::Discovering);

    // Some hosts do not deliver 255.255.255.255 back to local sockets; in
    // that case the test environment cannot verify broadcast discovery.
    QElapsedTimer timer;
    timer.start();
    while (client.state() != SlimeVrConnectionState::Connected && timer.elapsed() < 1500) {
        QTest::qWait(50);
    }
    if (client.state() != SlimeVrConnectionState::Connected) {
        QSKIP("本机未回送全局广播，跳过广播发现验证");
    }
    QVERIFY(server.handshakeCount() >= 1);

    client.stop();
}

void SlimeVrUdpClientTest::C09_disabledSettingsKeepClientOff()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));

    SlimeVrSettings settings = fixedHostSettings(server.port());
    settings.enabled = false;
    SlimeVrUdpClient client;
    client.setIdentity(testIdentity());
    client.applySettings(settings);
    QCOMPARE(client.state(), SlimeVrConnectionState::Disabled);
    QTest::qWait(300);
    QCOMPARE(server.handshakeCount(), 0);

    client.stop();
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    SlimeVrUdpClientTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_slimevr_udp_client.moc"
