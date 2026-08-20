#pragma once

#include "slimevr_protocol.h"
#include "slimevr_settings.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

enum class SlimeVrConnectionState {
    Disabled,
    Discovering,
    Handshaking,
    Connected,
    Backoff,
    Error,
};

struct SlimeVrNetworkStatistics {
    quint64 datagramsSent = 0;
    quint64 datagramsReceived = 0;
    quint64 sendErrors = 0;
    quint64 invalidDatagrams = 0;
    quint64 handshakeAttempts = 0;
    quint64 handshakeSuccesses = 0;
    quint64 reconnects = 0;
    quint64 packetNumber = 0;
    qint64 lastSentMs = 0;     // monotonic clock, 0 = never
    qint64 lastReceivedMs = 0; // monotonic clock, 0 = never
};

// SlimeVR UDP session state machine. Socket activity runs on the Qt event
// loop of the owning thread; no blocking calls. When the server stops
// responding the client backs off and re-handshakes; rotation data is a
// later stage's concern.
class SlimeVrUdpClient final : public QObject {
    Q_OBJECT

public:
    explicit SlimeVrUdpClient(QObject *parent = nullptr);

    void applySettings(const SlimeVrSettings &settings);
    void setIdentity(const SlimeVrProtocol::HandshakeIdentity &identity);
    void start();
    void stop();
    void reset();

    bool isConnected() const;
    bool sendNormalPacket(quint32 type, const QByteArray &payload);

    SlimeVrConnectionState state() const;
    SlimeVrNetworkStatistics statistics() const;
    quint16 boundPort() const;
    QHostAddress serverAddress() const;
    quint16 serverPort() const;

signals:
    void stateChanged(SlimeVrConnectionState state);
    void statisticsChanged(const SlimeVrNetworkStatistics &statistics);
    void protocolError(const QString &message);

private:
    void setState(SlimeVrConnectionState state);
    void sendHandshakeNow();
    void handleDatagram(const QByteArray &datagram, const QHostAddress &from, quint16 fromPort);
    void onReadyRead();
    void onConnectionTimeout();
    void onBackoffFinished();
    void sendHeartbeatReply();
    void markStatisticsDirty();
    void flushStatistics();
    void restartTimeoutTimer();

    SlimeVrSettings settings_;
    SlimeVrProtocol::HandshakeIdentity identity_;
    SlimeVrConnectionState state_ = SlimeVrConnectionState::Disabled;
    SlimeVrNetworkStatistics statistics_;

    QUdpSocket socket_;
    QHostAddress serverAddress_;
    quint16 serverPort_ = 0;
    bool hasServerAddress_ = false;
    bool previouslyConnected_ = false;
    bool statisticsDirty_ = false;

    QTimer handshakeTimer_;
    QTimer backoffTimer_;
    QTimer timeoutTimer_;
    QTimer statisticsTimer_;
    QElapsedTimer monotonicClock_;
};
