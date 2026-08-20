# SubStage 6: UI, worker-thread runtime and product integration.
find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets SerialPort)
list(APPEND MOC_DEFS -DQT_WIDGETS_LIB -DQT_OPENGL_LIB)

moc_header(src/app/demo_data_source.h MOC_DEMO_DATA_SOURCE)
moc_header(src/app/runtime_controller.h MOC_RUNTIME_CONTROLLER)
moc_header(src/ui/main_window.h MOC_MAIN_WINDOW_V2)
moc_test_source(src/app/runtime_controller.cpp)

add_library(handstudio_app STATIC
    src/app/demo_data_source.h src/app/demo_data_source.cpp
    src/app/runtime_controller.h src/app/runtime_controller.cpp
    src/ui/main_window.h src/ui/main_window.cpp
    ${MOC_DEMO_DATA_SOURCE} ${MOC_RUNTIME_CONTROLLER} ${MOC_MAIN_WINDOW_V2})
target_include_directories(handstudio_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${MOC_OUT_DIR})
target_link_libraries(handstudio_app PUBLIC
    handstudio_core handstudio_io handstudio_hand handstudio_skeleton
    handstudio_model handstudio_render
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::SerialPort Qt6::OpenGL Qt6::OpenGLWidgets)

# Product target is created by the root file before this include.
target_sources(HandSkeletonStudio PRIVATE src/app/main.cpp)
target_link_libraries(HandSkeletonStudio PRIVATE
    handstudio_core handstudio_io handstudio_model handstudio_render handstudio_app
    Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

add_custom_command(TARGET HandSkeletonStudio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:HandSkeletonStudio>/assets)

if(BUILD_TESTING)
    add_handstudio_test(test_app_integration)
    target_link_libraries(test_app_integration PRIVATE handstudio_app Qt6::Widgets)
    target_compile_definitions(test_app_integration PRIVATE
        HANDSTUDIO_TEST_GLB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/generic-hand-left.glb"
        HANDSTUDIO_TEST_RIG_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/hand_rig_generic_left.json"
        HANDSTUDIO_TEST_RUNTIME_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/default_runtime.json")
    set_tests_properties(test_app_integration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
