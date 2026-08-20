#include "fake_slimevr_server.h"

#include "slimevr_protocol.h"

namespace {
QByteArray discoveryResponse()
{
    QByteArray response;
    response.append(char(3));
    response.append("Hey OVR =D 5");
    return response;
}
} // namespace

FakeSlimeVrServer::FakeSlimeVrServer(QObject *parent)
    : QObject(parent)
{
    connect(&socket_, &QUdpSocket::readyRead, this, &FakeSlimeVrServer::onReadyRead);
}

FakeSlimeVrServer::~FakeSlimeVrServer() = default;

bool FakeSlimeVrServer::listen(quint16 port)
{
    return socket_.bind(QHostAddress::AnyIPv4, port);
}

quint16 FakeSlimeVrServer::port() const
{
    return socket_.localPort();
}

bool FakeSlimeVrServer::isBound() const
{
    return socket_.state() == QAbstractSocket::BoundState;
}

void FakeSlimeVrServer::setAutoRespondDiscovery(bool enabled)
{
    autoRespondDiscovery_ = enabled;
}

void FakeSlimeVrServer::sendHeartbeat()
{
    QByteArray packet;
    packet.append(char(0));
    packet.append(char(0));
    packet.append(char(0));
    packet.append(char(1)); // HeartBeat (server -> tracker)
    for (int i = 0; i < 8; ++i) {
        packet.append(char(0));
    }
    const qint64 sent = socket_.writeDatagram(packet, lastClientAddress_, lastClientPort_);
    Q_UNUSED(sent);
}

void FakeSlimeVrServer::sendPingPong(const QByteArray &payload)
{
    QByteArray packet;
    packet.append(char(0));
    packet.append(char(0));
    packet.append(char(0));
    packet.append(char(10)); // PingPong
    for (int i = 0; i < 8; ++i) {
        packet.append(char(0));
    }
    packet.append(payload);
    socket_.writeDatagram(packet, lastClientAddress_, lastClientPort_);
}

QHostAddress FakeSlimeVrServer::lastClientAddress() const
{
    return lastClientAddress_;
}

quint16 FakeSlimeVrServer::lastClientPort() const
{
    return lastClientPort_;
}

int FakeSlimeVrServer::handshakeCount() const
{
    return handshakeCount_;
}

int FakeSlimeVrServer::heartbeatReplyCount() const
{
    return heartbeatReplyCount_;
}

int FakeSlimeVrServer::pingPongEchoCount() const
{
    return pingPongEchoCount_;
}

int FakeSlimeVrServer::sensorInfoCount() const
{
    return sensorInfoCount_;
}

int FakeSlimeVrServer::rotationDataCount() const
{
    return rotationDataCount_;
}

QByteArray FakeSlimeVrServer::lastEcho() const
{
    return lastEcho_;
}

QVector<QByteArray> FakeSlimeVrServer::sensorInfoPayloads() const
{
    return sensorInfoPayloads_;
}

QVector<QByteArray> FakeSlimeVrServer::rotationPayloads() const
{
    return rotationPayloads_;
}

void FakeSlimeVrServer::onReadyRead()
{
    while (socket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(socket_.pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        socket_.readDatagram(datagram.data(), datagram.size(), &from, &fromPort);
        recordClient(from, fromPort);
        if (isHandshake(datagram)) {
            handshakeCount_++;
            if (autoRespondDiscovery_) {
                socket_.writeDatagram(discoveryResponse(), from, fromPort);
            }
            emit handshakeReceived();
            continue;
        }
        switch (normalPacketType(datagram)) {
        case 0: // HeartBeat reply from tracker
            heartbeatReplyCount_++;
            emit heartbeatReplyReceived();
            break;
        case 10: // PingPong echo from tracker
            pingPongEchoCount_++;
            lastEcho_ = datagram;
            emit pingPongEchoReceived(datagram);
            break;
        case 15: // SensorInfo
            sensorInfoCount_++;
            sensorInfoPayloads_.append(datagram.mid(12));
            break;
        case 17: // RotationData
            rotationDataCount_++;
            rotationPayloads_.append(datagram.mid(12));
            break;
        default:
            break;
        }
    }
}

void FakeSlimeVrServer::recordClient(const QHostAddress &address, quint16 port)
{
    lastClientAddress_ = address;
    lastClientPort_ = port;
}

bool FakeSlimeVrServer::isHandshake(const QByteArray &datagram)
{
    if (datagram.size() < 4) {
        return false;
    }
    const auto *bytes = reinterpret_cast<const quint8 *>(datagram.constData());
    return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 3;
}

quint32 FakeSlimeVrServer::normalPacketType(const QByteArray &datagram)
{
    if (datagram.size() < 4) {
        return 0xFFFFFFFFU;
    }
    const auto *bytes = reinterpret_cast<const quint8 *>(datagram.constData());
    return (quint32(bytes[0]) << 24U) | (quint32(bytes[1]) << 16U)
        | (quint32(bytes[2]) << 8U) | quint32(bytes[3]);
}
