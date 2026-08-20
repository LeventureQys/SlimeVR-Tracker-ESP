#pragma once

#include "core/imu_frames.h"
#include "protocol/crc16.h"
#include "protocol/protocol_constants.h"

#include <QByteArray>
#include <QByteArrayView>

#include <array>

namespace handstudio::testutil {

inline QByteArray makeRawFrame(quint8 address, quint8 sequence,
                               const std::array<qint16, 9> &values)
{
    QByteArray frame;
    frame.reserve(RawFrameSize);
    frame.append(static_cast<char>(RawFrameHeader0));
    frame.append(static_cast<char>(RawFrameHeader1));
    frame.append(static_cast<char>(address));
    frame.append(static_cast<char>(sequence));
    frame.append(static_cast<char>(RawFramePayloadLength));
    for (qint16 value : values) {
        const quint16 encoded = static_cast<quint16>(value);
        frame.append(static_cast<char>((encoded >> 8U) & 0xFFU));
        frame.append(static_cast<char>(encoded & 0xFFU));
    }
    const quint16 crc = crc16Modbus(QByteArrayView(frame));
    frame.append(static_cast<char>(crc & 0xFFU));
    frame.append(static_cast<char>((crc >> 8U) & 0xFFU));
    return frame;
}

inline QByteArray makeCompleteGroupBytes(quint8 sequence, qint16 marker = 1)
{
    QByteArray group;
    group.reserve(RawFrameSize * ImuSensorCount);
    for (int i = 0; i < ImuSensorCount; ++i) {
        std::array<qint16, 9> values{};
        values[0] = static_cast<qint16>(marker + i);
        values[1] = static_cast<qint16>(marker * 2);
        values[2] = static_cast<qint16>(2048);
        group += makeRawFrame(static_cast<quint8>(0x50 + i), sequence, values);
    }
    return group;
}

inline QByteArray makeRecordingBytes(int groups, qint16 markerSeed = 1)
{
    QByteArray recording;
    recording.reserve(groups * RawFrameSize * ImuSensorCount);
    for (int g = 0; g < groups; ++g) {
        recording += makeCompleteGroupBytes(static_cast<quint8>(g), markerSeed + g);
    }
    return recording;
}

}
