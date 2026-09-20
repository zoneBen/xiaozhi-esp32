@echo off
rem ===========================================================================
rem  setup_idf.bat  --  一次性安装 ESP-IDF 6.1 的 python 虚拟环境和工具链
rem
rem  何时运行：第一次编译前；或 build.bat / flash.bat 提示找不到 python
rem            虚拟环境时。
rem  安装位置：D:\ESP32\v6.1\v6.1 下的 python_env 和 tools
rem  前置条件：PATH 里要有 python.exe 和 git.exe
rem  注意：需要联网，首次会下载 xtensa 工具链等（几百 MB），耗时较长。
rem        这里只装 esp32s3 目标，尽量减少下载量。
rem ===========================================================================

cd /d "%~dp0"
title XiaoZhi 环境安装 - ESP-IDF 6.1

echo ============================================================
echo  ESP-IDF 6.1 环境安装（只需执行一次）
echo ============================================================
echo.

call "%~dp0idf_env.bat" setup
if errorlevel 1 goto :fail

rem ---- 引导用的 python：PATH 里没有就退而用现有安装器自带的 ---------------
where python.exe >nul 2>nul
if not errorlevel 1 goto :have_python
for /d %%D in ("D:\ESP32\Espressif\tools\idf-python\*") do if exist "%%~D\python.exe" set "PATH=%%~D;%PATH%"
where python.exe >nul 2>nul
if errorlevel 1 (
    echo [错误] PATH 里找不到 python.exe，无法引导安装。
    echo        请先安装 Python 3，或把 python 所在目录加进 PATH。
    goto :fail
)

:have_python
where git.exe >nul 2>nul
if errorlevel 1 (
    echo [错误] PATH 里找不到 git.exe，无法引导安装。
    goto :fail
)

echo.
echo ---- 开始安装 -------------------------------------------------
echo  安装到：%IDF_TOOLS_PATH%
echo  过程中长时间没有输出通常是在下载，请耐心等待。
echo.
call "%IDF_DIR%\install.bat" esp32s3
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  安装完成，现在可以运行 build.bat 编译了。
echo ============================================================
pause
exit /b 0

:fail
echo.
echo ********************************************
echo  安装失败，请查看上面的日志
echo ********************************************
pause
exit /b 1
