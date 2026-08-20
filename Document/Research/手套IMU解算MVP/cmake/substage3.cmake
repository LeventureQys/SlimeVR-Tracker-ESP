# ---------------------------------------------------------------------------
# SubStage 3: calibration + fusion layer.
# Included by the root CMakeLists after base targets + helpers are defined.
# This file is self-contained; it must not be edited by other SubStages.
#
# The new fusion layer wraps the legacy pure-math Madgwick core
# (legacy/src/madgwick_filter.cpp, 旧 SlimeVR MVP 参考实现). To keep
# handstudio_core self-contained (and avoid a six_imu_core <-> handstudio_core
# link cycle introduced by SubStage 2), the legacy math core is compiled into
# handstudio_core here as well. The legacy six_imu_core target still compiles
# its own copy; no product executable links both libraries, and the linker
# resolves MadgwickFilter from a single object.
# ---------------------------------------------------------------------------

target_sources(handstudio_core PRIVATE
    src/calibration/axis_remap.h
    src/calibration/calibration_parameters.h src/calibration/calibration_parameters.cpp
    src/calibration/rest_detector.h src/calibration/rest_detector.cpp
    src/calibration/static_gyro_bias_estimator.h src/calibration/static_gyro_bias_estimator.cpp
    src/calibration/magnetic_calibration.h src/calibration/magnetic_calibration.cpp
    src/calibration/calibration_pipeline.h src/calibration/calibration_pipeline.cpp
    src/calibration/calibration_store.h src/calibration/calibration_store.cpp
    src/fusion/ifusion_filter.h
    src/fusion/quaternion_util.h
    src/fusion/madgwick_fusion_filter.h src/fusion/madgwick_fusion_filter.cpp
    src/fusion/vqf_fusion_filter.h src/fusion/vqf_fusion_filter.cpp
    src/fusion/magnetic_health_monitor.h src/fusion/magnetic_health_monitor.cpp
    src/fusion/pose_guard.h src/fusion/pose_guard.cpp
    src/fusion/fusion_bank.h src/fusion/fusion_bank.cpp
    src/fusion/vqf/vqf.h src/fusion/vqf/vqf.cpp
    legacy/src/madgwick_filter.h legacy/src/madgwick_filter.cpp)

if(BUILD_TESTING)
    add_handstudio_test(test_calibration)
    target_link_libraries(test_calibration PRIVATE handstudio_core)

    add_handstudio_test(test_fusion)
    target_link_libraries(test_fusion PRIVATE handstudio_core)
endif()
