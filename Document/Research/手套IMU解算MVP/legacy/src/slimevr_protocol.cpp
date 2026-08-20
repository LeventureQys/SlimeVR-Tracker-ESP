#include "slimevr_protocol.h"

#include <QByteArrayView>

#include <cstring>

namespace {

void appendU16(QByteArray &out, quint16 value)
{
    out.append(char((value >> 8U) & 0xFFU));
    out.append(char(value & 0xFFU));
}

void appendU32(QByteArray &out, quint32 value)
{
    out.append(char((value >> 24U) & 0xFFU));
    out.append(char((value >> 16U) & 0xFFU));
    out.append(char((value >> 8U) & 0xFFU));
    out.append(char(value & 0xFFU));
}

void appendU64(QByteArray &out, quint64 value)
{
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.append(char((value >> quint64(shift)) & 0xFFU));
    }
}

bool appendShortString(QByteArray &out, const QByteArray &utf8)
{
    if (utf8.size() > 255) {
        return false;
    }
    out.append(char(utf8.size()));
    out.append(utf8);
    return true;
}

std::optional<quint32> readU32(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 4 > data.size()) {
        return std::nullopt;
    }
    const auto *bytes = reinterpret_cast<const quint8 *>(data.constData() + offset);
    return (quint32(bytes[0]) << 24U) | (quint32(bytes[1]) << 16U)
        | (quint32(bytes[2]) << 8U) | quint32(bytes[3]);
}

std::optional<quint64> readU64(const QByteArray &data, int offset)
{
    if (offset < 0 || offset + 8 > data.size()) {
        return std::nullopt;
    }
    const auto *bytes = reinterpret_cast<const quint8 *>(data.constData() + offset);
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8U) | quint64(bytes[i]);
    }
    return value;
}

} // namespace

namespace SlimeVrProtocol {

QByteArray encodeHandshake(const HandshakeIdentity &identity)
{
    QByteArray out;
    out.reserve(96);
    appendU32(out, static_cast<quint32>(SendPacketType::Handshake));
    appendU64(out, 0); // handshake always uses packet number 0
    appendU32(out, identity.board);
    appendU32(out, identity.primarySensorType);
    appendU32(out, identity.mcu);
    appendU32(out, 0); // legacy IMU info, unused
    appendU32(out, 0); // legacy IMU info, unused
    appendU32(out, 0); // legacy IMU info, unused
    appendU32(out, identity.protocolVersion);
    if (!appendShortString(out, identity.firmwareVersion.toUtf8())) {
        return {};
    }
    for (quint8 byte : identity.deviceId) {
        out.append(char(byte));
    }
    out.append(char(identity.trackerType));
    if (!appendShortString(out, identity.vendorName.toUtf8())
        || !appendShortString(out, identity.vendorUrl.toUtf8())
        || !appendShortString(out, identity.productName.toUtf8())
        || !appendShortString(out, identity.updateAddress.toUtf8())
        || !appendShortString(out, identity.updateName.toUtf8())) {
        return {};
    }
    return out;
}

QByteArray encodeNormalPacket(quint32 type, quint64 packetNumber, const QByteArray &payload)
{
    if (type > 0xFFU) {
        return {};
    }
    QByteArray out;
    out.reserve(12 + payload.size());
    out.append(char(0));
    out.append(char(0));
    out.append(char(0));
    out.append(char(type));
    appendU64(out, packetNumber);
    out.append(payload);
    return out;
}

QByteArray encodeHeartbeat(quint64 packetNumber)
{
    return encodeNormalPacket(static_cast<quint32>(SendPacketType::HeartBeat), packetNumber, {});
}

QByteArray encodeSensorInfo(const SensorInfoFields &fields)
{
    QByteArray out;
    out.reserve(8);
    out.append(char(fields.sensorId));
    out.append(char(fields.sensorState));
    out.append(char(fields.sensorType));
    appendU16(out, fields.sensorConfigData);
    out.append(char(fields.hasCompletedRestCalibration));
    out.append(char(fields.sensorPosition));
    out.append(char(fields.trackerDataType));
    return out;
}

QByteArray encodeRotationData(
    quint8 sensorId,
    quint8 dataType,
    float x,
    float y,
    float z,
    float w,
    quint8 accuracyInfo)
{
    QByteArray out;
    out.reserve(20);
    out.append(char(sensorId));
    out.append(char(dataType));
    appendFloat32(out, x);
    appendFloat32(out, y);
    appendFloat32(out, z);
    appendFloat32(out, w);
    out.append(char(accuracyInfo));
    return out;
}

bool isDiscoveryResponse(const QByteArray &datagram)
{
    static const QByteArray expected = QByteArrayLiteral("Hey OVR =D 5");
    if (datagram.size() < 1 + expected.size() || static_cast<quint8>(datagram.at(0)) != 3) {
        return false;
    }
    return QByteArrayView(datagram).mid(1, expected.size()) == QByteArrayView(expected);
}

std::optional<quint32> decodeNormalPacketType(const QByteArray &datagram)
{
    // Normal packets carry 3 zero bytes followed by a single type byte at
    // offset 3, matching the firmware sendPacketType() layout.
    if (datagram.size() < 4) {
        return std::nullopt;
    }
    return quint32(quint8(datagram.at(3)));
}

std::optional<quint64> decodePacketNumber(const QByteArray &datagram)
{
    return readU64(datagram, 4);
}

void appendFloat32(QByteArray &out, float value)
{
    quint32 bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(out, bits);
}

std::optional<float> readFloat32(const QByteArray &datagram, int offset)
{
    const std::optional<quint32> bits = readU32(datagram, offset);
    if (!bits) {
        return std::nullopt;
    }
    float value = 0.0F;
    static_assert(sizeof(value) == sizeof(*bits), "float must be 32-bit");
    std::memcpy(&value, &*bits, sizeof(value));
    return value;
}

} // namespace SlimeVrProtocol
