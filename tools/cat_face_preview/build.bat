@echo off
rem Build the host preview. Requires any C++20 host compiler; TDM-GCC is what
rem this was developed against.
rem
rem The geometry sources are compiled straight out of the board directory --
rem there is no copy of them here on purpose. One source, two backends.

setlocal
cd /d "%~dp0"

set BOARD=..\..\main\boards\quandong-s3-dev-4g

set SOURCES=%BOARD%\cat_face.cc ^
 %BOARD%\cat_face_shapes.cc ^
 %BOARD%\cat_face_marks.cc ^
 %BOARD%\cat_face_table.cc ^
 %BOARD%\cat_face_anim.cc

g++ -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter ^
 -I"%BOARD%" -o preview.exe main.cc %SOURCES%

if errorlevel 1 (
  echo.
  echo build failed
  exit /b 1
)

echo build ok
echo.
echo   preview.exe --selftest   check the invariants
echo   preview.exe              write preview.html
