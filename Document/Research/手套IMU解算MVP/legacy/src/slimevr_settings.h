#pragma once

#include <QByteArray>
#include <QQuaternion>
#include <QString>

#include <array>

enum class SlimeVrDiscoveryMode {
    Broadcast,
    FixedHost,
};

enum class GloveSide {
    Left,
    Right,
};

struct SlimeVrSettings {
    bool enabled = false;
    SlimeVrDiscoveryMode discoveryMode = SlimeVrDiscoveryMode::Broadcast;
    QString host;             // only used when discoveryMode == FixedHost
    quint16 port = 6969;      // default SlimeVR server port
    int handshakeIntervalMs = 1000;
    int connectionTimeoutMs = 3000;
    int sendRateHz = 75;      // S2.2 rotation send rate, 50..100
    GloveSide gloveSide = GloveSide::Left;
    QByteArray deviceId;      // exactly 6 bytes when assigned; empty means unassigned
    std::array<QQuaternion, 6> mountings{}; // per-sensor fixed mounting rotations

    static SlimeVrSettings defaults();
    bool operator==(const SlimeVrSettings &other) const;
};

// Returns true when the whole settings group is usable. FixedHost requires a
// parseable host address (IPv4 or IPv6 literal). Every mounting must be
// finite and unit-length.
bool validateSlimeVrSettings(const SlimeVrSettings &settings, QString *errorMessage = nullptr);
