#pragma once

#include <QByteArrayView>
#include <QtGlobal>

namespace SixImuProtocol {
quint16 crc16Modbus(QByteArrayView bytes);
}
