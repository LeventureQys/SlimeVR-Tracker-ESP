#pragma once

#include <QHostAddress>
#include <QObject>
#include <QUdpSocket>

// Minimal loopback stand-in for SlimeVR Server, used by network tests.
// It can answer tracker handshakes with the discovery response, send server
// heartbeats and ping-pong payloads, and counts the client packets it sees.
class FakeSlimeVrServer final : public QObject {
    Q_OBJECT

public:
    explicit FakeSlimeVrServer(QObject *parent = nullptr);
    ~FakeSlimeVrServer() override;

    bool listen(quint16 port = 0);
    quint16 port() const;
    bool isBound() const;

    void setAutoRespondDiscovery(bool enabled);
    void sendHeartbeat();
    void sendPingPong(const QByteArray &payload);

    QHostAddress lastClientAddress() const;
    quint16 lastClientPort() const;

    int handshakeCount() const;
    int heartbeatReplyCount() const;
    int pingPongEchoCount() const;
    int sensorInfoCount() const;
    int rotationDataCount() const;
    QByteArray lastEcho() const;
    QVector<QByteArray> sensorInfoPayloads() const;
    QVector<QByteArray> rotationPayloads() const;

signals:
    void handshakeReceived();
    void heartbeatReplyReceived();
    void pingPongEchoReceived(const QByteArray &echo);

private:
    void onReadyRead();
    void recordClient(const QHostAddress &address, quint16 port);
    static bool isHandshake(const QByteArray &datagram);
    static quint32 normalPacketType(const QByteArray &datagram);

    QUdpSocket socket_;
    bool autoRespondDiscovery_ = true;
    QHostAddress lastClientAddress_;
    quint16 lastClientPort_ = 0;
    int handshakeCount_ = 0;
    int heartbeatReplyCount_ = 0;
    int pingPongEchoCount_ = 0;
    int sensorInfoCount_ = 0;
    int rotationDataCount_ = 0;
    QByteArray lastEcho_;
    QVector<QByteArray> sensorInfoPayloads_;
    QVector<QByteArray> rotationPayloads_;
};
