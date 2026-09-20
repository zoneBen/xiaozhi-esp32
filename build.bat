@echo off
rem ===========================================================================
rem  build.bat  --  一键编译
rem
rem  用法：  直接双击           编译默认板子 quandong-s3-dev
rem          build.bat 板子名    编译 main/boards 下的其它板子
rem  产物：  build\merged-binary.bin
rem
rem  首次使用请先运行 setup_idf.bat 装好 ESP-IDF 工具链。
rem  板子的选择由 scripts\build.py 完成，它会设置 target 与板型 Kconfig，
rem  然后执行 idf.py build + merge-bin。
rem ===========================================================================

cd /d "%~dp0"
title XiaoZhi 编译 - quandong-s3-dev

set "BOARD=%~1"
if "%BOARD%"=="" set "BOARD=quandong-s3-dev"

echo ============================================================
echo  XiaoZhi 一键编译
echo  板子：%BOARD%
echo  目录：%CD%
echo ============================================================
echo.

call "%~dp0idf_env.bat"
if errorlevel 1 goto :fail

echo.
echo ---- 开始编译 -------------------------------------------------
"%IDF_PYTHON%" "%~dp0scripts\build.py" "%BOARD%"
if errorlevel 1 goto :fail

echo.
echo ============================================================
echo  编译成功
echo  固件：%CD%\build\merged-binary.bin
echo  下一步：运行 flash.bat 烧录
echo ============================================================
pause
exit /b 0

:fail
echo.
echo ********************************************
echo  编译失败，请查看上面的日志
echo ********************************************
pause
exit /b 1
