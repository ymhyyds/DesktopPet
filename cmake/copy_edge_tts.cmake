# ── Copy edge_tts PyInstaller bundle to deploy directory ────────────────────
# Invoked from CMakeLists.txt POST_BUILD via:
#   cmake -D SRC_DIR=... -D TARGET_DIR=... -P cmake/copy_edge_tts.cmake
#
# Copies the entire PyInstaller --onedir output (edge_tts_py/).
# Unlike the old .venv approach, PyInstaller output contains NO dev files
# (pip, scripts, .gitignore), so no PATTERN exclusions are needed.
#
# Parameters:
#   SRC_DIR    — source edge_tts_py directory (absolute)
#   TARGET_DIR — DesktopPet.exe output directory (generator expression resolved)

if(NOT DEFINED SRC_DIR)
    message(FATAL_ERROR "[copy_edge_tts] SRC_DIR not specified")
endif()
if(NOT DEFINED TARGET_DIR)
    message(FATAL_ERROR "[copy_edge_tts] TARGET_DIR not specified")
endif()

set(PYI_DST "${TARGET_DIR}/tools/edge_tts/edge_tts_py")

# 源码树里没有 PyInstaller 产物时（例如只保留部署副本、不再重新打包），
# 跳过复制而不是让整个构建失败——TTS 用不到时不该阻塞编译。
if(NOT EXISTS "${SRC_DIR}")
    message(WARNING "[copy_edge_tts] 源目录不存在，跳过复制：${SRC_DIR}")
    return()
endif()

file(COPY "${SRC_DIR}" DESTINATION "${TARGET_DIR}/tools/edge_tts")

message(STATUS "[copy_edge_tts] Done — copied edge_tts_py to ${PYI_DST}")
