# ── TTS Unit Tests ──────────────────────────────────────────────────────────
# Standalone test executable (NOT part of DesktopPet.exe).
# Uses mock HTTP server (Python) to avoid real VoiceVox Engine requirement.
# Build with: cmake -DBUILD_TTS_TESTS=ON -B build && cmake --build build --target test_tts --config Release
# Run with:   ctest --test-dir build -C Release --output-on-failure

find_package(Qt6 REQUIRED COMPONENTS Test Core Network Multimedia Widgets)

# Source files from the main project under test
set(TESTED_SOURCES
    ${CMAKE_SOURCE_DIR}/ai/VoiceVoxClient.cpp
    ${CMAKE_SOURCE_DIR}/ai/TtsEngine.cpp
    ${CMAKE_SOURCE_DIR}/ai/PromptManager.cpp
    ${CMAKE_SOURCE_DIR}/core/WeatherManager.cpp
)

# Only test_main.cpp — it #includes the other .cpp files internally
set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WeatherManagerTest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/MockServer.h
)

add_executable(test_tts ${TEST_SOURCES} ${TESTED_SOURCES})

set_target_properties(test_tts PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_tts PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_tts PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Network
    Qt6::Multimedia
    Qt6::Gui
    Qt6::Widgets
)

# Copy mock server scripts to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/MockVoiceVoxServer.py
    ${CMAKE_CURRENT_BINARY_DIR}/MockVoiceVoxServer.py
    COPYONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_edge_tts.py
    ${CMAKE_CURRENT_BINARY_DIR}/mock_edge_tts.py
    COPYONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/mock_edge_tts.bat
    ${CMAKE_CURRENT_BINARY_DIR}/mock_edge_tts.bat
    COPYONLY
)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/MockWeatherServer.py
    ${CMAKE_CURRENT_BINARY_DIR}/MockWeatherServer.py
    COPYONLY
)

# Also copy mock scripts to the executable output directory so they're
# available when running test_tts.exe directly (not via ctest).
add_custom_command(TARGET test_tts POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/mock_edge_tts.py"
        "$<TARGET_FILE_DIR:test_tts>/mock_edge_tts.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/mock_edge_tts.bat"
        "$<TARGET_FILE_DIR:test_tts>/mock_edge_tts.bat"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MockVoiceVoxServer.py"
        "$<TARGET_FILE_DIR:test_tts>/MockVoiceVoxServer.py"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MockWeatherServer.py"
        "$<TARGET_FILE_DIR:test_tts>/MockWeatherServer.py"
)

# Deploy Qt6Multimedia runtime DLL so test_tts can load it
# without relying on PATH or manual copy.
if(WIN32)
    add_custom_command(TARGET test_tts POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Multimedia>"
            "$<TARGET_FILE_DIR:test_tts>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Widgets>"
            "$<TARGET_FILE_DIR:test_tts>"
        COMMENT "Deploying Qt6Multimedia and Qt6Widgets runtimes for test_tts"
        VERBATIM
    )
endif()

add_test(NAME test_tts
    COMMAND test_tts
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── Bedtime Reminder Unit Tests ───────────────────────────────────────────────
# Tests pure logic in BedtimeHelper.h (no mock server, no App/TTS dependencies).

add_executable(test_bedtime
    ${CMAKE_CURRENT_SOURCE_DIR}/BedtimeReminderTest.cpp
)

set_target_properties(test_bedtime PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_bedtime PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(test_bedtime PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
)

add_test(NAME test_bedtime
    COMMAND test_bedtime
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── Drag Anchor Unit Tests ─────────────────────────────────────────────────
# Pure-function tests for coordinate calculation (no mock server, no App deps).

add_executable(test_drag_anchor
    ${CMAKE_CURRENT_SOURCE_DIR}/DragAnchorTest.cpp
)

set_target_properties(test_drag_anchor PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_drag_anchor PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/interaction
)

target_link_libraries(test_drag_anchor PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
)

add_test(NAME test_drag_anchor
    COMMAND test_drag_anchor
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── Drawable Bounds Unit Tests ────────────────────────────────────────────
# Pure-function tests for drawable AABB filtering (NaN/Inf/degenerate).
# No Cubism SDK, no OpenGL, no mock server dependencies.

add_executable(test_drawable_bounds
    ${CMAKE_CURRENT_SOURCE_DIR}/DrawableBoundsTest.cpp
)

set_target_properties(test_drawable_bounds PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_drawable_bounds PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/interaction
)

target_link_libraries(test_drawable_bounds PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
)

add_test(NAME test_drawable_bounds
    COMMAND test_drawable_bounds
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── Input Region Grid Unit Tests ──────────────────────────────────────────
# Pure-logic tests for InputRegionBuilder helpers: pointInTriangle, mergeCellRects,
# isTrustedInputRegion, isSuspiciousInputRegion, and publish decision simulation.
# No Qt Widgets, no Live2D SDK, no OpenGL, no mock server dependencies.

add_executable(test_input_region_grid
    ${CMAKE_CURRENT_SOURCE_DIR}/InputRegionGridTest.cpp
)

set_target_properties(test_input_region_grid PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_input_region_grid PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/interaction
)

target_link_libraries(test_input_region_grid PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
)

add_test(NAME test_input_region_grid
    COMMAND test_input_region_grid
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── Expression Selector Unit Tests ──────────────────────────────────────────
# Pure-logic tests for expression name management and random selection with
# consecutive-repeat avoidance. No Cubism SDK, no OpenGL, no window deps.

add_executable(test_expression
    ${CMAKE_CURRENT_SOURCE_DIR}/ExpressionManagerTest.cpp
)

set_target_properties(test_expression PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_expression PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_expression PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Gui
)

add_test(NAME test_expression
    COMMAND test_expression
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── LipSync Envelope Unit Tests ──────────────────────────────────────────
# Pure logic tests for the lip-sync envelope algorithm.
# No Cubism SDK, no OpenGL, no Qt Multimedia, no mock server.

add_executable(test_lipsync
    ${CMAKE_CURRENT_SOURCE_DIR}/LipSyncEnvelopeTest.cpp
    ${CMAKE_SOURCE_DIR}/pet/LipSyncEnvelope.cpp
)

set_target_properties(test_lipsync PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_lipsync PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pet
)

target_link_libraries(test_lipsync PRIVATE
    Qt6::Test
    Qt6::Core
)

add_test(NAME test_lipsync
    COMMAND test_lipsync
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── LipSync Fallback Config Unit Tests ─────────────────────────────────
# Pure logic tests for sidecar config parsing, mapping, source selection.
# No Cubism SDK, no OpenGL, no Qt Multimedia, no mock server.
# Only Qt6::Test + Qt6::Core (via LipSyncConfig.h).

add_executable(test_lipsync_fallback
    ${CMAKE_CURRENT_SOURCE_DIR}/LipSyncFallbackTest.cpp
    ${CMAKE_SOURCE_DIR}/pet/LipSyncConfig.cpp
)

set_target_properties(test_lipsync_fallback PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_lipsync_fallback PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pet
)

target_link_libraries(test_lipsync_fallback PRIVATE
    Qt6::Test
    Qt6::Core
)

add_test(NAME test_lipsync_fallback
    COMMAND test_lipsync_fallback
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── Emotion Parameter Resolver Unit Tests ────────────────────────────
# Pure logic tests for emotion parameter resolution (mouth source, cheek/brow existence).
# No Cubism SDK, no OpenGL, no Qt Multimedia, no mock server.
# Only Qt6::Test + Qt6::Core (via EmotionParameterResolver.h).

add_executable(test_emotion_parameters
    ${CMAKE_CURRENT_SOURCE_DIR}/EmotionParameterResolverTest.cpp
    ${CMAKE_SOURCE_DIR}/pet/EmotionParameterResolver.cpp
)

set_target_properties(test_emotion_parameters PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_emotion_parameters PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/pet
)

target_link_libraries(test_emotion_parameters PRIVATE
    Qt6::Test
    Qt6::Core
)

add_test(NAME test_emotion_parameters
    COMMAND test_emotion_parameters
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
