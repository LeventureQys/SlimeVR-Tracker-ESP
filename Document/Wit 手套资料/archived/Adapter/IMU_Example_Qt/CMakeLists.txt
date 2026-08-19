cmake_minimum_required(VERSION 3.20)
project(IMUExampleQt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Bluetooth Test)

add_library(wit_protocol STATIC
    src/imu_data.h
    src/wit_protocol_parser.h
    src/wit_protocol_parser.cpp
)
target_include_directories(wit_protocol PUBLIC src)
target_link_libraries(wit_protocol PUBLIC Qt6::Core)

add_library(imu_runtime STATIC
    src/wit_ble_manager.h
    src/wit_ble_manager.cpp
    src/demo_data_source.h
    src/demo_data_source.cpp
    src/main_window.h
    src/main_window.cpp
)
target_include_directories(imu_runtime PUBLIC src)
target_link_libraries(imu_runtime PUBLIC wit_protocol Qt6::Core Qt6::Widgets Qt6::Bluetooth)

add_executable(IMUExampleQt src/main.cpp)
target_link_libraries(IMUExampleQt PRIVATE imu_runtime)
if(WIN32)
    set_target_properties(IMUExampleQt PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_wit_protocol tests/test_wit_protocol.cpp)
    target_link_libraries(test_wit_protocol PRIVATE wit_protocol Qt6::Test)
    add_test(NAME wit_protocol COMMAND test_wit_protocol)

    add_executable(test_main_window tests/test_main_window.cpp)
    target_link_libraries(test_main_window PRIVATE imu_runtime Qt6::Test)
    add_test(NAME main_window COMMAND test_main_window)
    set_tests_properties(main_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(ble_hardware_diagnostic tests/ble_hardware_diagnostic.cpp)
    target_link_libraries(ble_hardware_diagnostic PRIVATE imu_runtime)
endif()
