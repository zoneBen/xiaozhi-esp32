@echo off
rem Temporary non-interactive build launcher (mirrors build.bat without pause).
cd /d "%~dp0"
call "%~dp0idf_env.bat"
if errorlevel 1 exit /b 1
"%IDF_PYTHON%" "%~dp0scripts\build.py" %1
exit /b %errorlevel%
