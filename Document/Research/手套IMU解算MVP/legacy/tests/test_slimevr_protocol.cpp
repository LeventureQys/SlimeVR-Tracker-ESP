#include "slimevr_protocol.h"

#include <QtTest>

#include <limits>

using SlimeVrProtocol::HandshakeIdentity;

namespace {

HandshakeIdentity testIdentity()
{
    HandshakeIdentity identity;
    identity.board = 20;
    identity.primarySensorType = 0;
    identity.mcu = 4;
    identity.protocolVersion = 22;
    identity.firmwareVersion = QStringLiteral("v9.9.9-test");
    identity.deviceId = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    identity.trackerType = 1;
    identity.vendorName = QStringLiteral("Vendor");
    identity.vendorUrl = QStringLiteral("https://example.invalid");
    identity.productName = QStringLiteral("Product");
    identity.updateAddress = QString();
    identity.updateName = QString();
    return identity;
}

quint32 readU32At(const QByteArray &data, int offset)
{
    const auto *bytes = reinterpret_cast<const quint8 *>(data.constData() + offset);
    return (quint32(bytes[0]) << 24U) | (quint32(bytes[1]) << 16U)
        | (quint32(bytes[2]) << 8U) | quint32(bytes[3]);
}

} // namespace

class SlimeVrProtocolTest final : public QObject {
    Q_OBJECT

private slots:
    void handshakeGoldenPrefix();
    void handshakeFieldLayout();
    void handshakeRejectsLongStrings();
    void heartbeatGoldenBytes();
    void discoveryResponseRecognition();
    void normalPacketTypeAndNumberDecoding();
    void float32RoundTrip();
    void float32KnownValues();
    void float32OutOfRangeReads();
};

void SlimeVrProtocolTest::handshakeGoldenPrefix()
{
    const QByteArray encoded = SlimeVrProtocol::encodeHandshake(testIdentity());
    QVERIFY(!encoded.isEmpty());
    QVERIFY(encoded.size() >= 12);
    // Type 3 (u32 BE) followed by packet number 0 (u64 BE).
    QCOMPARE(encoded.left(4), QByteArray::fromHex("00000003"));
    QCOMPARE(encoded.mid(4, 8), QByteArray::fromHex("0000000000000000"));
}

void SlimeVrProtocolTest::handshakeFieldLayout()
{
    const HandshakeIdentity identity = testIdentity();
    const QByteArray encoded = SlimeVrProtocol::encodeHandshake(identity);

    QCOMPARE(readU32At(encoded, 12), quint32(20)); // board
    QCOMPARE(readU32At(encoded, 16), quint32(0));  // primary sensor type
    QCOMPARE(readU32At(encoded, 20), quint32(4));  // mcu
    QCOMPARE(readU32At(encoded, 24), quint32(0));  // legacy reserved
    QCOMPARE(readU32At(encoded, 28), quint32(0));  // legacy reserved
    QCOMPARE(readU32At(encoded, 32), quint32(0));  // legacy reserved
    QCOMPARE(readU32At(encoded, 36), quint32(22)); // protocol version

    int offset = 40;
    const QByteArray firmware = identity.firmwareVersion.toUtf8();
    QCOMPARE(int(quint8(encoded.at(offset))), firmware.size());
    offset += 1;
    QCOMPARE(encoded.mid(offset, firmware.size()), firmware);
    offset += firmware.size();

    QCOMPARE(encoded.mid(offset, 6), QByteArray::fromHex("deadbeef0001"));
    offset += 6;

    QCOMPARE(quint8(encoded.at(offset)), quint8(1)); // tracker type
    offset += 1;

    const auto checkShortString = [&](const QString &value) {
        const QByteArray utf8 = value.toUtf8();
        QCOMPARE(int(quint8(encoded.at(offset))), utf8.size());
        offset += 1;
        QCOMPARE(encoded.mid(offset, utf8.size()), utf8);
        offset += utf8.size();
    };
    checkShortString(identity.vendorName);
    checkShortString(identity.vendorUrl);
    checkShortString(identity.productName);
    checkShortString(identity.updateAddress);
    checkShortString(identity.updateName);

    QCOMPARE(offset, encoded.size());
}

void SlimeVrProtocolTest::handshakeRejectsLongStrings()
{
    HandshakeIdentity identity = testIdentity();
    identity.productName = QString(256, QLatin1Char('x'));
    QVERIFY(SlimeVrProtocol::encodeHandshake(identity).isEmpty());
}

void SlimeVrProtocolTest::heartbeatGoldenBytes()
{
    QCOMPARE(
        SlimeVrProtocol::encodeHeartbeat(0),
        QByteArray::fromHex("000000000000000000000000"));
    QCOMPARE(
        SlimeVrProtocol::encodeHeartbeat(1),
        QByteArray::fromHex("000000000000000000000001"));
    QCOMPARE(
        SlimeVrProtocol::encodeHeartbeat(0x0102030405060708ULL),
        QByteArray::fromHex("000000000102030405060708"));
}

void SlimeVrProtocolTest::discoveryResponseRecognition()
{
    QByteArray valid;
    valid.append(char(3));
    valid.append("Hey OVR =D 5");
    QVERIFY(SlimeVrProtocol::isDiscoveryResponse(valid));

    QByteArray shortResponse = valid.left(valid.size() - 1);
    QVERIFY(!SlimeVrProtocol::isDiscoveryResponse(shortResponse));

    QByteArray wrongType = valid;
    wrongType[0] = char(0);
    QVERIFY(!SlimeVrProtocol::isDiscoveryResponse(wrongType));

    QByteArray wrongText = valid;
    wrongText[5] = char('X');
    QVERIFY(!SlimeVrProtocol::isDiscoveryResponse(wrongText));

    QVERIFY(!SlimeVrProtocol::isDiscoveryResponse(QByteArray()));
}

void SlimeVrProtocolTest::normalPacketTypeAndNumberDecoding()
{
    QByteArray heartbeat;
    heartbeat.append(char(0));
    heartbeat.append(char(0));
    heartbeat.append(char(0));
    heartbeat.append(char(1));
    heartbeat.append(QByteArray::fromHex("000000000000002A"));
    QCOMPARE(
        SlimeVrProtocol::decodeNormalPacketType(heartbeat),
        std::optional<quint32>(1));
    QCOMPARE(
        SlimeVrProtocol::decodePacketNumber(heartbeat),
        std::optional<quint64>(0x2A));

    QCOMPARE(SlimeVrProtocol::decodeNormalPacketType(QByteArray(3, char(0))), std::nullopt);
    QCOMPARE(SlimeVrProtocol::decodePacketNumber(QByteArray(11, char(0))), std::nullopt);
}

void SlimeVrProtocolTest::float32RoundTrip()
{
    const std::array<float, 8> values{
        0.0F, 1.0F, -1.0F, 0.5F, -123.456F, 3.4028235e38F, -3.4028235e38F, 1.17549435e-38F};
    QByteArray buffer;
    for (float value : values) {
        SlimeVrProtocol::appendFloat32(buffer, value);
    }
    int offset = 0;
    for (float value : values) {
        const std::optional<float> decoded = SlimeVrProtocol::readFloat32(buffer, offset);
        QVERIFY(decoded.has_value());
        QCOMPARE(*decoded, value);
        offset += 4;
    }
}

void SlimeVrProtocolTest::float32KnownValues()
{
    QByteArray buffer;
    SlimeVrProtocol::appendFloat32(buffer, 1.0F);
    QCOMPARE(buffer, QByteArray::fromHex("3F800000"));
    buffer.clear();
    SlimeVrProtocol::appendFloat32(buffer, -2.5F);
    QCOMPARE(buffer, QByteArray::fromHex("C0200000"));
}

void SlimeVrProtocolTest::float32OutOfRangeReads()
{
    QByteArray buffer(3, char(0));
    QVERIFY(!SlimeVrProtocol::readFloat32(buffer, 0).has_value());
}

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    SlimeVrProtocolTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_slimevr_protocol.moc"
