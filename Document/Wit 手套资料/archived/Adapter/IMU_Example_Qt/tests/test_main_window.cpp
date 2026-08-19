#include "main_window.h"

#include <QCheckBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

class TestMainWindow final : public QObject {
    Q_OBJECT

private slots:
    void exposesExpectedControls();
    void displaysSnapshot();
    void togglesDemoMode();
    void handlesConnectWithoutSelection();
};

void TestMainWindow::exposesExpectedControls()
{
    MainWindow window;
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("scanButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("stopScanButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("connectButton")));
    QVERIFY(window.findChild<QPushButton *>(QStringLiteral("disconnectButton")));
    QVERIFY(window.findChild<QCheckBox *>(QStringLiteral("demoModeCheckBox")));
    QVERIFY(window.findChild<QListWidget *>(QStringLiteral("deviceListWidget")));
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("statusLabel")));
    auto *table = window.findChild<QTableWidget *>(QStringLiteral("dataTableWidget"));
    QVERIFY(table);
    QCOMPARE(table->rowCount(), 15);
    QCOMPARE(table->columnCount(), 3);
    QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
}

void TestMainWindow::displaysSnapshot()
{
    MainWindow window;
    ImuData data;
    data.accelerationX = 1.23456;
    data.angularVelocityX = -45.6789;
    data.angleX = 12.345;
    data.magneticX = 0.12345;
    data.batteryPercent = 75.0;
    data.temperatureCelsius = 24.567;
    data.firmwareVersion = QStringLiteral("1.2.3");
    data.frameCount = 42;
    data.lastUpdated = QDateTime::currentDateTime();
    window.displayData(data);
    QTest::qWait(150);

    auto *table = window.findChild<QTableWidget *>(QStringLiteral("dataTableWidget"));
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("1.235"));
    QCOMPARE(table->item(3, 1)->text(), QStringLiteral("-45.679"));
    QCOMPARE(table->item(6, 1)->text(), QStringLiteral("12.35"));
    QCOMPARE(table->item(9, 1)->text(), QStringLiteral("0.123"));
    QCOMPARE(table->item(12, 1)->text(), QStringLiteral("75"));
    QCOMPARE(table->item(13, 1)->text(), QStringLiteral("24.57"));
    QCOMPARE(table->item(14, 1)->text(), QStringLiteral("1.2.3"));
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("g"));
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("frameCountLabel"))->text().contains(QStringLiteral("42")));
}

void TestMainWindow::togglesDemoMode()
{
    MainWindow window;
    auto *demo = window.findChild<QCheckBox *>(QStringLiteral("demoModeCheckBox"));
    auto *scan = window.findChild<QPushButton *>(QStringLiteral("scanButton"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
    demo->setChecked(true);
    QTest::qWait(250);
    QVERIFY(status->text().contains(QStringLiteral("演示模式")));
    QVERIFY(!scan->isEnabled());
    QVERIFY(window.findChild<QLabel *>(QStringLiteral("frameCountLabel"))->text() != QStringLiteral("帧计数：0"));
    demo->setChecked(false);
    QVERIFY(scan->isEnabled());
}

void TestMainWindow::handlesConnectWithoutSelection()
{
    MainWindow window;
    auto *button = window.findChild<QPushButton *>(QStringLiteral("connectButton"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
    QVERIFY(!button->isEnabled());
    button->setEnabled(true);
    button->click();
    QVERIFY(status->text().contains(QStringLiteral("选择设备")));
}

QTEST_MAIN(TestMainWindow)
#include "test_main_window.moc"
