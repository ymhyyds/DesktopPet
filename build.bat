@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d "C:\Users\余敏涵\Desktop\DesktopPet\build"

echo === CMake Configure ===
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" .. -G "Visual Studio 18 2026" -A x64 -T v143 -DCMAKE_PREFIX_PATH="E:\QT\6.11.1\msvc2022_64" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo === Build ===
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build . --config Release
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

echo === Build SUCCESS ===
