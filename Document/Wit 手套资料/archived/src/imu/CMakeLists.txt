add_library(handdemo_imu STATIC
    imu_data.h
    wit_protocol_parser.h
    wit_protocol_parser.cpp
    wit_ble_manager.h
    wit_ble_manager.cpp
)
target_include_directories(handdemo_imu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(handdemo_imu PUBLIC Qt6::Core Qt6::Bluetooth)
