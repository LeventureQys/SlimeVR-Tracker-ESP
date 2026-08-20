#include "slimevr_udp_client.h"

namespace {
constexpr int BackoffIntervalMs = 1000;
constexpr int StatisticsFlushIntervalMs = 250;
} // namespace

SlimeVrUdpClient::SlimeVrUdpClient(QObject *parent)
    : QObject(parent)
{
    socket_.bind(QHostAddress::AnyIPv4, 0);

    handshakeTimer_.setInterval(settings_.handshakeIntervalMs);
    connect(&handshakeTimer_, &QTimer::timeout, this, &SlimeVrUdpClient::sendHandshakeNow);

    backoffTimer_.setSingleShot(true);
    backoffTimer_.setInterval(BackoffIntervalMs);
    connect(&backoffTimer_, &QTimer::timeout, this, &SlimeVrUdpClient::onBackoffFinished);

    timeoutTimer_.setSingleShot(true);
    connect(&timeoutTimer_, &QTimer::timeout, this, &SlimeVrUdpClient::onConnectionTimeout);

    statisticsTimer_.setInterval(StatisticsFlushIntervalMs);
    connect(&statisticsTimer_, &QTimer::timeout, this, &SlimeVrUdpClient::flushStatistics);

    connect(&socket_, &QUdpSocket::readyRead, this, &SlimeVrUdpClient::onReadyRead);

    monotonicClock_.start();
}

void SlimeVrUdpClient::applySettings(const SlimeVrSettings &settings)
{
    stop();
    settings_ = settings;
    if (settings_.enabled) {
        start();
    }
}

void SlimeVrUdpClient::setIdentity(const SlimeVrProtocol::HandshakeIdentity &identity)
{
    identity_ = identity;
}

void SlimeVrUdpClient::start()
{
    QString error;
    if (!validateSlimeVrSettings(settings_, &error)) {
        setState(SlimeVrConnectionState::Error);
        emit protocolError(error);
        return;
    }
    if (socket_.state() != QAbstractSocket::BoundState) {
        if (!socket_.bind(QHostAddress::AnyIPv4, 0)) {
            setState(SlimeVrConnectionState::Error);
            emit protocolError(QStringLiteral("无法绑定本地 UDP 端口：%1").arg(socket_.errorString()));
            return;
        }
    }

    handshakeTimer_.setInterval(settings_.handshakeIntervalMs);
    statisticsTimer_.start();
    sendHandshakeNow();
    setState(settings_.discoveryMode == SlimeVrDiscoveryMode::Broadcast
                 ? SlimeVrConnectionState::Discovering
                 : SlimeVrConnectionState::Handshaking);
}

void SlimeVrUdpClient::stop()
{
    handshakeTimer_.stop();
    backoffTimer_.stop();
    timeoutTimer_.stop();
    statisticsTimer_.stop();
    socket_.close();
    hasServerAddress_ = false;
    previouslyConnected_ = false;
    setState(SlimeVrConnectionState::Disabled);
}

void SlimeVrUdpClient::reset()
{
    stop();
}

bool SlimeVrUdpClient::isConnected() const
{
    return state_ == SlimeVrConnectionState::Connected && hasServerAddress_;
}

bool SlimeVrUdpClient::sendNormalPacket(quint32 type, const QByteArray &payload)
{
    if (!isConnected()) {
        return false;
    }
    statistics_.packetNumber++;
    const QByteArray datagram = SlimeVrProtocol::encodeNormalPacket(
        type, statistics_.packetNumber, payload);
    if (datagram.isEmpty()) {
        statistics_.sendErrors++;
        markStatisticsDirty();
        return false;
    }
    if (socket_.writeDatagram(datagram, serverAddress_, serverPort_) < 0) {
        statistics_.sendErrors++;
        markStatisticsDirty();
        return false;
    }
    statistics_.datagramsSent++;
    statistics_.lastSentMs = monotonicClock_.elapsed();
    markStatisticsDirty();
    return true;
}

SlimeVrConnectionState SlimeVrUdpClient::state() const
{
    return state_;
}

SlimeVrNetworkStatistics SlimeVrUdpClient::statistics() const
{
    return statistics_;
}

quint16 SlimeVrUdpClient::boundPort() const
{
    return socket_.localPort();
}

QHostAddress SlimeVrUdpClient::serverAddress() const
{
    return serverAddress_;
}

quint16 SlimeVrUdpClient::serverPort() const
{
    return serverPort_;
}

void SlimeVrUdpClient::setState(SlimeVrConnectionState state)
{
    if (state_ == state) {
        return;
    }
    state_ = state;
    markStatisticsDirty();
    emit stateChanged(state_);
}

void SlimeVrUdpClient::sendHandshakeNow()
{
    const QByteArray datagram = SlimeVrProtocol::encodeHandshake(identity_);
    if (datagram.isEmpty()) {
        setState(SlimeVrConnectionState::Error);
        emit protocolError(QStringLiteral("握手数据编码失败，请检查身份字段长度。"));
        return;
    }

    statistics_.handshakeAttempts++;
    markStatisticsDirty();

    qint64 sent = -1;
    if (settings_.discoveryMode == SlimeVrDiscoveryMode::Broadcast) {
        sent = socket_.writeDatagram(datagram, QHostAddress::Broadcast, settings_.port);
        if (hasServerAddress_) {
            // Also retry the last known server address; some networks drop
            // global broadcasts while unicast still works.
            const qint64 remembered = socket_.writeDatagram(datagram, serverAddress_, serverPort_);
            sent = qMax(sent, remembered);
        }
    } else {
        sent = socket_.writeDatagram(datagram, QHostAddress(settings_.host), settings_.port);
    }
    if (sent < 0) {
        statistics_.sendErrors++;
    } else {
        statistics_.datagramsSent++;
        statistics_.lastSentMs = monotonicClock_.elapsed();
    }
    markStatisticsDirty();

    if (state_ == SlimeVrConnectionState::Discovering || state_ == SlimeVrConnectionState::Handshaking) {
        if (!handshakeTimer_.isActive()) {
            handshakeTimer_.start();
        }
    }
}

void SlimeVrUdpClient::onReadyRead()
{
    while (socket_.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(socket_.pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        socket_.readDatagram(datagram.data(), datagram.size(), &from, &fromPort);
        handleDatagram(datagram, from, fromPort);
    }
}

void SlimeVrUdpClient::handleDatagram(const QByteArray &datagram, const QHostAddress &from, quint16 fromPort)
{
    if (state_ == SlimeVrConnectionState::Disabled || state_ == SlimeVrConnectionState::Error) {
        return;
    }

    if (SlimeVrProtocol::isDiscoveryResponse(datagram)) {
        if (state_ == SlimeVrConnectionState::Connected) {
            // A second handshake while connected is ignored, matching firmware.
            return;
        }
        statistics_.handshakeSuccesses++;
        if (previouslyConnected_) {
            statistics_.reconnects++;
        }
        previouslyConnected_ = true;
        serverAddress_ = from;
        serverPort_ = fromPort;
        hasServerAddress_ = true;
        handshakeTimer_.stop();
        backoffTimer_.stop();
        restartTimeoutTimer();
        statistics_.datagramsReceived++;
        statistics_.lastReceivedMs = monotonicClock_.elapsed();
        markStatisticsDirty();
        setState(SlimeVrConnectionState::Connected);
        return;
    }

    statistics_.datagramsReceived++;
    if (state_ != SlimeVrConnectionState::Connected) {
        statistics_.invalidDatagrams++;
        markStatisticsDirty();
        return;
    }
    if (!hasServerAddress_ || from != serverAddress_ || fromPort != serverPort_) {
        statistics_.invalidDatagrams++;
        markStatisticsDirty();
        return;
    }

    const std::optional<quint32> packetType = SlimeVrProtocol::decodeNormalPacketType(datagram);
    if (!packetType) {
        statistics_.invalidDatagrams++;
        markStatisticsDirty();
        return;
    }

    if (*packetType == static_cast<quint32>(SlimeVrProtocol::ReceivePacketType::Handshake)) {
        // Handshake received again while connected: ignore, do not refresh.
        return;
    }

    restartTimeoutTimer();
    statistics_.lastReceivedMs = monotonicClock_.elapsed();

    switch (*packetType) {
    case static_cast<quint32>(SlimeVrProtocol::ReceivePacketType::HeartBeat):
        sendHeartbeatReply();
        break;
    case static_cast<quint32>(SlimeVrProtocol::ReceivePacketType::PingPong):
        if (socket_.writeDatagram(datagram, serverAddress_, serverPort_) < 0) {
            statistics_.sendErrors++;
        } else {
            statistics_.datagramsSent++;
            statistics_.lastSentMs = monotonicClock_.elapsed();
        }
        markStatisticsDirty();
        break;
    default:
        break;
    }
}

void SlimeVrUdpClient::sendHeartbeatReply()
{
    statistics_.packetNumber++;
    const QByteArray datagram = SlimeVrProtocol::encodeHeartbeat(statistics_.packetNumber);
    if (socket_.writeDatagram(datagram, serverAddress_, serverPort_) < 0) {
        statistics_.sendErrors++;
    } else {
        statistics_.datagramsSent++;
        statistics_.lastSentMs = monotonicClock_.elapsed();
    }
    markStatisticsDirty();
}

void SlimeVrUdpClient::onConnectionTimeout()
{
    if (state_ != SlimeVrConnectionState::Connected) {
        return;
    }
    handshakeTimer_.stop();
    setState(SlimeVrConnectionState::Backoff);
    backoffTimer_.start();
}

void SlimeVrUdpClient::onBackoffFinished()
{
    if (state_ != SlimeVrConnectionState::Backoff) {
        return;
    }
    sendHandshakeNow();
    setState(settings_.discoveryMode == SlimeVrDiscoveryMode::Broadcast
                 ? SlimeVrConnectionState::Discovering
                 : SlimeVrConnectionState::Handshaking);
}

void SlimeVrUdpClient::restartTimeoutTimer()
{
    timeoutTimer_.start(settings_.connectionTimeoutMs);
}

void SlimeVrUdpClient::markStatisticsDirty()
{
    statisticsDirty_ = true;
}

void SlimeVrUdpClient::flushStatistics()
{
    if (!statisticsDirty_) {
        return;
    }
    statisticsDirty_ = false;
    emit statisticsChanged(statistics_);
}
