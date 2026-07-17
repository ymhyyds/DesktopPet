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

file(COPY "${SRC_DIR}" DESTINATION "${TARGET_DIR}/tools/edge_tts")

message(STATUS "[copy_edge_tts] Done — copied edge_tts_py to ${PYI_DST}")
