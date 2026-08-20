#pragma once

#include <QByteArrayView>
#include <QtGlobal>

namespace handstudio {

// CRC16-Modbus (polynomial 0xA001, init 0xFFFF, little-endian output).
quint16 crc16Modbus(QByteArrayView bytes);

}
