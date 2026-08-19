cmake_minimum_required(VERSION 3.24)

project(HandRigDemo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Bluetooth OpenGL OpenGLWidgets Test)

include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_BINARY_DIR}/_deps")
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    assimp
    URL https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(assimp)

add_library(hand_rig_import STATIC
    src/core/model_data.h
    src/import/model_importer.cpp
    src/import/model_importer.h
)
target_include_directories(hand_rig_import PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hand_rig_import PUBLIC Qt6::Core Qt6::Gui PRIVATE assimp::assimp)
if(MSVC)
    target_compile_options(hand_rig_import PRIVATE /utf-8)
endif()

add_subdirectory(src/motion)
add_subdirectory(src/imu)
add_subdirectory(src/render)

add_executable(hand_rig_demo
    src/app/main.cpp
    src/ui/main_window.cpp
    src/ui/main_window.h
)
target_include_directories(hand_rig_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(hand_rig_demo PRIVATE
    HAND_DEMO_DEFAULT_MODEL="${CMAKE_CURRENT_SOURCE_DIR}/models/3D带骨架手模.fbx"
    HAND_DEMO_DEFAULT_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/config/hand_rig.json"
)
target_link_libraries(hand_rig_demo PRIVATE
    hand_rig_import
    handdemo_motion
    handdemo_imu
    handdemo_render
    Qt6::Widgets
)
if(MSVC)
    target_compile_options(hand_rig_demo PRIVATE /utf-8)
endif()

include(CTest)
if(BUILD_TESTING)
    configure_file(tests/test_paths.h.in generated/test_paths.h @ONLY)
    add_executable(test_model_importer tests/test_model_importer.cpp)
    target_include_directories(test_model_importer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_link_libraries(test_model_importer PRIVATE hand_rig_import Qt6::Test)
    if(MSVC)
        target_compile_options(test_model_importer PRIVATE /utf-8)
    endif()
    add_test(NAME model_importer COMMAND test_model_importer)
    add_subdirectory(tests/motion)
    add_subdirectory(tests/imu)
endif()
