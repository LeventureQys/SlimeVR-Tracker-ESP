# ===========================================================================
# SubStage 5：GLB 导入与 OpenGL 渲染（自包含，由根 CMakeLists 末尾 include）。
# 依赖根已定义的 moc_header / moc_test_source / add_handstudio_test 助手与
# MOC_DEFS / MOC_INCLUDE_DIRS / MOC_OUT_DIR 变量。
# ===========================================================================

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
list(APPEND MOC_DEFS -DQT_WIDGETS_LIB -DQT_OPENGL_LIB)

# ---------------------------------------------------------------------------
# Assimp（可选，5.4.3）。SubStage 5 的 ModelImporter 使用自包含 glTF2/GLB 解析器，
# 不依赖 Assimp；此选项保留给后续 Assimp 验证/导入路径（联网可用时）。
# 开发会话沙箱阻断 TLS 出网且本地 build/_deps/assimp-* 缓存为空，故默认 OFF。
# ---------------------------------------------------------------------------
option(HAND_SKELETON_FETCH_ASSIMP "Fetch and build Assimp 5.4.3 from source" OFF)
if(HAND_SKELETON_FETCH_ASSIMP)
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
endif()

# ---------------------------------------------------------------------------
# 模型库：RiggedModel 数据契约 + 自包含 GLB 导入器。
# ---------------------------------------------------------------------------
add_library(handstudio_model STATIC
    src/model/model_data.h
    src/model/standard_joints.h
    src/model/model_importer.h
    src/model/model_importer.cpp)
target_include_directories(handstudio_model PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(handstudio_model PUBLIC Qt6::Core Qt6::Gui)

# ---------------------------------------------------------------------------
# 渲染库：OpenGL 3.3 蒙皮 + 骨架覆盖层 + 轨道相机（含手动 moc）。
# ---------------------------------------------------------------------------
moc_header(src/render/hand_render_widget.h MOC_HAND_RENDER_WIDGET)
add_library(handstudio_render STATIC
    src/render/hand_render_widget.h
    src/render/hand_render_widget.cpp
    ${MOC_HAND_RENDER_WIDGET})
target_include_directories(handstudio_render PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src ${MOC_OUT_DIR})
target_link_libraries(handstudio_render PUBLIC
    handstudio_model
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)

# ---------------------------------------------------------------------------
# 测试（纳入 CTest）。
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
    add_handstudio_test(test_glb_model)
    target_link_libraries(test_glb_model PRIVATE handstudio_model)
    target_compile_definitions(test_glb_model PRIVATE
        HANDSTUDIO_TEST_GLB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/generic-hand-left.glb")

    add_handstudio_test(test_render_offscreen)
    target_link_libraries(test_render_offscreen PRIVATE
        handstudio_model handstudio_render
        Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
    target_compile_definitions(test_render_offscreen PRIVATE
        HANDSTUDIO_TEST_GLB_PATH="${CMAKE_CURRENT_SOURCE_DIR}/assets/generic-hand-left.glb")
    # 无窗口 OpenGL：使用 windows 平台。Qt 6.8 的 offscreen 平台插件不支持
    # createPlatformOpenGLContext（无法创建 GL 上下文），故 GL 测试用 windows 平台；
    # QT_OPENGL=desktop 使用本机 GPU/驱动提供的桌面 GL（本机若为软件桌面 GL 会低于 3.3）。
    set_tests_properties(test_render_offscreen PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=windows;QT_OPENGL=desktop")
endif()
