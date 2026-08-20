#pragma once

#include <core/model_data.h>
#include <rig_config.h>

#include <QString>
#include <QStringList>

#include <optional>

namespace HandSkeleton {

struct ModelSetupError {
    QString code;
    QString message;
    QString detail;
};

struct ModelSetup {
    handrig::RiggedModel model;
    handdemo::motion::SkeletonBinding binding;
    handdemo::motion::RigConfig config;
    QStringList warnings;
};

struct ModelSetupResult {
    std::optional<ModelSetup> value;
    std::optional<ModelSetupError> error;

    explicit operator bool() const { return value.has_value(); }
};

class ModelLoader final {
public:
    static ModelSetupResult load(const QString &modelPath, const QString &configPath);
    static handdemo::motion::SkeletonBinding bindingFromModel(const handrig::RiggedModel &model);
};

} // namespace HandSkeleton
