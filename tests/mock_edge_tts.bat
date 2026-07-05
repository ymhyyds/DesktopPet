@echo off
REM Mock edge-tts batch launcher — delegates to mock_edge_tts.py
REM Used by the test suite to avoid hitting the real Edge TTS service.
REM This file is copied to the build directory by CMake.
python "%~dp0mock_edge_tts.py" %*
exit /b %ERRORLEVEL%
