#include "metatype_registration.h"

#include "calibrated_types.h"
#include "diagnostic.h"
#include "fusion_types.h"
#include "hand_observation_types.h"
#include "hand_skeleton_frame.h"
#include "imu_frames.h"
#include "sensor_id.h"

#include <QMetaType>

namespace handstudio {

void registerCoreMetaTypes()
{
    qRegisterMetaType<SensorId>();
    qRegisterMetaType<RawImuFrame>();
    qRegisterMetaType<SixImuSampleGroup>();
    qRegisterMetaType<CalibratedImuSample>();
    qRegisterMetaType<FusionMode>();
    qRegisterMetaType<MagneticHealth>();
    qRegisterMetaType<CalibrationState>();
    qRegisterMetaType<FusedImuPose>();
    qRegisterMetaType<HandSide>();
    qRegisterMetaType<FingerObservation>();
    qRegisterMetaType<HandObservationFrame>();
    qRegisterMetaType<DiagnosticSeverity>();
    qRegisterMetaType<Diagnostic>();
    qRegisterMetaType<BoneSource>();
    qRegisterMetaType<HandBoneFrame>();
    qRegisterMetaType<HandSkeletonFrame>();
}

}
