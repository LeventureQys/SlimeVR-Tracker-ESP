#pragma once

#include "core/hand_skeleton_frame.h"
#include "model/model_data.h"

#include <QStringList>

namespace handstudio {

struct PaletteMapResult {
    QVector<int> paletteToSkeleton;
    QStringList errors;
    bool valid() const noexcept { return errors.isEmpty(); }
};

PaletteMapResult buildSkinPaletteMap(const RiggedModel &model,
                                     const QVector<HandBoneFrame> &skeletonBones);
bool applySkinPalette(const RiggedModel &model, HandSkeletonFrame &frame, QStringList *errors = nullptr);

}
