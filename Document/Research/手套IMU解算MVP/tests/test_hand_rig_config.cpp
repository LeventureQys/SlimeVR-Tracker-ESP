#include "model/model_importer.h"
#include "skeleton/kinematic_skeleton.h"

#include <QFile>
#include <QtTest>

using namespace handstudio;

class TestHandRigConfig : public QObject {
    Q_OBJECT
private slots:
    void loadsStrictConfiguration();
    void rejectsInvalidValues();
};

void TestHandRigConfig::loadsStrictConfiguration()
{
    ModelImporter importer;
    auto model = importer.load(QStringLiteral(HANDSTUDIO_TEST_GLB_PATH));
    QVERIFY(model.hasValue());
    QFile file(QStringLiteral(HANDSTUDIO_TEST_RIG_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto result = loadHandRigConfig(file.readAll(), *model.value);
    QVERIFY2(result.config.has_value(), qPrintable(result.errors.join("\n")));
    QCOMPARE(result.config->joints.size(), 25);
    QCOMPARE(result.config->rootName, QStringLiteral("wrist"));
}

void TestHandRigConfig::rejectsInvalidValues()
{
    RiggedModel model;
    auto nanResult = loadHandRigConfig("{\"schemaVersion\":1,\"skeletonId\":\"x\",\"rootName\":\"wrist\",\"joints\":[]}", model);
    QVERIFY(!nanResult.config.has_value());
    QVERIFY(!nanResult.errors.isEmpty());
}

QTEST_APPLESS_MAIN(TestHandRigConfig)
#include "test_hand_rig_config.moc"
