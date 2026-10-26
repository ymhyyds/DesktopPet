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
    ${CMAKE_SOURCE_DIR}/ai/DeepSeekClient.cpp
    ${CMAKE_SOURCE_DIR}/ai/PromptManager.cpp
    ${CMAKE_SOURCE_DIR}/ai/EvidenceManager.cpp
    ${CMAKE_SOURCE_DIR}/core/WeatherManager.cpp
)

# Only test_main.cpp — it #includes the other .cpp files internally
set(TEST_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/test_main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/WeatherManagerTest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/EvidenceManagerTest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/MockServer.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DeepSeekClientKeyPersistenceTest.h
    ${CMAKE_CURRENT_SOURCE_DIR}/DeepSeekClientKeyPersistenceTest.cpp
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
    ${CMAKE_CURRENT_SOURCE_DIR}/MockDeepSeekServer.py
    ${CMAKE_CURRENT_BINARY_DIR}/MockDeepSeekServer.py
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
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MockDeepSeekServer.py"
        "$<TARGET_FILE_DIR:test_tts>/MockDeepSeekServer.py"
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

# ── Motion Parameter Lifecycle Unit Tests ──────────────────────────────
# Pure logic tests for: entryBaseline inheritance, pending restore chain,
# conflict resolution, outgoing-only calculation, generation invalidation,
# natural end cleanup, StartMotion failure rollback, Idle selection.
# No Cubism SDK, no OpenGL, no Qt Multimedia, no mock server.

add_executable(test_motion_lifecycle
    ${CMAKE_CURRENT_SOURCE_DIR}/MotionLifecycleTest.cpp
)

set_target_properties(test_motion_lifecycle PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_motion_lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}
)

target_link_libraries(test_motion_lifecycle PRIVATE
    Qt6::Test
    Qt6::Core
)

add_test(NAME test_motion_lifecycle
    COMMAND test_motion_lifecycle
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── DeepSeekClient 网络超时与错误恢复 ──
# Standalone test executable (NOT part of test_tts).
# Uses MockDeepSeekServer.py to avoid real network requests.

add_executable(test_ds_network
    ${CMAKE_CURRENT_SOURCE_DIR}/run_network_tests.cpp
    ${CMAKE_SOURCE_DIR}/ai/DeepSeekClient.cpp
)

set_target_properties(test_ds_network PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_ds_network PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_ds_network PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Network
    Qt6::Gui
    Qt6::Widgets
)

# Copy MockDeepSeekServer.py to build directory
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/MockDeepSeekServer.py
    ${CMAKE_CURRENT_BINARY_DIR}/MockDeepSeekServer.py
    COPYONLY
)

# Also copy to executable output directory
add_custom_command(TARGET test_ds_network POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/MockDeepSeekServer.py"
        "$<TARGET_FILE_DIR:test_ds_network>/MockDeepSeekServer.py"
    COMMENT "Copying MockDeepSeekServer.py for test_ds_network"
    VERBATIM
)

add_test(NAME test_ds_network
    COMMAND test_ds_network
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── AutoStart 注册表操作安全封装 ──
# Pure logic tests using QSettings(IniFormat) — no real registry access.
# Tests savePrevious, applyAutoStart, verifyAutoStart, restorePrevious.

add_executable(test_autostart
    ${CMAKE_CURRENT_SOURCE_DIR}/AutoStartTest.cpp
    ${CMAKE_SOURCE_DIR}/core/AutoStartManager.cpp
)

set_target_properties(test_autostart PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_autostart PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(test_autostart PRIVATE
    Qt6::Test
    Qt6::Core
)

if(WIN32)
    target_link_libraries(test_autostart PRIVATE
        ole32
        shell32
        shlwapi
    )

    add_custom_command(TARGET test_autostart POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>"
            "$<TARGET_FILE_DIR:test_autostart>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "$<TARGET_FILE_DIR:test_autostart>"
        COMMENT "Deploying Qt6 runtime DLLs for test_autostart"
        VERBATIM
    )
endif()

add_test(NAME test_autostart
    COMMAND test_autostart
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── 单实例保护测试 ──
# Tests first-acquire / second-reject / release-and-reacquire / activation signal.
# Uses unique pipe names per test case (no cross-test interference).
# Requires Qt6::Network for QLocalServer/QLocalSocket.
# Uses QTEST_GUILESS_MAIN (QCoreApplication) — no qwindows platform plugin needed.

add_executable(test_single_instance
    ${CMAKE_CURRENT_SOURCE_DIR}/SingleInstanceTest.cpp
    ${CMAKE_SOURCE_DIR}/core/SingleInstanceManager.cpp
)

set_target_properties(test_single_instance PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_single_instance PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(test_single_instance PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Network
)

# 部署 Qt6::Core, Qt6::Network, Qt6::Test 运行时 DLL 到测试 exe 输出目录，
# 使用生成器表达式，不硬编码路径或 Debug/Release 名称。
if(WIN32)
    add_custom_command(TARGET test_single_instance POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>"
            "$<TARGET_FILE_DIR:test_single_instance>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Network>"
            "$<TARGET_FILE_DIR:test_single_instance>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Test>"
            "$<TARGET_FILE_DIR:test_single_instance>"
        COMMENT "Deploying Qt6 runtime DLLs for test_single_instance"
        VERBATIM
    )
endif()

add_test(NAME test_single_instance
    COMMAND test_single_instance
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── 长期记忆（memories 表 / 抽取器纯逻辑） ──
# test_memory_db: 迁移/CRUD/FTS 同步/角色隔离，需要 Qt6::Sql（QSQLITE -> 自带 SQLite）。
# test_memory_extractor: JSON 容错解析 / bigram 相似度 / 关键词推导，不发网络请求。

add_executable(test_memory_db
    ${CMAKE_CURRENT_SOURCE_DIR}/MemoryDBTest.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.h
)

set_target_properties(test_memory_db PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_memory_db PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_memory_db PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Sql
)

add_executable(test_memory_extractor
    ${CMAKE_CURRENT_SOURCE_DIR}/MemoryExtractorTest.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryExtractor.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryExtractor.h
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.h
)

set_target_properties(test_memory_extractor PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(test_memory_extractor PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/ai
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(test_memory_extractor PRIVATE
    Qt6::Test
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

if(WIN32)
    foreach(_mem_test IN ITEMS test_memory_db test_memory_extractor)
        add_custom_command(TARGET ${_mem_test} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:${_mem_test}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Test>" "$<TARGET_FILE_DIR:${_mem_test}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Network>" "$<TARGET_FILE_DIR:${_mem_test}>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::Sql>" "$<TARGET_FILE_DIR:${_mem_test}>"
            VERBATIM
        )
    endforeach()
endif()

add_test(NAME test_memory_db
    COMMAND test_memory_db
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(NAME test_memory_extractor
    COMMAND test_memory_extractor
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# ── 历史回填命令行工具 ──
# 复用 MemoryExtractor::startBackfill，与设置页按钮同一条代码路径。
# 用法见 tools/backfill_history/main.cpp。

add_executable(backfill_history
    ${CMAKE_SOURCE_DIR}/tools/backfill_history/main.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryExtractor.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryExtractor.h
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.h
)

set_target_properties(backfill_history PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(backfill_history PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/ai
)

target_link_libraries(backfill_history PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

if(WIN32)
    add_custom_command(TARGET backfill_history POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:backfill_history>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Network>" "$<TARGET_FILE_DIR:backfill_history>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>" "$<TARGET_FILE_DIR:backfill_history>"
        VERBATIM
    )
endif()

# ── 自动抽取单次验证工具 ──
# 复用 MemoryExtractor::extractFromRecentMessages，与桌宠自动抽取同一条代码路径。
# 用法见 tools/extract_once/main.cpp（建议在数据库副本上跑）。

add_executable(extract_once
    ${CMAKE_SOURCE_DIR}/tools/extract_once/main.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryExtractor.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryExtractor.h
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.cpp
    ${CMAKE_SOURCE_DIR}/memory/MemoryDB.h
)

set_target_properties(extract_once PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(extract_once PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/ai
)

target_link_libraries(extract_once PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::Sql
)

if(WIN32)
    add_custom_command(TARGET extract_once POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>" "$<TARGET_FILE_DIR:extract_once>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Network>" "$<TARGET_FILE_DIR:extract_once>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Sql>" "$<TARGET_FILE_DIR:extract_once>"
        VERBATIM
    )
endif()

# ── 单实例竞争测试辅助程序 ──
# 被 test_single_instance 通过 QProcess 调用，模拟两个进程同时启动。
# 使用自己的 main() + QCoreApplication.

add_executable(concurrent_race_helper
    ${CMAKE_CURRENT_SOURCE_DIR}/concurrent_race_helper.cpp
    ${CMAKE_SOURCE_DIR}/core/SingleInstanceManager.cpp
)

set_target_properties(concurrent_race_helper PROPERTIES
    WIN32_EXECUTABLE FALSE
)

target_include_directories(concurrent_race_helper PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
)

target_link_libraries(concurrent_race_helper PRIVATE
    Qt6::Core
    Qt6::Network
)

# 部署 Qt6::Core, Qt6::Network 运行时 DLL
if(WIN32)
    add_custom_command(TARGET concurrent_race_helper POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Core>"
            "$<TARGET_FILE_DIR:concurrent_race_helper>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:Qt6::Network>"
            "$<TARGET_FILE_DIR:concurrent_race_helper>"
        COMMENT "Deploying Qt6 runtime DLLs for concurrent_race_helper"
        VERBATIM
    )
endif()
