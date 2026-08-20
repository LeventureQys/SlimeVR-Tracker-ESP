#include "model/model_importer.h"
#include "model/standard_joints.h"
#include "skeleton/skin_palette_mapper.h"

#include <QtTest>

using namespace handstudio;

class TestSkeletonPalette : public QObject {
    Q_OBJECT
private slots:
    void mapsAllTwentyFiveNames();
};

void TestSkeletonPalette::mapsAllTwentyFiveNames()
{
    ModelImporter importer;
    auto result = importer.load(QStringLiteral(HANDSTUDIO_TEST_GLB_PATH));
    QVERIFY(result.hasValue());
    QVector<HandBoneFrame> bones;
    for (const QString &name : standardJointNames()) { HandBoneFrame bone; bone.boneName=name; bones.append(bone); }
    const auto map = buildSkinPaletteMap(*result.value, bones);
    QVERIFY2(map.valid(), qPrintable(map.errors.join("\n")));
    QCOMPARE(map.paletteToSkeleton.size(), 25);
    QCOMPARE(result.value->bones[24].name, QStringLiteral("wrist"));
}

QTEST_APPLESS_MAIN(TestSkeletonPalette)
#include "test_skeleton_palette.moc"
