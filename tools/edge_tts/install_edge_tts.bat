@echo off
REM ── DesktopPet Edge TTS 虚拟环境安装脚本 ──────────────────────────────
REM 在 tools/edge_tts/.venv 中创建 Python 虚拟环境并安装 edge-tts
REM 用法：直接双击运行，或在终端中执行
REM 不需要管理员权限
REM ────────────────────────────────────────────────────────────────────────

setlocal enabledelayedexpansion

echo [EdgeTTS] 正在检测 Python 环境...

REM 检测 Python
set PYTHON=
where py >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set PYTHON=py
    goto :found_python
)

where python >nul 2>&1
if %ERRORLEVEL% equ 0 (
    set PYTHON=python
    goto :found_python
)

echo [EdgeTTS] 错误：未找到 Python（py 或 python）。请先安装 Python 3.8+。
echo [EdgeTTS] 下载地址：https://www.python.org/downloads/
exit /b 1

:found_python
echo [EdgeTTS] 检测到：%PYTHON%
%PYTHON% --version

REM 切换到脚本所在目录
cd /d "%~dp0"

REM 创建虚拟环境
echo [EdgeTTS] 正在创建虚拟环境 .venv ...
%PYTHON% -m venv .venv
if %ERRORLEVEL% neq 0 (
    echo [EdgeTTS] 错误：创建虚拟环境失败
    exit /b 1
)

REM 激活并安装 edge-tts
echo [EdgeTTS] 正在安装/升级 edge-tts ...
call .venv\Scripts\activate.bat
if %ERRORLEVEL% neq 0 (
    echo [EdgeTTS] 错误：激活虚拟环境失败
    exit /b 1
)

pip install --upgrade edge-tts
if %ERRORLEVEL% neq 0 (
    echo [EdgeTTS] 错误：安装 edge-tts 失败
    call deactivate
    exit /b 1
)

echo [EdgeTTS] 安装完成！
echo [EdgeTTS] edge-tts 路径：%~dp0.venv\Scripts\edge-tts.exe

call deactivate
exit /b 0
