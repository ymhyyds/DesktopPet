cmake_minimum_required(VERSION 3.20)
project(DesktopPet VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# 基础设置
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    # release 模式下生成 Windows 子系统程序（不弹黑色控制台窗口）
    set(CMAKE_WIN32_EXECUTABLE TRUE)
endif()

# ---------------------------------------------------------------------------
# 第三方依赖
# ---------------------------------------------------------------------------
# Qt6：Widgets(窗口/UI) Network(DeepSeek API) Sql(SQLite) Gui(QMovie等) Multimedia(TTS 音频播放)
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Sql
    OpenGLWidgets
    Multimedia
)

# SQLite：Qt::Sql 默认通过 QSQLITE driver 内置支持，
# 不需要单独链接 sqlite3，但保留 find_package 以便未来切换到原生 sqlite3 C API。
find_package(SQLite3 QUIET)

# ---------------------------------------------------------------------------
# 源码文件分组（按模块组织，方便维护和后续新增模块）
# ---------------------------------------------------------------------------
set(CORE_SOURCES
    core/App.cpp
    core/App.h
    core/AutoStartManager.cpp
    core/AutoStartManager.h
    core/GlobalHotkeyManager.cpp
    core/GlobalHotkeyManager.h
    core/WeatherManager.cpp
    core/WeatherManager.h
    core/WindowManager.cpp
    core/WindowManager.h
)

set(PET_SOURCES
    pet/PetWindow.cpp
    pet/PetWindow.h
    pet/CharacterLoader.cpp
    pet/CharacterLoader.h
    pet/PetAnimationState.h
    pet/AffectionSystem.cpp
    pet/AffectionSystem.h
    pet/SpeechBubble.cpp
    pet/SpeechBubble.h
    pet/FloatingTextWidget.cpp
    pet/FloatingTextWidget.h
    pet/LipSyncConfig.cpp
    pet/LipSyncConfig.h
)

set(AI_SOURCES
    ai/DeepSeekClient.cpp
    ai/DeepSeekClient.h
    ai/PromptManager.cpp
    ai/PromptManager.h
    ai/EvidenceManager.cpp
    ai/EvidenceManager.h
    ai/EmotionAnalyzer.cpp
    ai/EmotionAnalyzer.h
    ai/TtsEngine.cpp
    ai/TtsEngine.h
    ai/VoiceVoxClient.cpp
    ai/VoiceVoxClient.h
)

set(MEMORY_SOURCES
    memory/MemoryDB.cpp
    memory/MemoryDB.h
)

set(INTERACTION_SOURCES
    interaction/InteractionManager.cpp
    interaction/InteractionManager.h
    interaction/InputProxyWindow.cpp
    interaction/InputProxyWindow.h
)

set(TRAY_SOURCES
    tray/TrayManager.cpp
    tray/TrayManager.h
)

set(CHAT_SOURCES
    chat/ChatWindow.cpp
    chat/ChatWindow.h
)

set(SETTINGS_SOURCES
    settings/SettingsWindow.cpp
    settings/SettingsWindow.h
    settings/ParameterWindow.cpp
    settings/ParameterWindow.h
    settings/CropDialog.cpp
    settings/CropDialog.h
    settings/ParameterPanel.cpp
    settings/ParameterPanel.h
)

set(ALL_SOURCES
    main.cpp
    ${CORE_SOURCES}
    ${PET_SOURCES}
    ${AI_SOURCES}
    ${MEMORY_SOURCES}
    ${INTERACTION_SOURCES}
    ${TRAY_SOURCES}
    ${CHAT_SOURCES}
    ${SETTINGS_SOURCES}
)

# ---------------------------------------------------------------------------
# 资源文件（预留：托盘图标等，后续往 resources.qrc 里加即可）
# ---------------------------------------------------------------------------
set(RESOURCE_FILES
    resources/resources.qrc
)

# ---------------------------------------------------------------------------
# 可执行文件
# ---------------------------------------------------------------------------
add_executable(DesktopPet
    ${ALL_SOURCES}
    ${RESOURCE_FILES}
)

# 让 #include "core/App.h" 这种写法在所有模块里都能找到头文件
target_include_directories(DesktopPet PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/pet
    ${CMAKE_CURRENT_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}/interaction
    ${CMAKE_CURRENT_SOURCE_DIR}/tray
    ${CMAKE_CURRENT_SOURCE_DIR}/chat
    ${CMAKE_CURRENT_SOURCE_DIR}/settings
)

target_link_libraries(DesktopPet PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
    Qt6::OpenGLWidgets
    Qt6::Multimedia
    dwmapi.lib
)

# ---------------------------------------------------------------------------
# 编译期宏：方便代码里区分调试/发布行为（比如是否打印网络请求日志）
# ---------------------------------------------------------------------------
target_compile_definitions(DesktopPet PRIVATE
    $<$<CONFIG:Debug>:PET_DEBUG_LOG=1>
    $<$<CONFIG:Release>:PET_DEBUG_LOG=0>
)

# ---------------------------------------------------------------------------
# 安装规则：把 characters/ 和 database/ 目录拷贝到可执行文件同级目录
# 这样角色包"热插拔"在打包后依然生效（不依赖源码目录）
# ---------------------------------------------------------------------------
install(TARGETS DesktopPet
    RUNTIME DESTINATION .
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/characters
    DESTINATION .
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/database
    DESTINATION .
)

# 开发期方便：构建完成后自动把 characters/ 和 database/ 同步到输出目录，
# 这样不用 install 也能直接在 build 目录里跑起来调试。
add_custom_command(TARGET DesktopPet POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/characters
        $<TARGET_FILE_DIR:DesktopPet>/characters
    COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_FILE_DIR:DesktopPet>/database
    COMMENT "Syncing characters/ and database/ to output directory"
)

# Windows 下用 windeployqt 拷依赖 dll 的便捷 target（手动调用：cmake --build . --target deploy）
if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
              "${Qt6_DIR}/../../bin"
              "$ENV{QT_QMAKE_DIR}/../windeployqt"
        PATHS "C:/Qt"
        PATH_SUFFIXES "bin"
    )
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_target(deploy
            COMMAND ${WINDEPLOYQT_EXECUTABLE} $<TARGET_FILE:DesktopPet>
            DEPENDS DesktopPet
            COMMENT "Running windeployqt to bundle Qt runtime dependencies"
        )
    else()
        message(STATUS "[DesktopPet] windeployqt 未找到，跳过 deploy target。"
                       "可通过 'cmake --build . --target deploy' 手动调用。")
    endif()
endif()

# ===========================================================================
# Live2D Cubism SDK 集成（探索性接入，先验证链接，渲染代码在后续步骤补充）
# ===========================================================================
# 注意：Live2D Cubism Core 只提供 MSVC 工具集 141/142/143 三个版本的预编译库，
# 即使本项目用 VS2026 生成，也必须强制用 -T v143 这个旧工具集编译整个项目，
# 否则链接时找不到匹配的 Core 库版本（这是 SDK 本身的限制，不是配置错误）。
set(LIVE2D_SDK_ROOT "E:/Project/Live2D_SDK" CACHE PATH "Live2D Cubism SDK for Native 根目录")

if(EXISTS "${LIVE2D_SDK_ROOT}")
    set(LIVE2D_CORE_PATH "${LIVE2D_SDK_ROOT}/Core")
    set(LIVE2D_FRAMEWORK_PATH "${LIVE2D_SDK_ROOT}/Framework")
    set(LIVE2D_GLEW_PATH "${LIVE2D_SDK_ROOT}/Samples/OpenGL/thirdParty/glew")

    # Core 库导入：固定用 143 工具集 + MD（动态运行时，跟 Qt 的 /MD 链接方式保持一致）
    add_library(Live2DCubismCore STATIC IMPORTED)
    set(LIVE2D_CORE_LIB_DIR "${LIVE2D_CORE_PATH}/lib/windows/x86_64/143")
    set_target_properties(Live2DCubismCore PROPERTIES
        IMPORTED_LOCATION_DEBUG          "${LIVE2D_CORE_LIB_DIR}/Live2DCubismCore_MDd.lib"
        IMPORTED_LOCATION_RELEASE        "${LIVE2D_CORE_LIB_DIR}/Live2DCubismCore_MD.lib"
        IMPORTED_LOCATION_MINSIZEREL     "${LIVE2D_CORE_LIB_DIR}/Live2DCubismCore_MD.lib"
        IMPORTED_LOCATION_RELWITHDEBINFO "${LIVE2D_CORE_LIB_DIR}/Live2DCubismCore_MD.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${LIVE2D_CORE_PATH}/include"
    )

    # GLEW：Cubism 渲染器依赖它解析 OpenGL 扩展函数，跟官方 Demo 保持一致用法
    add_subdirectory("${LIVE2D_GLEW_PATH}/build/cmake" "${CMAKE_CURRENT_BINARY_DIR}/glew")

    # glewinfo/visualinfo 是 GLEW 自带的诊断小工具（看显卡支持哪些 OpenGL 扩展），
    # 不是渲染必需品。它们的 main() 是控制台程序入口，
    # 但本项目顶层设置了 WIN32_EXECUTABLE（让主程序不弹控制台窗口），
    # 这个设置会被这两个子目录里的 target 一起继承，导致 WinMain/main 不匹配报链接错误。
    # 直接排除在编译范围外，反正用不上。
    foreach(_glew_tool IN ITEMS glewinfo visualinfo)
        if(TARGET ${_glew_tool})
            set_target_properties(${_glew_tool} PROPERTIES EXCLUDE_FROM_ALL TRUE)
        endif()
    endforeach()

    # Framework：纯源码形式提供，跟官方 Demo 一样用 add_subdirectory 整个引入
    set(FRAMEWORK_SOURCE OpenGL)
    add_subdirectory("${LIVE2D_FRAMEWORK_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/Framework")
    target_compile_definitions(Framework PUBLIC CSM_TARGET_WIN_GL)
    target_include_directories(Framework PUBLIC "${LIVE2D_GLEW_PATH}/include")
    target_link_libraries(Framework Live2DCubismCore glew_s)

    # 把 Framework 链接进主程序
    target_link_libraries(DesktopPet PRIVATE Framework)
    target_compile_definitions(DesktopPet PRIVATE LIVE2D_SDK_AVAILABLE=1)

    # ---------------------------------------------------------------------
    # 复用官方 Demo 里"平台无关"的几个类（模型加载/纹理管理/文件IO），
    # 这些类不依赖 GLFW，可以直接拿来在 Qt 里用，不需要重写。
    # 真正要我们自己写的只有 pet/Live2DView（用 Qt 的 QOpenGLWidget 代替 GLFW 主循环）。
    # ---------------------------------------------------------------------
    set(LIVE2D_SAMPLE_COMMON_PATH "${LIVE2D_SDK_ROOT}/Samples/Common")
    set(LIVE2D_SAMPLE_SRC_PATH "${LIVE2D_SDK_ROOT}/Samples/OpenGL/Demo/proj.win.cmake/src")
    set(LIVE2D_STB_PATH "${LIVE2D_SDK_ROOT}/Samples/OpenGL/thirdParty/stb")
    set(LIVE2D_GLFW_PATH "${LIVE2D_SDK_ROOT}/Samples/OpenGL/thirdParty/glfw")
    set(LIVE2D_RESOURCES_PATH "${LIVE2D_SDK_ROOT}/Samples/Resources")

    # 关闭 GLFW 自带的文档/测试/示例编译——这些子目录里也有控制台小工具，
    # 会跟之前 glewinfo/visualinfo 同样的原因（继承 WIN32_EXECUTABLE 设置）报链接错误，
    # 而且我们也用不上它们。
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

    # 真正链接 GLFW（不只是 include 头文件）：LAppPal::UpdateTime() 内部调用了 glfwGetTime()，
    # 需要真实的函数符号，不是 GLEW 那种只用类型声明的情况。
    add_subdirectory("${LIVE2D_GLFW_PATH}" "${CMAKE_CURRENT_BINARY_DIR}/glfw")

    add_library(Live2DSampleIntegration STATIC
        "${LIVE2D_SAMPLE_COMMON_PATH}/CubismSampleViewMatrix_Common.cpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/CubismSampleViewMatrix_Common.hpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppModel_Common.cpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppModel_Common.hpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppAllocator_Common.cpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppAllocator_Common.hpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppWavFileHandler_Common.cpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppWavFileHandler_Common.hpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppTextureManager_Common.cpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/LAppTextureManager_Common.hpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/MouseActionManager_Common.cpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/MouseActionManager_Common.hpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/TouchManager_Common.cpp"
        "${LIVE2D_SAMPLE_COMMON_PATH}/TouchManager_Common.hpp"
        "${LIVE2D_SAMPLE_SRC_PATH}/MouseActionManager.cpp"
        "${LIVE2D_SAMPLE_SRC_PATH}/MouseActionManager.hpp"
        # CubismUserModelExtend 已被本地 ModelController 替代
        "${LIVE2D_SAMPLE_SRC_PATH}/LAppPal.cpp"
        "${LIVE2D_SAMPLE_SRC_PATH}/LAppPal.hpp"
        "${LIVE2D_SAMPLE_SRC_PATH}/LAppTextureManager.cpp"
        "${LIVE2D_SAMPLE_SRC_PATH}/LAppTextureManager.hpp"
        "${LIVE2D_SAMPLE_SRC_PATH}/LAppDefine.cpp"
        "${LIVE2D_SAMPLE_SRC_PATH}/LAppDefine.hpp"
    )
    target_include_directories(Live2DSampleIntegration PUBLIC
        "${LIVE2D_SAMPLE_COMMON_PATH}"
        "${LIVE2D_SAMPLE_SRC_PATH}"
        "${LIVE2D_STB_PATH}"
        "${LIVE2D_GLFW_PATH}/include"
    )
    target_link_libraries(Live2DSampleIntegration PUBLIC Framework glfw)
    target_compile_definitions(Live2DSampleIntegration PUBLIC CSM_TARGET_WIN_GL)
    # 这几个文件是官方 SDK 自带的，按它们原本的编码读取，不强制走我们项目的警告级别
    target_compile_options(Live2DSampleIntegration PRIVATE /W0)

    target_link_libraries(DesktopPet PRIVATE Live2DSampleIntegration opengl32)
    target_include_directories(DesktopPet PRIVATE "${LIVE2D_SAMPLE_SRC_PATH}" "${LIVE2D_SAMPLE_COMMON_PATH}")
    target_compile_definitions(DesktopPet PRIVATE
        LIVE2D_RESOURCES_PATH="${LIVE2D_RESOURCES_PATH}/"
    )
    target_sources(DesktopPet PRIVATE
        pet/EmotionParameterResolver.h
        pet/EmotionParameterResolver.cpp
        pet/LipSyncEnvelope.h
        pet/LipSyncEnvelope.cpp
        pet/Live2DView.h
        pet/Live2DView.cpp
        pet/ModelController.hpp
        pet/ModelController.cpp
    )

    message(STATUS "[DesktopPet] Live2D Cubism SDK 已接入：${LIVE2D_SDK_ROOT}")

    # =====================================================================
    # 复制 Cubism 着色器文件到输出目录
    #
    # CubismShader_OpenGLES2 在 GenerateShaders() 中从文件加载着色器源码，
    # 需要运行时在 "FrameworkShaders/" 目录下找到对应的 .vert/.frag 文件。
    # 官方 Demo (proj.win.cmake/CMakeLists.txt:140) 也是同样做法。
    # 不复制这些文件会导致所有着色器编译失败 → 模型渲染不可见 + GL_INVALID_OPERATION。
    # =====================================================================
    set(LIVE2D_FRAMEWORK_SHADER_PATH "${LIVE2D_FRAMEWORK_PATH}/src/Rendering/OpenGL/Shaders/Standard")
    add_custom_command(TARGET DesktopPet POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${LIVE2D_FRAMEWORK_SHADER_PATH}"
            "$<TARGET_FILE_DIR:DesktopPet>/FrameworkShaders"
        COMMENT "Copying Cubism Framework shaders to output directory"
    )
else()
    message(STATUS "[DesktopPet] 未找到 Live2D SDK（${LIVE2D_SDK_ROOT}），跳过 Live2D 集成，仅使用 gif 动画方案")
endif()

# ---------------------------------------------------------------------------
# 可选：TTS 单元测试（不编入正式 DesktopPet.exe）
# ---------------------------------------------------------------------------
option(BUILD_TTS_TESTS "Build TTS unit tests with mock VoiceVox server" OFF)
if(BUILD_TTS_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
