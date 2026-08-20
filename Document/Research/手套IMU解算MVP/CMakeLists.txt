cmake_minimum_required(VERSION 3.20)
project(HandSkeletonStudio VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Test)
include(CTest)

get_target_property(QT_MOC_EXECUTABLE Qt6::moc IMPORTED_LOCATION)
# legacy/ = 旧 SlimeVR MVP 参考实现（仅服务遗留测试，不进入产品链接图）。
set(MOC_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests
    ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src ${CMAKE_CURRENT_SOURCE_DIR}/legacy/tests)
foreach(component Core Gui Test)
    get_target_property(component_includes Qt6::${component} INTERFACE_INCLUDE_DIRECTORIES)
    if(component_includes)
        list(APPEND MOC_INCLUDE_DIRS ${component_includes})
    endif()
endforeach()
list(REMOVE_DUPLICATES MOC_INCLUDE_DIRS)
set(MOC_DEFS -DQT_CORE_LIB -DQT_GUI_LIB -DQT_TESTLIB_LIB
    -DUNICODE -DWIN32 -DWIN64 -D_WIN64 -D_UNICODE -D_ENABLE_EXTENDED_ALIGNED_STORAGE)
set(MOC_OUT_DIR ${CMAKE_BINARY_DIR}/generated_moc)
file(MAKE_DIRECTORY ${MOC_OUT_DIR})

function(moc_header header_rel out_var)
    get_filename_component(name ${header_rel} NAME_WE)
    set(output ${MOC_OUT_DIR}/moc_${name}.cpp)
    set(arguments ${QT_MOC_EXECUTABLE} ${MOC_DEFS})
    foreach(include_dir ${MOC_INCLUDE_DIRS})
        list(APPEND arguments -I${include_dir})
    endforeach()
    execute_process(COMMAND ${arguments} ${CMAKE_CURRENT_SOURCE_DIR}/${header_rel} -o ${output}
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "moc failed for ${header_rel}: ${error}")
    endif()
    set(${out_var} ${output} PARENT_SCOPE)
endfunction()

function(moc_test_source source_rel)
    get_filename_component(name ${source_rel} NAME_WE)
    set(arguments ${QT_MOC_EXECUTABLE} ${MOC_DEFS})
    foreach(include_dir ${MOC_INCLUDE_DIRS})
        list(APPEND arguments -I${include_dir})
    endforeach()
    execute_process(COMMAND ${arguments} ${CMAKE_CURRENT_SOURCE_DIR}/${source_rel}
        -o ${MOC_OUT_DIR}/${name}.moc RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "moc failed for ${source_rel}: ${error}")
    endif()
endfunction()

foreach(pair
        "legacy/src/frame_stream_parser.h|MOC_FRAME"
        "legacy/src/sequence_grouper.h|MOC_GROUP"
        "legacy/src/six_imu_solver.h|MOC_SOLVER"
        "legacy/src/slimevr_udp_client.h|MOC_CLIENT"
        "legacy/src/slimevr_pose_sender.h|MOC_POSE_SENDER")
    string(REPLACE "|" ";" parts "${pair}")
    list(GET parts 0 header)
    list(GET parts 1 variable)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${header}")
        moc_header(${header} generated_moc)
        set(${variable} ${generated_moc})
    endif()
endforeach()

add_library(handstudio_core STATIC
    src/core/sensor_id.h
    src/core/imu_frames.h
    src/core/calibrated_types.h
    src/core/fusion_types.h
    src/core/hand_observation_types.h
    src/core/hand_skeleton_frame.h
    src/core/diagnostic.h
    src/core/schema_version.h
    src/core/metatype_registration.h src/core/metatype_registration.cpp
    src/config/runtime_config.h src/config/runtime_config.cpp)
target_include_directories(handstudio_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src)
target_link_libraries(handstudio_core PUBLIC Qt6::Core Qt6::Gui)

add_executable(HandSkeletonStudio src/app/main.cpp)
target_link_libraries(HandSkeletonStudio PRIVATE handstudio_core)

# ---------------------------------------------------------------------------
# 旧 SlimeVR MVP 遗留目标（参考实现；不进入 HandSkeletonStudio 链接图）。
# ---------------------------------------------------------------------------
set(LEGACY_CORE_SOURCES
    legacy/src/imu_types.h legacy/src/protocol_constants.h
    legacy/src/crc16.h legacy/src/crc16.cpp
    legacy/src/frame_stream_parser.h legacy/src/frame_stream_parser.cpp
    legacy/src/sequence_grouper.h legacy/src/sequence_grouper.cpp
    legacy/src/solver_settings.h legacy/src/solver_settings.cpp
    legacy/src/settings_store.h legacy/src/settings_store.cpp
    legacy/src/madgwick_filter.h legacy/src/madgwick_filter.cpp
    legacy/src/six_imu_solver.h legacy/src/six_imu_solver.cpp)
add_library(six_imu_core STATIC ${LEGACY_CORE_SOURCES} ${MOC_FRAME} ${MOC_GROUP} ${MOC_SOLVER})
target_include_directories(six_imu_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src ${MOC_OUT_DIR})
target_link_libraries(six_imu_core PUBLIC Qt6::Core Qt6::Gui)

set(NETWORK_SOURCES
    legacy/src/slimevr_protocol.h legacy/src/slimevr_protocol.cpp
    legacy/src/slimevr_settings.h legacy/src/slimevr_settings.cpp
    legacy/src/slimevr_settings_store.h legacy/src/slimevr_settings_store.cpp
    legacy/src/slimevr_udp_client.h legacy/src/slimevr_udp_client.cpp
    legacy/src/slimevr_sensor_mapping.h legacy/src/slimevr_sensor_mapping.cpp
    legacy/src/slimevr_coordinate_transform.h legacy/src/slimevr_coordinate_transform.cpp
    legacy/src/slimevr_pose_adapter.h legacy/src/slimevr_pose_adapter.cpp
    legacy/src/slimevr_pose_sender.h legacy/src/slimevr_pose_sender.cpp)
set(NETWORK_SOURCES_EXISTING)
foreach(source ${NETWORK_SOURCES})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
        list(APPEND NETWORK_SOURCES_EXISTING ${source})
    endif()
endforeach()
if(NETWORK_SOURCES_EXISTING)
    find_package(Qt6 REQUIRED COMPONENTS Network)
    add_library(six_imu_network STATIC EXCLUDE_FROM_ALL
        ${NETWORK_SOURCES_EXISTING} ${MOC_CLIENT} ${MOC_POSE_SENDER})
    target_include_directories(six_imu_network PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src ${MOC_OUT_DIR})
    target_link_libraries(six_imu_network PUBLIC Qt6::Core Qt6::Gui Qt6::Network)
endif()

function(add_handstudio_test name)
    moc_test_source(tests/${name}.cpp)
    add_executable(${name} tests/${name}.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MOC_OUT_DIR})
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

if(BUILD_TESTING)
    add_handstudio_test(test_core_contracts)
    target_link_libraries(test_core_contracts PRIVATE handstudio_core)

    add_handstudio_test(test_runtime_config)
    target_link_libraries(test_runtime_config PRIVATE handstudio_core)

    # 遗留测试：验证旧 SlimeVR MVP 参考实现仍可编译运行（编译 legacy/ 下源码）。
    moc_test_source(legacy/tests/test_protocol.cpp)
    add_executable(test_protocol legacy/tests/test_protocol.cpp
        legacy/src/crc16.cpp legacy/src/crc16.h legacy/src/frame_stream_parser.cpp
        legacy/src/frame_stream_parser.h legacy/src/imu_types.h legacy/src/protocol_constants.h
        ${MOC_FRAME})
    target_include_directories(test_protocol PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src ${MOC_OUT_DIR})
    target_link_libraries(test_protocol PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME test_protocol COMMAND test_protocol)
    set_tests_properties(test_protocol PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    moc_test_source(legacy/tests/test_sequence_grouper.cpp)
    add_executable(test_sequence_grouper legacy/tests/test_sequence_grouper.cpp
        legacy/src/sequence_grouper.cpp legacy/src/sequence_grouper.h legacy/src/imu_types.h
        legacy/src/protocol_constants.h ${MOC_GROUP})
    target_include_directories(test_sequence_grouper PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src ${MOC_OUT_DIR})
    target_link_libraries(test_sequence_grouper PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME test_sequence_grouper COMMAND test_sequence_grouper)
    set_tests_properties(test_sequence_grouper PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    moc_test_source(legacy/tests/test_madgwick_filter.cpp)
    add_executable(test_madgwick_filter legacy/tests/test_madgwick_filter.cpp
        legacy/src/madgwick_filter.cpp legacy/src/madgwick_filter.h
        legacy/src/six_imu_solver.cpp legacy/src/six_imu_solver.h
        legacy/src/solver_settings.cpp legacy/src/solver_settings.h
        legacy/src/settings_store.cpp legacy/src/settings_store.h
        legacy/src/imu_types.h legacy/src/protocol_constants.h ${MOC_SOLVER})
    target_include_directories(test_madgwick_filter PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src ${MOC_OUT_DIR})
    target_link_libraries(test_madgwick_filter PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME test_madgwick_filter COMMAND test_madgwick_filter)
    set_tests_properties(test_madgwick_filter PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    moc_test_source(legacy/tests/test_settings.cpp)
    add_executable(test_settings legacy/tests/test_settings.cpp
        legacy/src/solver_settings.cpp legacy/src/solver_settings.h
        legacy/src/settings_store.cpp legacy/src/settings_store.h)
    target_include_directories(test_settings PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/legacy/src ${MOC_OUT_DIR})
    target_link_libraries(test_settings PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    add_test(NAME test_settings COMMAND test_settings)
    set_tests_properties(test_settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# ---------------------------------------------------------------------------
# SubStage-local CMake includes (each file self-contained; added by MainAgent).
# SubStage agents must NOT edit this root file; they write cmake/substageN.cmake.
# Root moc/moc_test_source/add_handstudio_test helpers are visible here.
# ---------------------------------------------------------------------------
foreach(substage_file
        cmake/substage2.cmake
        cmake/substage3.cmake
        cmake/substage5.cmake
        cmake/substage4.cmake
        cmake/substage6.cmake)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${substage_file}")
        include("${CMAKE_CURRENT_SOURCE_DIR}/${substage_file}")
    endif()
endforeach()
