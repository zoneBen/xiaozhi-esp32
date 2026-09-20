@echo off
rem ===========================================================================
rem  idf_env.bat  --  ESP-IDF 环境准备（供 build.bat / flash.bat / setup_idf.bat 调用）
rem
rem  用法：  call idf_env.bat           准备编译环境（要求工具链已装好）
rem          call idf_env.bat setup     只定位路径，供 setup_idf.bat 安装前使用
rem
rem  成功后向调用者导出：IDF_PATH、IDF_TOOLS_PATH、IDF_PYTHON、IDF_PYTHON_ENV_PATH
rem
rem  本工程要求 ESP-IDF >= 6.0.1（见 main/idf_component.yml）。
rem  注意：本脚本会覆盖系统里残留的旧值 —— 本机的 IDF_PATH / IDF_TOOLS_PATH
rem        全局指向 5.5.2，不覆盖会让 6.1 的编译用到错版本。
rem  特意不使用 setlocal —— 环境变量必须传递给调用者。
rem ===========================================================================

rem ------------------------------- 可配置区 ---------------------------------
rem 1) ESP-IDF 源码目录（该目录下必须有 export.bat）。留空 = 按候选列表探测。
set "IDF_DIR=D:\ESP32\v6.1\v6.1\esp-idf"

rem 2) IDF 工具目录（python_env、tools 装在这里）。留空 = 用 ESP-IDF 的上级目录。
set "IDF_TOOLS_DIR=D:\ESP32\v6.1\v6.1"

rem 3) 上面 IDF_DIR 留空时的自动探测候选，分号分隔，按顺序命中即用。
set "IDF_CANDIDATES=D:\ESP32\v6.1\v6.1\esp-idf;D:\ESP32\Espressif\frameworks\esp-idf-v6.1;%USERPROFILE%\.espressif\frameworks\esp-idf-v6.1;C:\Espressif\frameworks\esp-idf-v6.1"
rem --------------------------------------------------------------------------

set "IDF_MODE=%~1"

rem Git Bash / MSYS2 环境下 IDF 的 .bat 会直接退出，先清掉该变量
set "MSYSTEM="

rem ---- 1. 定位 ESP-IDF：候选目录本身或它下一层子目录中含 export.bat -------
if not defined IDF_DIR (
    for %%P in ("%IDF_CANDIDATES:;=" "%") do (
        if not defined IDF_DIR if exist "%%~P\export.bat" set "IDF_DIR=%%~P"
        if not defined IDF_DIR for /d %%Q in ("%%~P\*") do if not defined IDF_DIR if exist "%%~Q\export.bat" set "IDF_DIR=%%~Q"
    )
)
if not defined IDF_DIR goto :fail_no_idf
if not exist "%IDF_DIR%\export.bat" goto :fail_no_idf

rem ---- 2. 读版本号（MAJOR.MINOR），用于匹配 python 虚拟环境的目录名 -------
set "IDF_VER_MAJOR="
set "IDF_VER_MINOR="
if exist "%IDF_DIR%\tools\cmake\version.cmake" (
    for /f "tokens=3 delims=() " %%A in ('findstr /b "set(IDF_VERSION_MAJOR" "%IDF_DIR%\tools\cmake\version.cmake"') do set "IDF_VER_MAJOR=%%A"
    for /f "tokens=3 delims=() " %%A in ('findstr /b "set(IDF_VERSION_MINOR" "%IDF_DIR%\tools\cmake\version.cmake"') do set "IDF_VER_MINOR=%%A"
)
if not defined IDF_VER_MAJOR goto :fail_incomplete_tree
if not defined IDF_VER_MINOR goto :fail_incomplete_tree
set "IDF_VER=%IDF_VER_MAJOR%.%IDF_VER_MINOR%"

rem ---- 3. 统一成绝对路径（消掉 ..），否则 IDF 会因路径字符串不一致报警告 --
for %%R in ("%IDF_DIR%") do set "IDF_DIR=%%~fR"
if not defined IDF_TOOLS_DIR set "IDF_TOOLS_DIR=%IDF_DIR%\.."
for %%R in ("%IDF_TOOLS_DIR%") do set "IDF_TOOLS_DIR=%%~fR"

rem 覆盖系统里可能残留的旧值（本机指向 5.5.2 的安装目录）
set "IDF_PATH=%IDF_DIR%"
set "IDF_TOOLS_PATH=%IDF_TOOLS_DIR%"

echo [信息] ESP-IDF 目录：%IDF_DIR%
echo [信息] ESP-IDF 版本：%IDF_VER%
echo [信息] IDF 工具目录：%IDF_TOOLS_PATH%

rem ---- 4. 版本门槛：本工程要求 >= 6.0.1 -----------------------------------
if %IDF_VER_MAJOR% LSS 6 (
    echo.
    echo [错误] 当前 ESP-IDF 为 %IDF_VER%，本工程要求 ^>= 6.0.1。
    echo        请修改 "%~f0" 顶部的 IDF_DIR，指向 ESP-IDF v6.x。
    exit /b 1
)

rem ---- 5. 把工具自带的 python / git 放进 PATH，双击运行时也能找到 --------
rem 注意：下面每条 for 必须各自成句，否则第二条拿到的 PATH 不含第一条的修改
for /d %%D in ("%IDF_TOOLS_PATH%\python_env\idf%IDF_VER%_py*_env") do if exist "%%~D\Scripts\python.exe" set "PATH=%%~D\Scripts;%PATH%"
for /d %%D in ("%IDF_TOOLS_PATH%\tools\idf-git\*") do if exist "%%~D\cmd\git.exe" set "PATH=%%~D\cmd;%PATH%"

rem ---- 6. setup 模式：只提供路径，装工具链是 setup_idf.bat 的事 ----------
if /i "%IDF_MODE%"=="setup" exit /b 0

rem ---- 7. 本版本对应的 python 虚拟环境是否已安装 -------------------------
set "IDF_PY_ENV="
for /d %%D in ("%IDF_TOOLS_PATH%\python_env\idf%IDF_VER%_py*_env") do if exist "%%~D\Scripts\python.exe" set "IDF_PY_ENV=%%~D"
if not defined IDF_PY_ENV goto :fail_no_venv

rem ---- 8. 执行 export.bat，激活 IDF 环境 --------------------------------
call "%IDF_DIR%\export.bat"
if errorlevel 1 goto :fail_export
if not defined IDF_PATH goto :fail_export

rem ---- 9. 选定 python：优先用 IDF 自带的虚拟环境 ------------------------
set "IDF_PYTHON=python"
if defined IDF_PYTHON_ENV_PATH if exist "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" set "IDF_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe"

echo [信息] ESP-IDF 版本自检：
"%IDF_PYTHON%" "%IDF_PATH%\tools\idf.py" --version
if errorlevel 1 goto :fail_idf_py
exit /b 0

rem ============================== 错误处理 ==================================

:fail_no_idf
echo.
echo [错误] 没有找到可用的 ESP-IDF（目录下缺少 export.bat）。
echo        请用记事本打开 "%~f0"，把 IDF_DIR 设置成你的 ESP-IDF 路径，
echo        或把该路径补充到 IDF_CANDIDATES 里。
echo        当前候选：%IDF_CANDIDATES%
exit /b 1

:fail_incomplete_tree
echo.
echo [错误] ESP-IDF 目录不完整：读不到 tools\cmake\version.cmake 里的版本号。
echo        目录：%IDF_DIR%
exit /b 1

:fail_no_venv
echo.
echo [错误] 没有找到 IDF %IDF_VER% 对应的 python 虚拟环境。
echo        期望位置：%IDF_TOOLS_PATH%\python_env\idf%IDF_VER%_py*_env\Scripts\python.exe
echo        请先运行一次 setup_idf.bat 完成工具链安装（需联网，耗时较长）。
exit /b 1

:fail_export
echo.
echo [错误] ESP-IDF 环境初始化失败（export.bat 未成功）。
exit /b 1

:fail_idf_py
echo.
echo [错误] 无法执行 idf.py，ESP-IDF 环境可能不完整。
exit /b 1
