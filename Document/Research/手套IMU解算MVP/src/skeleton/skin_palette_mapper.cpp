#include "skeleton/skin_palette_mapper.h"

#include <QHash>
#include <QSet>

namespace handstudio {

PaletteMapResult buildSkinPaletteMap(const RiggedModel &model,
                                     const QVector<HandBoneFrame> &skeletonBones)
{
    PaletteMapResult result;
    QHash<QString, int> skeletonByName;
    for (int index = 0; index < skeletonBones.size(); ++index) {
        const QString name = skeletonBones[index].boneName;
        if (name.isEmpty()) {
            result.errors.append(QStringLiteral("skeleton bone name is empty"));
        } else if (skeletonByName.contains(name)) {
            result.errors.append(QStringLiteral("duplicate skeleton bone: %1").arg(name));
        } else {
            skeletonByName.insert(name, index);
        }
    }
    QSet<QString> paletteNames;
    result.paletteToSkeleton.resize(model.bones.size());
    for (int paletteIndex = 0; paletteIndex < model.bones.size(); ++paletteIndex) {
        const QString name = model.bones[paletteIndex].name;
        if (name.isEmpty() || paletteNames.contains(name)) {
            result.errors.append(QStringLiteral("invalid palette bone: %1").arg(name));
            continue;
        }
        paletteNames.insert(name);
        if (!skeletonByName.contains(name)) {
            result.errors.append(QStringLiteral("palette bone missing from skeleton: %1").arg(name));
            continue;
        }
        result.paletteToSkeleton[paletteIndex] = skeletonByName.value(name);
    }
    if (model.bones.size() != skeletonBones.size()) {
        result.errors.append(QStringLiteral("palette and skeleton counts differ"));
    }
    return result;
}

bool applySkinPalette(const RiggedModel &model, HandSkeletonFrame &frame, QStringList *errors)
{
    const PaletteMapResult map = buildSkinPaletteMap(model, frame.bones);
    if (!map.valid()) {
        if (errors) {
            *errors = map.errors;
        }
        return false;
    }
    for (int paletteIndex = 0; paletteIndex < model.bones.size(); ++paletteIndex) {
        const int skeletonIndex = map.paletteToSkeleton[paletteIndex];
        frame.bones[skeletonIndex].skinMatrix = frame.bones[skeletonIndex].globalMatrix
                                                * model.bones[paletteIndex].inverseBind;
    }
    return true;
}

}
