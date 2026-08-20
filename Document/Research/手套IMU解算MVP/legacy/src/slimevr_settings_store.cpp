#include "slimevr_settings_store.h"

#include <QRandomGenerator>

#include <cmath>

namespace {
const QString EnabledKey = QStringLiteral("slimevr/enabled");
const QString DiscoveryModeKey = QStringLiteral("slimevr/discoveryMode");
const QString HostKey = QStringLiteral("slimevr/host");
const QString PortKey = QStringLiteral("slimevr/port");
const QString HandshakeIntervalKey = QStringLiteral("slimevr/handshakeIntervalMs");
const QString ConnectionTimeoutKey = QStringLiteral("slimevr/connectionTimeoutMs");
const QString SendRateKey = QStringLiteral("slimevr/sendRateHz");
const QString GloveSideKey = QStringLiteral("slimevr/gloveSide");
const QString DeviceIdKey = QStringLiteral("slimevr/deviceId");
const QString MountingKeyPrefix = QStringLiteral("slimevr/mount");

QString mountingKey(int index)
{
    return QStringLiteral("slimevr/mount%1").arg(index);
}

bool validMounting(const QQuaternion &q)
{
    bool finite = std::isfinite(double(q.scalar())) && std::isfinite(double(q.x()))
        && std::isfinite(double(q.y())) && std::isfinite(double(q.z()));
    if (!finite) {
        return false;
    }
    const double norm = std::sqrt(double(q.scalar()) * q.scalar() + double(q.x()) * q.x()
                                  + double(q.y()) * q.y() + double(q.z()) * q.z());
    return std::abs(norm - 1.0) <= 1.0e-3;
}

QQuaternion loadMounting(const QSettings &settings, int index, const QQuaternion &fallback)
{
    if (!settings.contains(mountingKey(index))) {
        return fallback;
    }
    const QStringList parts = settings.value(mountingKey(index)).toString().split(QLatin1Char(','));
    if (parts.size() != 4) {
        return fallback;
    }
    bool ok = false;
    const double w = parts.at(0).trimmed().toDouble(&ok);
    const double x = ok ? parts.at(1).trimmed().toDouble(&ok) : 0.0;
    const double y = ok ? parts.at(2).trimmed().toDouble(&ok) : 0.0;
    const double z = ok ? parts.at(3).trimmed().toDouble(&ok) : 0.0;
    if (!ok) {
        return fallback;
    }
    const QQuaternion parsed{float(w), float(x), float(y), float(z)};
    return validMounting(parsed) ? parsed : fallback;
}

QString formatNumber(double value)
{
    return QString::number(value, 'g', 9);
}

int loadInt(const QSettings &settings, const QString &key, int fallback)
{
    if (!settings.contains(key)) {
        return fallback;
    }
    bool ok = false;
    const int value = settings.value(key).toInt(&ok);
    return ok ? value : fallback;
}

bool loadBool(const QSettings &settings, const QString &key, bool fallback)
{
    if (!settings.contains(key)) {
        return fallback;
    }
    const QVariant value = settings.value(key);
    if (value.metaType().id() == QMetaType::Bool) {
        return value.toBool();
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) {
        return true;
    }
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) {
        return false;
    }
    return fallback;
}

bool validDeviceId(const QByteArray &id)
{
    return id.size() == 6 && id != QByteArray(6, char(0));
}

QByteArray generateDeviceId()
{
    QByteArray id;
    id.resize(6);
    do {
        for (int i = 0; i < id.size(); ++i) {
            id[i] = char(QRandomGenerator::global()->bounded(256));
        }
    } while (!validDeviceId(id));
    return id;
}
} // namespace

SlimeVrSettingsStore::SlimeVrSettingsStore(QSettings &settings)
    : settings_(settings)
{
}

SlimeVrSettings SlimeVrSettingsStore::load()
{
    const SlimeVrSettings defaults = SlimeVrSettings::defaults();
    SlimeVrSettings result = defaults;
    result.enabled = loadBool(settings_, EnabledKey, defaults.enabled);
    const QString discoveryMode = settings_.value(DiscoveryModeKey).toString().trimmed().toLower();
    if (discoveryMode == QStringLiteral("fixed")) {
        result.discoveryMode = SlimeVrDiscoveryMode::FixedHost;
    } else if (discoveryMode == QStringLiteral("broadcast")) {
        result.discoveryMode = SlimeVrDiscoveryMode::Broadcast;
    } else {
        result.discoveryMode = defaults.discoveryMode;
    }
    result.host = settings_.value(HostKey).toString();
    result.port = quint16(loadInt(settings_, PortKey, defaults.port));
    result.handshakeIntervalMs = loadInt(settings_, HandshakeIntervalKey, defaults.handshakeIntervalMs);
    result.connectionTimeoutMs = loadInt(settings_, ConnectionTimeoutKey, defaults.connectionTimeoutMs);
    result.sendRateHz = loadInt(settings_, SendRateKey, defaults.sendRateHz);
    const QString gloveSide = settings_.value(GloveSideKey).toString().trimmed().toLower();
    if (gloveSide == QStringLiteral("right")) {
        result.gloveSide = GloveSide::Right;
    } else {
        result.gloveSide = GloveSide::Left;
    }
    result.deviceId = QByteArray::fromHex(settings_.value(DeviceIdKey).toString().toUtf8());
    const QQuaternion identity(1.0F, 0.0F, 0.0F, 0.0F);
    for (int index = 0; index < 6; ++index) {
        result.mountings[size_t(index)] = loadMounting(settings_, index, identity);
    }

    bool needsSave = false;
    if (!validDeviceId(result.deviceId)) {
        result.deviceId = generateDeviceId();
        needsSave = true;
    }
    if (needsSave) {
        settings_.setValue(DeviceIdKey, QString::fromLatin1(result.deviceId.toHex()));
        settings_.sync();
    }

    return validateSlimeVrSettings(result) ? result : defaults;
}

bool SlimeVrSettingsStore::save(const SlimeVrSettings &value, QString *errorMessage)
{
    QString error;
    if (!validateSlimeVrSettings(value, &error)) {
        if (errorMessage) {
            *errorMessage = error;
        }
        return false;
    }
    settings_.setValue(EnabledKey, value.enabled);
    settings_.setValue(
        DiscoveryModeKey,
        value.discoveryMode == SlimeVrDiscoveryMode::FixedHost
            ? QStringLiteral("fixed")
            : QStringLiteral("broadcast"));
    settings_.setValue(HostKey, value.host);
    settings_.setValue(PortKey, int(value.port));
    settings_.setValue(HandshakeIntervalKey, value.handshakeIntervalMs);
    settings_.setValue(ConnectionTimeoutKey, value.connectionTimeoutMs);
    settings_.setValue(SendRateKey, value.sendRateHz);
    settings_.setValue(
        GloveSideKey,
        value.gloveSide == GloveSide::Right ? QStringLiteral("right") : QStringLiteral("left"));
    settings_.setValue(DeviceIdKey, QString::fromLatin1(value.deviceId.toHex()));
    for (int index = 0; index < 6; ++index) {
        const QQuaternion &mounting = value.mountings[size_t(index)];
        settings_.setValue(
            mountingKey(index),
            QStringLiteral("%1,%2,%3,%4")
                .arg(formatNumber(double(mounting.scalar())),
                     formatNumber(double(mounting.x())),
                     formatNumber(double(mounting.y())),
                     formatNumber(double(mounting.z()))));
    }
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SlimeVR 设置写入失败，QSettings 状态码：%1").arg(int(settings_.status()));
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

void SlimeVrSettingsStore::clear()
{
    settings_.remove(QStringLiteral("slimevr"));
    settings_.sync();
}
