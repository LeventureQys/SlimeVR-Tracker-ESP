#include "import/model_importer.h"
#include "motion/imu_pose.h"
#include "motion/rig_config.h"
#include "ui/main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QSurfaceFormat>
#include <QTimer>
#include <QDebug>

namespace {

handdemo::motion::SkeletonBinding toSkeletonBinding(const handrig::RiggedModel &model)
{
    handdemo::motion::SkeletonBinding binding;
    binding.globalInverse = model.globalInverse;
    binding.bones.reserve(model.bones.size());
    for (const handrig::BoneData &bone : model.bones) {
        binding.bones.push_back({bone.name, bone.parentIndex, bone.bindLocal, bone.inverseBind});
    }
    return binding;
}

QString configErrors(const QVector<handdemo::motion::RigConfigError> &errors)
{
    QStringList details;
    for (const auto &error : errors) {
        details.push_back(QStringLiteral("[%1] %2: %3").arg(error.code, error.fieldPath, error.message));
    }
    return details.join('\n');
}

}

int main(int argc, char *argv[])
{
    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("hand_rig_demo"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("可活动骨架手模调试 Demo"));
    parser.addHelpOption();
    QCommandLineOption modelOption(QStringLiteral("model"), QStringLiteral("指定 FBX 模型路径"), QStringLiteral("path"));
    QCommandLineOption smokeOption(QStringLiteral("smoke-test"), QStringLiteral("首帧渲染后自动退出"));
    parser.addOption(modelOption);
    parser.addOption(smokeOption);
    parser.process(application);

    const QString modelPath = parser.isSet(modelOption) ? parser.value(modelOption)
                                                         : QString::fromUtf8(HAND_DEMO_DEFAULT_MODEL);
    handrig::ModelImporter importer;
    handrig::ModelLoadResult loadResult = importer.load(modelPath);
    std::shared_ptr<const handrig::RiggedModel> model;
    std::unique_ptr<handdemo::motion::PoseSolver> solver;
    std::unique_ptr<handdemo::motion::ImuPoseMapper> mapper;
    QString errorSummary;
    QString errorDetail;

    if (!loadResult.hasValue()) {
        errorSummary = loadResult.error->message;
        errorDetail = loadResult.error->detail;
    } else {
        model = std::make_shared<handrig::RiggedModel>(std::move(*loadResult.value));
        const handdemo::motion::SkeletonBinding binding = toSkeletonBinding(*model);
        QFile configFile(QString::fromUtf8(HAND_DEMO_DEFAULT_CONFIG));
        if (!configFile.open(QIODevice::ReadOnly)) {
            errorSummary = QStringLiteral("无法读取骨架配置");
            errorDetail = configFile.errorString();
            model.reset();
        } else {
            const auto configResult = handdemo::motion::RigConfigLoader::load(configFile.readAll(), binding.bones);
            if (!configResult) {
                errorSummary = QStringLiteral("骨架配置校验失败");
                errorDetail = configErrors(configResult.errors);
                model.reset();
            } else {
                solver = std::make_unique<handdemo::motion::PoseSolver>(binding, *configResult.config);
                mapper = std::make_unique<handdemo::motion::ImuPoseMapper>(*solver);
            }
        }
    }

    handdemo::ui::MainWindow window(model, std::move(solver), std::move(mapper), loadResult.warnings);
    if (!errorSummary.isEmpty()) {
        if (parser.isSet(smokeOption)) {
            qCritical().noquote() << "SMOKE_STARTUP_ERROR:" << errorSummary << errorDetail;
            QTimer::singleShot(0, &application, [&application] { application.exit(2); });
        } else {
            window.showStartupError(errorSummary, errorDetail);
        }
    } else if (parser.isSet(smokeOption)) {
        window.enableSmokeTest();
        QTimer::singleShot(15000, &application, [&application] { application.exit(3); });
    }
    window.show();
    return application.exec();
}
