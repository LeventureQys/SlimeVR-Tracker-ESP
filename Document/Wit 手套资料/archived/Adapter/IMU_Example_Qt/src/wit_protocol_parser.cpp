#include "wit_protocol_parser.h"

#include <QDateTime>

namespace {

constexpr int frameSize = 20;

quint16 unsignedValue(char low, char high)
{
    return static_cast<quint16>(static_cast<quint8>(low)) |
           (static_cast<quint16>(static_cast<quint8>(high)) << 8);
}

}

WitProtocolParser::WitProtocolParser(QObject *parent)
    : QObject(parent)
{
}

void WitProtocolParser::appendBytes(const QByteArray &bytes)
{
    buffer_.append(bytes);

    while (true) {
        int headerIndex = -1;
        for (int index = 0; index + 1 < buffer_.size(); ++index) {
            const auto first = static_cast<quint8>(buffer_.at(index));
            const auto second = static_cast<quint8>(buffer_.at(index + 1));
            if (first == 0x55 && (second == 0x61 || second == 0x71)) {
                headerIndex = index;
                break;
            }
        }

        if (headerIndex < 0) {
            if (!buffer_.isEmpty() && static_cast<quint8>(buffer_.back()) == 0x55) {
                buffer_ = QByteArray(1, static_cast<char>(0x55));
            } else {
                buffer_.clear();
            }
            return;
        }

        if (headerIndex > 0) {
            buffer_.remove(0, headerIndex);
        }
        if (buffer_.size() < frameSize) {
            return;
        }

        const QByteArray frame = buffer_.left(frameSize);
        buffer_.remove(0, frameSize);
        if (parseFrame(frame)) {
            ++data_.frameCount;
            data_.lastUpdated = QDateTime::currentDateTime();
            emit dataUpdated(data_);
        }
    }
}

void WitProtocolParser::reset()
{
    buffer_.clear();
    data_ = {};
}

const ImuData &WitProtocolParser::data() const
{
    return data_;
}

double WitProtocolParser::batteryPercentForVoltage(double voltage)
{
    if (voltage > 3.96) return 100.0;
    if (voltage > 3.93) return 90.0;
    if (voltage > 3.87) return 75.0;
    if (voltage > 3.82) return 60.0;
    if (voltage > 3.79) return 50.0;
    if (voltage > 3.77) return 40.0;
    if (voltage > 3.73) return 30.0;
    if (voltage > 3.70) return 20.0;
    if (voltage > 3.68) return 15.0;
    if (voltage > 3.50) return 10.0;
    if (voltage > 3.40) return 5.0;
    return 0.0;
}

bool WitProtocolParser::parseFrame(const QByteArray &frame)
{
    const auto type = static_cast<quint8>(frame.at(1));
    if (type == 0x61) {
        data_.accelerationX = signedValue(frame.at(2), frame.at(3)) / 32768.0 * 16.0;
        data_.accelerationY = signedValue(frame.at(4), frame.at(5)) / 32768.0 * 16.0;
        data_.accelerationZ = signedValue(frame.at(6), frame.at(7)) / 32768.0 * 16.0;
        data_.angularVelocityX = signedValue(frame.at(8), frame.at(9)) / 32768.0 * 2000.0;
        data_.angularVelocityY = signedValue(frame.at(10), frame.at(11)) / 32768.0 * 2000.0;
        data_.angularVelocityZ = signedValue(frame.at(12), frame.at(13)) / 32768.0 * 2000.0;
        data_.angleX = signedValue(frame.at(14), frame.at(15)) / 32768.0 * 180.0;
        data_.angleY = signedValue(frame.at(16), frame.at(17)) / 32768.0 * 180.0;
        data_.angleZ = signedValue(frame.at(18), frame.at(19)) / 32768.0 * 180.0;
        return true;
    }

    const auto reg = static_cast<quint8>(frame.at(2));
    if (reg == 0x3a) {
        data_.magneticX = signedValue(frame.at(4), frame.at(5)) / 120.0;
        data_.magneticY = signedValue(frame.at(6), frame.at(7)) / 120.0;
        data_.magneticZ = signedValue(frame.at(8), frame.at(9)) / 120.0;
        return true;
    }
    if (reg == 0x64) {
        data_.batteryPercent = batteryPercentForVoltage(
            signedValue(frame.at(4), frame.at(5)) / 100.0);
        return true;
    }
    if (reg == 0x40) {
        data_.temperatureCelsius = signedValue(frame.at(4), frame.at(5)) / 100.0;
        return true;
    }
    if (reg == 0x2e) {
        const quint32 version = static_cast<quint32>(unsignedValue(frame.at(4), frame.at(5))) |
                                (static_cast<quint32>(unsignedValue(frame.at(6), frame.at(7))) << 16);
        if ((version & 0x80000000U) != 0) {
            const quint32 major = (version >> 14) & 0x1ffffU;
            const quint32 minor = (version >> 8) & 0x3fU;
            const quint32 patch = version & 0xffU;
            data_.firmwareVersion = QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
        }
        return true;
    }
    return false;
}

qint16 WitProtocolParser::signedValue(char low, char high)
{
    return static_cast<qint16>(unsignedValue(low, high));
}
