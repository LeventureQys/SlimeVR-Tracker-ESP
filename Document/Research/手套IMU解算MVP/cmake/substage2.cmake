# ---------------------------------------------------------------------------
# SubStage 2: input, protocol, recording, replay.
# Included by the root CMakeLists after base targets + helpers are defined.
# This file is self-contained; it must not be edited by other SubStages.
#
# NOTE: to avoid cross-SubStage build coupling during parallel development, the
# SubStage 2 sources live in their own static library (handstudio_io) instead of
# being added to the shared handstudio_core. handstudio_io depends only on the
# header-only core types under src/core/. Final integration into the product
# executable is done in SubStage 6.
# ---------------------------------------------------------------------------

find_package(Qt6 REQUIRED COMPONENTS SerialPort)

# SerialPort Q_OBJECT headers need the component define for moc.
list(APPEND MOC_DEFS -DQT_SERIALPORT_LIB)

# Unique-name moc for handstudio headers. The root moc_header() derives its
# output name from NAME_WE, which would collide with the legacy adaptation
# headers src/frame_stream_parser.h / src/sequence_grouper.h. Use a "hs_" prefix.
function(ss2_moc_header header_rel out_var)
    get_filename_component(name ${header_rel} NAME_WE)
    set(output ${MOC_OUT_DIR}/moc_hs_${name}.cpp)
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

ss2_moc_header("src/protocol/frame_stream_parser.h" MOC_HS_PARSER)
ss2_moc_header("src/protocol/sequence_grouper.h" MOC_HS_GROUP)
ss2_moc_header("src/input/idata_source.h" MOC_IDATA)
ss2_moc_header("src/input/serial_data_source.h" MOC_SERIAL)
ss2_moc_header("src/input/replay_data_source.h" MOC_REPLAY)
ss2_moc_header("src/recording/session_recorder.h" MOC_RECORDER)
ss2_moc_header("src/recording/replay_controller.h" MOC_REPLAY_CTRL)

add_library(handstudio_io STATIC
    src/protocol/protocol_constants.h
    src/protocol/crc16.h src/protocol/crc16.cpp
    src/protocol/protocol_statistics.h
    src/protocol/frame_stream_parser.h src/protocol/frame_stream_parser.cpp
    src/protocol/sequence_grouper.h src/protocol/sequence_grouper.cpp
    src/input/idata_source.h
    src/input/serial_data_source.h src/input/serial_data_source.cpp
    src/input/replay_data_source.h src/input/replay_data_source.cpp
    src/recording/bounded_write_queue.h
    src/recording/recording_schema.h src/recording/recording_schema.cpp
    src/recording/session_recorder.h src/recording/session_recorder.cpp
    src/recording/replay_controller.h src/recording/replay_controller.cpp
    ${MOC_HS_PARSER} ${MOC_HS_GROUP} ${MOC_IDATA} ${MOC_SERIAL} ${MOC_REPLAY}
    ${MOC_RECORDER} ${MOC_REPLAY_CTRL})
target_include_directories(handstudio_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(handstudio_io PUBLIC Qt6::Core Qt6::Gui Qt6::SerialPort)

# The legacy src/crc16.cpp, src/frame_stream_parser.cpp, src/sequence_grouper.cpp
# now delegate to handstudio::* (single implementation), so six_imu_core needs it.
target_link_libraries(six_imu_core PUBLIC handstudio_io)

# Legacy protocol tests compile the adapters above, which delegate to handstudio_io.
# These test targets only exist when BUILD_TESTING is enabled.
if(BUILD_TESTING)
    target_link_libraries(test_protocol PRIVATE handstudio_io)
    target_link_libraries(test_sequence_grouper PRIVATE handstudio_io)
endif()

if(BUILD_TESTING)
    add_handstudio_test(test_protocol_v2)
    target_link_libraries(test_protocol_v2 PRIVATE handstudio_io)
    target_include_directories(test_protocol_v2 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    add_handstudio_test(test_recording)
    target_link_libraries(test_recording PRIVATE handstudio_io)
    target_include_directories(test_recording PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

    add_handstudio_test(test_replay)
    target_link_libraries(test_replay PRIVATE handstudio_io)
    target_include_directories(test_replay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()

add_executable(hardware_baseline_capture tools/hardware_baseline_capture.cpp)
target_include_directories(hardware_baseline_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(hardware_baseline_capture PRIVATE handstudio_io Qt6::SerialPort)
