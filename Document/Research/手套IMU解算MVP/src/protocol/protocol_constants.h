#pragma once

#include <QtGlobal>

namespace handstudio {

inline constexpr quint8 RawFrameHeader0 = 0xAA;
inline constexpr quint8 RawFrameHeader1 = 0x55;
inline constexpr int RawFrameSize = 25;
inline constexpr quint8 RawFramePayloadLength = 0x12;
inline constexpr int ImuSensorCount = 6;
inline constexpr int DefaultPendingGroupLimit = 8;
inline constexpr double NominalSampleRateHz = 200.0;
inline constexpr double NominalSamplePeriodSeconds = 0.005;
inline constexpr int RawBytesPerSecond = RawFrameSize * ImuSensorCount * 200;

}
