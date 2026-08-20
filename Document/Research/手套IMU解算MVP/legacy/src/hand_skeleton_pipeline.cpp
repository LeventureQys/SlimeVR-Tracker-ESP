#include "hand_skeleton_pipeline.h"

namespace {

QVector3D matrixTranslation(const QMatrix4x4 &matrix)
{
    return matrix.column(3).toVector3D();
}

QQuaternion localRotationFromPose(const handdemo::motion::JointPose &pose)
{
    return QQuaternion::fromEulerAngles(pose.eulerDegrees).normalized();
}

} // namespace

namespace HandSkeleton {

Pipeline::Pipeline(handdemo::motion::SkeletonBinding binding, handdemo::motion::RigConfig config)
    : binding_(std::move(binding))
    , solver_(binding_, std::move(config))
    , mapper_(std::make_unique<handdemo::motion::ImuPoseMapper>(solver_))
{
}

HandSkeletonFrame Pipeline::process(const SixImuSnapshot &snapshot)
{
    const handdemo::motion::HandImuFrame input = SixImuSnapshotAdapter::adapt(snapshot);
    return buildFrame(snapshot, mapper_->update(input));
}

void Pipeline::reset()
{
    mapper_->reset();
}

const handdemo::motion::SkeletonBinding &Pipeline::binding() const
{
    return binding_;
}

const handdemo::motion::RigConfig &Pipeline::config() const
{
    return solver_.config();
}

HandSkeletonFrame Pipeline::buildFrame(
    const SixImuSnapshot &snapshot,
    const handdemo::motion::ImuMappingResult &mapping) const
{
    HandSkeletonFrame frame;
    frame.sequence = snapshot.sequence;
    frame.timestampNs = snapshot.updatedMonotonicNs;
    for (int fingerIndex = 0; fingerIndex < 5; ++fingerIndex) {
        frame.fingerValid[size_t(fingerIndex)]
            = snapshot.poses[size_t(fingerIndex + 1)].valid;
    }
    frame.frameApplied = mapping.frameApplied;
    frame.coupledApproximation = mapping.pose.coupledApproximation;
    frame.diagnostics.reserve(mapping.errors.size());
    for (const handdemo::motion::PoseInputError &error : mapping.errors) {
        frame.diagnostics.push_back({error.code, error.message, error.detail});
    }

    const int boneCount = binding_.bones.size();
    frame.bones.reserve(boneCount);
    for (int index = 0; index < boneCount; ++index) {
        const handdemo::motion::BoneBinding &binding = binding_.bones[index];
        HandBoneFrame bone;
        bone.boneName = binding.name;
        bone.parentIndex = binding.parentIndex;
        bone.bindTranslation = matrixTranslation(binding.bindLocal);
        if (index < mapping.pose.pose.localPoses.size()) {
            bone.localRotation = localRotationFromPose(mapping.pose.pose.localPoses[index]);
        }
        if (index < mapping.pose.globalMatrices.size()) {
            bone.globalMatrix = mapping.pose.globalMatrices[index];
            bone.localMatrix = binding.parentIndex >= 0
                ? mapping.pose.globalMatrices[binding.parentIndex].inverted()
                    * mapping.pose.globalMatrices[index]
                : mapping.pose.globalMatrices[index];
        }
        if (index < mapping.pose.skinMatrices.size()) {
            bone.skinMatrix = mapping.pose.skinMatrices[index];
        }
        if (index < mapping.pose.joints.size()) {
            bone.constrained = mapping.pose.joints[index].constrained;
        }
        bone.source = mapping.frameApplied ? HandBoneSource::Estimated : HandBoneSource::Held;
        bone.confidence = mapping.frameApplied ? 1.0F : 0.5F;
        frame.bones.push_back(bone);
    }
    return frame;
}

} // namespace HandSkeleton
