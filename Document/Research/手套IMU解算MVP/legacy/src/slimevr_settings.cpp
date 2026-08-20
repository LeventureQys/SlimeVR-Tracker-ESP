#include "slimevr_settings.h"

#include <QHostAddress>

#include <cmath>

namespace {
bool finiteQuaternion(const QQuaternion &q)
{
    return std::isfinite(double(q.scalar())) && std::isfinite(double(q.x()))
        && std::isfinite(double(q.y())) && std::isfinite(double(q.z()));
}

bool unitQuaternion(const QQuaternion &q)
{
    const double norm = std::sqrt(double(q.scalar()) * q.scalar() + double(q.x()) * q.x()
                                  + double(q.y()) * q.y() + double(q.z()) * q.z());
    return std::abs(norm - 1.0) <= 1.0e-3;
}
} // namespace

SlimeVrSettings SlimeVrSettings::defaults()
{
    SlimeVrSettings settings;
    settings.mountings.fill(QQuaternion(1.0F, 0.0F, 0.0F, 0.0F));
    return settings;
}

bool SlimeVrSettings::operator==(const SlimeVrSettings &other) const
{
    return enabled == other.enabled
        && discoveryMode == other.discoveryMode
        && host == other.host
        && port == other.port
        && handshakeIntervalMs == other.handshakeIntervalMs
        && connectionTimeoutMs == other.connectionTimeoutMs
        && sendRateHz == other.sendRateHz
        && gloveSide == other.gloveSide
        && deviceId == other.deviceId
        && mountings == other.mountings;
}

bool validateSlimeVrSettings(const SlimeVrSettings &settings, QString *errorMessage)
{
    const auto fail = [&](const QString &message) {
        if (errorMessage) {
            *errorMessage = message;
        }
        return false;
    };

    if (settings.port == 0) {
        return fail(QStringLiteral("SlimeVR 端口不能为 0。"));
    }
    if (settings.handshakeIntervalMs < 50 || settings.handshakeIntervalMs > 60000) {
        return fail(QStringLiteral("握手间隔必须在 50–60000 ms 之间。"));
    }
    if (settings.connectionTimeoutMs < 200 || settings.connectionTimeoutMs > 60000) {
        return fail(QStringLiteral("连接超时必须在 200–60000 ms 之间。"));
    }
    if (settings.sendRateHz < 1 || settings.sendRateHz > 200) {
        return fail(QStringLiteral("姿态发送率必须在 1–200 Hz 之间。"));
    }
    if (!settings.deviceId.isEmpty() && settings.deviceId.size() != 6) {
        return fail(QStringLiteral("设备标识必须是 6 字节。"));
    }
    for (const QQuaternion &mounting : settings.mountings) {
        if (!finiteQuaternion(mounting) || !unitQuaternion(mounting)) {
            return fail(QStringLiteral("安装旋转必须是有限且单位长度的四元数。"));
        }
    }
    if (settings.discoveryMode == SlimeVrDiscoveryMode::FixedHost) {
        const QString trimmed = settings.host.trimmed();
        if (trimmed.isEmpty()) {
            return fail(QStringLiteral("固定地址模式必须填写 Server 地址。"));
        }
        QHostAddress address;
        if (!address.setAddress(trimmed)) {
            return fail(QStringLiteral("Server 地址不是有效的 IP 地址（S2.1 暂不支持主机名）。"));
        }
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}
