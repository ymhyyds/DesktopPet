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
)

# Only test_main.cpp — it #includes the other .cpp files internally
set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
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
)

add_test(NAME test_tts
    COMMAND test_tts
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
