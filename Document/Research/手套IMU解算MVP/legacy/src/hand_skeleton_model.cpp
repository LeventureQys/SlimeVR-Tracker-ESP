#include "hand_skeleton_model.h"

#include <import/model_importer.h>

#include <QFile>

namespace {

QString rigConfigErrors(const QVector<handdemo::motion::RigConfigError> &errors)
{
    QStringList details;
    details.reserve(errors.size());
    for (const handdemo::motion::RigConfigError &error : errors) {
        details.push_back(
            QStringLiteral("[%1] %2: %3").arg(error.code, error.fieldPath, error.message));
    }
    return details.join('\n');
}

} // namespace

namespace HandSkeleton {

ModelSetupResult ModelLoader::load(const QString &modelPath, const QString &configPath)
{
    handrig::ModelLoadResult modelResult = handrig::ModelImporter().load(modelPath);
    if (!modelResult.hasValue()) {
        return {
            std::nullopt,
            ModelSetupError{
                QStringLiteral("model_load_failed"),
                modelResult.error ? modelResult.error->message : QStringLiteral("模型加载失败"),
                modelResult.error ? modelResult.error->detail : modelPath,
            },
        };
    }

    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly)) {
        return {
            std::nullopt,
            ModelSetupError{
                QStringLiteral("config_open_failed"),
                QStringLiteral("无法读取骨架配置"),
                configFile.errorString(),
            },
        };
    }

    handrig::RiggedModel model = std::move(*modelResult.value);
    handdemo::motion::SkeletonBinding binding = bindingFromModel(model);
    const handdemo::motion::RigConfigLoadResult configResult
        = handdemo::motion::RigConfigLoader::load(configFile.readAll(), binding.bones);
    if (!configResult) {
        return {
            std::nullopt,
            ModelSetupError{
                QStringLiteral("config_invalid"),
                QStringLiteral("骨架配置校验失败"),
                rigConfigErrors(configResult.errors),
            },
        };
    }

    return {
        ModelSetup{
            std::move(model),
            std::move(binding),
            *configResult.config,
            modelResult.warnings,
        },
        std::nullopt,
    };
}

handdemo::motion::SkeletonBinding ModelLoader::bindingFromModel(const handrig::RiggedModel &model)
{
    handdemo::motion::SkeletonBinding binding;
    binding.globalInverse = model.globalInverse;
    binding.bones.reserve(model.bones.size());
    for (const handrig::BoneData &bone : model.bones) {
        binding.bones.push_back({bone.name, bone.parentIndex, bone.bindLocal, bone.inverseBind});
    }
    return binding;
}

} // namespace HandSkeleton
