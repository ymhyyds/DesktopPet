@echo off
REM ── Build edge-tts self-contained package via PyInstaller ────────────────
REM Builds edge_tts_py/ — a portable --onedir bundle with zero Python dependency.
REM Run from the DesktopPet root directory or tools/edge_tts/.
REM
REM Prerequisites: Python 3.x, .venv activated (or pyinstaller installed)
REM
REM Usage:
REM   build_pyinstaller.bat
REM
REM Output:
REM   tools/edge_tts/edge_tts_py/
REM     edge-tts.exe       ← native PE, no Python needed
REM     _internal/         ← DLLs, .pyd, packages, stdlib
REM ==========================================================================

setlocal enabledelayedexpansion

REM ── Detect location ──────────────────────────────────────────────────────
if exist "%~dp0edge_tts_entry.py" (
    set WORK_DIR=%~dp0
) else if exist "%~dp0tools\edge_tts\edge_tts_entry.py" (
    set WORK_DIR=%~dp0tools\edge_tts\
) else (
    echo [ERROR] Cannot find edge_tts_entry.py. Run from project root or tools/edge_tts/
    exit /b 1
)

set ENTRY_SCRIPT=%WORK_DIR%edge_tts_entry.py
set VENV_PY=%WORK_DIR%.venv\Scripts\python.exe

REM ── Find Python interpreter ──────────────────────────────────────────────
if exist "%VENV_PY%" (
    set PYTHON=%VENV_PY%
) else (
    where python >nul 2>nul
    if !ERRORLEVEL! neq 0 (
        echo [ERROR] Python not found in PATH and no .venv exists
        exit /b 1
    )
    set PYTHON=python
)

REM ── Ensure PyInstaller is installed ──────────────────────────────────────
"%PYTHON%" -m pip install pyinstaller 2>nul
if !ERRORLEVEL! neq 0 (
    echo [ERROR] Failed to install PyInstaller
    exit /b 1
)

REM ── Clean previous build ─────────────────────────────────────────────────
if exist "%WORK_DIR%edge_tts_py\" (
    echo [INFO] Removing previous edge_tts_py/...
    rmdir /s /q "%WORK_DIR%edge_tts_py\"
)
if exist "%WORK_DIR%build\pyinstaller\" (
    rmdir /s /q "%WORK_DIR%build\"
)
if exist "%WORK_DIR%edge-tts.spec" del "%WORK_DIR%edge-tts.spec"

REM ── Build ────────────────────────────────────────────────────────────────
echo [INFO] Building edge-tts PyInstaller bundle...
cd /d "%WORK_DIR%"

"%PYTHON%" -m PyInstaller --onedir --name edge-tts --distpath . ^
    --workpath build\pyinstaller ^
    --noconfirm ^
    --hidden-import edge_tts ^
    --hidden-import edge_tts.__main__ ^
    --hidden-import edge_tts.util ^
    --hidden-import edge_tts.communicate ^
    --hidden-import edge_tts.drm ^
    --hidden-import edge_tts.srt_composer ^
    --hidden-import edge_tts.submaker ^
    --hidden-import edge_tts.voices ^
    --collect-all edge_tts ^
    --collect-all aiohttp ^
    --collect-all yarl ^
    --collect-all multidict ^
    --collect-all frozenlist ^
    --collect-all aiosignal ^
    --collect-all attrs ^
    --collect-all propcache ^
    edge_tts_entry.py

if !ERRORLEVEL! neq 0 (
    echo [ERROR] PyInstaller build failed
    exit /b 1
)

REM ── Rename output ────────────────────────────────────────────────────────
if exist "edge-tts\" (
    ren edge-tts edge_tts_py
)

REM ── Clean build intermediates ────────────────────────────────────────────
if exist "build\" rmdir /s /q "build\"
if exist "edge-tts.spec" del edge-tts.spec

REM ── Verify ───────────────────────────────────────────────────────────────
if not exist "edge_tts_py\edge-tts.exe" (
    echo [ERROR] Build verification failed: edge_tts_py\edge-tts.exe not found
    exit /b 1
)

for %%F in (edge_tts_py\edge-tts.exe) do set EXE_SIZE=%%~zF
echo [OK] Build complete: edge_tts_py\edge-tts.exe (%EXE_SIZE% bytes)
echo [OK] edge-tts_py structure:
dir /s /a-d edge_tts_py\ 2>nul | find "File(s)"

REM ── Quick smoke test ─────────────────────────────────────────────────────
echo [INFO] Running smoke test...
edge_tts_py\edge-tts.exe --version
if !ERRORLEVEL! equ 0 (
    echo [OK] Smoke test passed — edge-tts.exe is functional
) else (
    echo [WARN] Smoke test failed — check build output
)

exit /b 0
