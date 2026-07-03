@echo off
chcp 437 > nul

set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist %VCVARS% (
    echo ERROR: vcvarsall.bat not found
    pause
    exit /b 1
)

echo [1/3] Loading VS 2026 environment...
call %VCVARS% x64
echo OK

cd /d "%~dp0"

echo [2/3] CMake configure and build...
mkdir build 2>nul
cd build

cmake .. -G "Visual Studio 18 2026" -A x64 -DCMAKE_PREFIX_PATH="E:\QT\6.11.1\msvc2022_64" -T v143 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if errorlevel 1 (
    echo CMAKE CONFIGURE FAILED
    pause
    exit /b 1
)

cmake --build . --config Release
if errorlevel 1 (
    echo BUILD FAILED
    pause
    exit /b 1
)
echo OK

echo Deploying Qt DLLs...
cmake --build . --target deploy --config Release
echo OK

echo.
echo ===== BUILD SUCCESSFUL =====
echo Output: build\Release\DesktopPet.exe
echo.
pause
