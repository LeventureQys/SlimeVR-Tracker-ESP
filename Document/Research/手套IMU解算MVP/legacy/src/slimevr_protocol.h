#pragma once

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>

namespace SlimeVrProtocol {

// Tracker -> Server packet types. Normal packets carry 3 zero bytes followed
// by a single type byte at offset 3 (firmware sendPacketType layout).
enum class SendPacketType : quint32 {
    HeartBeat = 0,
    Handshake = 3,
    SensorInfo = 15,
    RotationData = 17,
};

// Server -> Tracker packet types. Discovery responses carry type 3 at byte 0
// (without packet number); normal server packets carry the type at offset 3.
enum class ReceivePacketType : quint32 {
    HeartBeat = 1,
    Handshake = 3,
    PingPong = 10,
};

// Identity field defaults for this Windows bridge. Board 20 is the
// "IMU Glove" category; MCU 4 is the desktop-app category used by wrangler.
namespace IdentityDefaults {
inline constexpr quint32 Board = 20;              // BOARD_GLOVE_IMU_SLIMEVR_DEV
inline constexpr quint32 PrimarySensorType = 0;   // SensorTypeID::Unknown
inline constexpr quint32 Mcu = 4;                 // MCU_WRANGLER
inline constexpr quint32 ProtocolVersion = 22;    // matches this repo's firmware line
inline constexpr quint32 TrackerTypeGloveLeft = 1;
inline constexpr quint32 TrackerTypeGloveRight = 2;
}

struct HandshakeIdentity {
    quint32 board = IdentityDefaults::Board;
    quint32 primarySensorType = IdentityDefaults::PrimarySensorType;
    quint32 mcu = IdentityDefaults::Mcu;
    quint32 protocolVersion = IdentityDefaults::ProtocolVersion;
    QString firmwareVersion = QStringLiteral("v0.2.0-bridge");
    std::array<quint8, 6> deviceId{};
    quint8 trackerType = IdentityDefaults::TrackerTypeGloveLeft;
    QString vendorName = QStringLiteral("SlimeVRResearch");
    QString vendorUrl;
    QString productName = QStringLiteral("SixImuGloveBridge");
    QString updateAddress;
    QString updateName;
};

// Builds the tracker handshake datagram (type 3, packet number fixed 0).
// Returns an empty QByteArray when a string exceeds 255 bytes or deviceId is
// malformed, so a partial packet can never be produced.
QByteArray encodeHandshake(const HandshakeIdentity &identity);

// Builds a normal packet header + payload: 3 zero bytes, type byte, 8-byte
// packet number, then payload.
QByteArray encodeNormalPacket(quint32 type, quint64 packetNumber, const QByteArray &payload);

// Builds a heartbeat (type 0) packet.
QByteArray encodeHeartbeat(quint64 packetNumber);

// SensorInfo payload fields. The server only parses up to trackerDataType;
// sensorConfigData is a 16-bit big-endian bitfield in the current protocol,
// so the payload is exactly 8 bytes (20 bytes including the packet header).
struct SensorInfoFields {
    quint8 sensorId = 0;
    quint8 sensorState = 1;    // SENSOR_OK
    quint8 sensorType = 0;     // SensorTypeID::Unknown
    quint16 sensorConfigData = 0;
    quint8 hasCompletedRestCalibration = 0;
    quint8 sensorPosition = 0;
    quint8 trackerDataType = 0; // SENSOR_DATATYPE_ROTATION
};

// Payload-only encoders (callers add the normal packet header).
QByteArray encodeSensorInfo(const SensorInfoFields &fields);
QByteArray encodeRotationData(
    quint8 sensorId,
    quint8 dataType,
    float x,
    float y,
    float z,
    float w,
    quint8 accuracyInfo);

// Server discovery response: byte 0 == 3 followed by "Hey OVR =D 5".
bool isDiscoveryResponse(const QByteArray &datagram);

// Reads the type of a normal server packet (offset 3). Returns nullopt when
// the datagram is too short to contain a type field.
std::optional<quint32> decodeNormalPacketType(const QByteArray &datagram);

// Reads the 64-bit packet number of a normal packet (offset 4).
std::optional<quint64> decodePacketNumber(const QByteArray &datagram);

// Big-endian float helpers shared with the RotationData encoder (S2.2).
void appendFloat32(QByteArray &out, float value);
std::optional<float> readFloat32(const QByteArray &datagram, int offset);

} // namespace SlimeVrProtocol
