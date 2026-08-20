#include "crc16.h"

#include "protocol/crc16.h"

namespace SixImuProtocol {

// Adaptation layer: delegates to the single canonical implementation in
// src/protocol/. No duplicate CRC logic lives here.
quint16 crc16Modbus(QByteArrayView bytes)
{
    return handstudio::crc16Modbus(bytes);
}

}
