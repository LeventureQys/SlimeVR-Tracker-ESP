cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(HandDemoRender LANGUAGES CXX)
    set(CMAKE_AUTOMOC ON)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
endif()

add_library(handdemo_render STATIC
    hand_render_widget.h
    hand_render_widget.cpp
)

target_compile_features(handdemo_render PUBLIC cxx_std_17)
target_include_directories(handdemo_render
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)
target_link_libraries(handdemo_render
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::OpenGL
        Qt6::OpenGLWidgets
)

if(MSVC)
    target_compile_options(handdemo_render PRIVATE /utf-8)
endif()
