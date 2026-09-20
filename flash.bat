@echo off
rem ===========================================================================
rem  flash.bat  --  一键烧录 + 打开串口监视器
rem
rem  用法：  直接双击         自动探测串口
rem          flash.bat COM5   指定串口
rem  前置：  先运行 build.bat 编译；若没编译过会提示是否现在编译
rem
rem  退出监视器：Ctrl + ]
rem ===========================================================================

cd /d "%~dp0"
title XiaoZhi 烧录 - quandong-s3-dev

set "PORT_ARG="
if not "%~1"=="" set "PORT_ARG=-p %~1"

echo ============================================================
echo  XiaoZhi 一键烧录（默认板子 quandong-s3-dev）
echo ============================================================
if "%PORT_ARG%"=="" echo  串口：自动探测
if not "%PORT_ARG%"=="" echo  串口：%~1
echo.

call "%~dp0idf_env.bat"
if errorlevel 1 goto :fail

if exist "build\flasher_args.json" goto :do_flash

echo [!] 还没有编译过（找不到 build\flasher_args.json）。
set /p "ANSWER=现在先执行 build.bat 编译吗？[Y/N] "
if /i not "%ANSWER%"=="Y" (
    echo [错误] 已取消，请先运行 build.bat。
    goto :fail
)
call "%~dp0build.bat"
if errorlevel 1 goto :fail

:do_flash
echo.
echo ---- 开始烧录 -------------------------------------------------
"%IDF_PYTHON%" "%IDF_PATH%\tools\idf.py" %PORT_ARG% flash monitor
if errorlevel 1 goto :fail

echo.
echo 烧录结束。
pause
exit /b 0

:fail
echo.
echo ********************************************
echo  烧录失败，请查看上面的日志
echo ********************************************
pause
exit /b 1
