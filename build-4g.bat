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
build.bat quandong-s3-dev-4g
