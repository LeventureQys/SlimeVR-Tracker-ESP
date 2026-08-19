add_executable(test_wit_protocol test_wit_protocol.cpp)
target_link_libraries(test_wit_protocol PRIVATE handdemo_imu Qt6::Test)
add_test(NAME wit_protocol COMMAND test_wit_protocol)
