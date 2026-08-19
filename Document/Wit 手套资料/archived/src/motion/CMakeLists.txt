cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(HandDemoMotion LANGUAGES CXX)
    enable_testing()
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Test)
endif()

add_library(handdemo_motion STATIC
    motion_types.h
    rig_config.h
    rig_config.cpp
    pose_solver.h
    pose_solver.cpp
    imu_pose.h
    imu_pose.cpp
    single_imu_finger_controller.h
    single_imu_finger_controller.cpp
)

target_compile_features(handdemo_motion PUBLIC cxx_std_17)
target_include_directories(handdemo_motion PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(handdemo_motion PUBLIC Qt6::Core Qt6::Gui)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../tests/motion" "${CMAKE_CURRENT_BINARY_DIR}/tests")
endif()
