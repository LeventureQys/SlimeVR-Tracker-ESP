# SubStage 4: hand observation and virtual kinematic skeleton.
add_library(handstudio_hand STATIC
    src/hand/orientation_decomposition.h src/hand/orientation_decomposition.cpp
    src/hand/mount_calibration.h src/hand/mount_calibration.cpp
    src/hand/hand_observation_solver.h src/hand/hand_observation_solver.cpp)
target_include_directories(handstudio_hand PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(handstudio_hand PUBLIC handstudio_core Qt6::Core Qt6::Gui)

add_library(handstudio_skeleton STATIC
    src/skeleton/kinematic_skeleton.h src/skeleton/kinematic_skeleton.cpp
    src/skeleton/skin_palette_mapper.h src/skeleton/skin_palette_mapper.cpp)
target_include_directories(handstudio_skeleton PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(handstudio_skeleton PUBLIC handstudio_core handstudio_model Qt6::Core Qt6::Gui)

if(BUILD_TESTING)
    add_handstudio_test(test_hand_observation)
    target_link_libraries(test_hand_observation PRIVATE handstudio_hand)

    foreach(test_name test_hand_rig_config test_skeleton_solver test_skeleton_palette)
        add_handstudio_test(${test_name})
        target_link_libraries(${test_name} PRIVATE handstudio_skeleton handstudio_model)
        target_compile_definitions(${test_name} PRIVATE
            HANDSTUDIO_TEST_GLB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/generic-hand-left.glb"
            HANDSTUDIO_TEST_RIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/hand_rig_generic_left.json")
    endforeach()
endif()
