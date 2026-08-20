#include "protocol/crc16.h"

namespace handstudio {

quint16 crc16Modbus(QByteArrayView bytes)
{
    quint16 crc = 0xFFFF;
    for (char byte : bytes) {
        crc ^= static_cast<quint8>(byte);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) != 0U ? static_cast<quint16>((crc >> 1U) ^ 0xA001U)
                                    : static_cast<quint16>(crc >> 1U);
        }
    }
    return crc;
}

}
