#include "fake_slimevr_server.h"
#include "main_window.h"

#include <QDir>
#include <QLabel>
#include <QSettings>
#include <QUuid>
#include <QtTest>

namespace {

QString temporaryIniPath()
{
    const QString directoryPath = QDir::current().filePath(QStringLiteral("integration-test-data"));
    QDir().mkpath(directoryPath);
    return QDir(directoryPath).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".ini"));
}

void writeSlimeSettings(QSettings &settings, quint16 port)
{
    settings.setValue(QStringLiteral("slimevr/enabled"), true);
    settings.setValue(QStringLiteral("slimevr/discoveryMode"), QStringLiteral("fixed"));
    settings.setValue(QStringLiteral("slimevr/host"), QStringLiteral("127.0.0.1"));
    settings.setValue(QStringLiteral("slimevr/port"), int(port));
    settings.setValue(QStringLiteral("slimevr/handshakeIntervalMs"), 100);
    settings.setValue(QStringLiteral("slimevr/connectionTimeoutMs"), 4000);
    settings.setValue(QStringLiteral("slimevr/sendRateHz"), 100);
    settings.sync();
}

} // namespace

// End-to-end wiring test: MainWindow + demo source + SlimeVR client + pose
// sender against a local fake server. No real network, no production
// settings.
class SlimeVrIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void I01_windowRegistersAndStreamsToFakeServer();
    void I02_disabledByDefaultLeavesNetworkSilent();
};

void SlimeVrIntegrationTest::I01_windowRegistersAndStreamsToFakeServer()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));

    const QString path = temporaryIniPath();
    {
        QSettings settings(path, QSettings::IniFormat);
        writeSlimeSettings(settings, server.port());
    }
    QSettings settings(path, QSettings::IniFormat);
    MainWindow window(&settings);
    window.show();

    QTRY_VERIFY_WITH_TIMEOUT(server.sensorInfoCount() >= 6, 5000);
    QVERIFY(window.findChild<QObject *>(QStringLiteral("slimeStatusLabel")) != nullptr);

    // Demo source feeds the full pipeline; rotations must reach the server.
    window.setDemoModeEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(server.rotationDataCount() >= 6, 5000);
    QCOMPARE(server.sensorInfoPayloads().size() >= 6, true);

    // The status label must reflect the connected session.
    const auto *statusLabel = window.findChild<QLabel *>(QStringLiteral("slimeStatusLabel"));
    QVERIFY(statusLabel != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(
        statusLabel->text().contains(QStringLiteral("已连接")), 3000);

    window.setDemoModeEnabled(false);
    window.close();
}

void SlimeVrIntegrationTest::I02_disabledByDefaultLeavesNetworkSilent()
{
    FakeSlimeVrServer server;
    QVERIFY(server.listen(0));

    const QString path = temporaryIniPath();
    QSettings settings(path, QSettings::IniFormat);
    MainWindow window(&settings);
    window.show();
    window.setDemoModeEnabled(true);

    QTest::qWait(500);
    QCOMPARE(server.handshakeCount(), 0);
    QCOMPARE(server.sensorInfoCount(), 0);

    window.setDemoModeEnabled(false);
    window.close();
}

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    SlimeVrIntegrationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_slimevr_integration.moc"
