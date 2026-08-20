#pragma once

#include <QtGlobal>

namespace SixImuProtocol {
inline constexpr quint8 Header0 = 0xAA;
inline constexpr quint8 Header1 = 0x55;
inline constexpr int FrameSize = 25;
inline constexpr quint8 PayloadLength = 0x12;
inline constexpr int SensorCount = 6;
inline constexpr int DefaultPendingGroupLimit = 8;
inline constexpr int NominalSampleRateHz = 200;
inline constexpr double NominalSamplePeriodSeconds = 0.005;
}
