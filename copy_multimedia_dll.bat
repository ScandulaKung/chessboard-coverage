@echo off
setlocal enabledelayedexpansion
echo ========================================
echo Copy Qt Multimedia DLL Script
echo ========================================
echo.

REM 查找可执行文件目录
set EXE_DIR=
if exist "build\Release\ColorBottleGame.exe" (
    set EXE_DIR=build\Release
) else if exist "build\Debug\ColorBottleGame.exe" (
    set EXE_DIR=build\Debug
) else if exist "build\ColorBottleGame.exe" (
    set EXE_DIR=build
) else (
    echo Error: ColorBottleGame.exe not found!
    echo Please run build.bat first to compile the project.
    pause
    exit /b 1
)

echo Found executable in: !EXE_DIR!
echo.

REM 尝试自动检测Qt路径
set QT_FOUND=0
set QT_PATH=

if exist "C:\Qt\6.10.1\msvc2022_64" (
    set QT_PATH=C:\Qt\6.10.1\msvc2022_64
    set QT_FOUND=1
) else if exist "C:\Qt\6.10.1\msvc2019_64" (
    set QT_PATH=C:\Qt\6.10.1\msvc2019_64
    set QT_FOUND=1
) else if exist "C:\Qt\6.10.1\mingw_64" (
    set QT_PATH=C:\Qt\6.10.1\mingw_64
    set QT_FOUND=1
)

if !QT_FOUND!==0 (
    echo Warning: Qt path not auto-detected.
    echo Please set QT_PATH manually:
    echo   set QT_PATH=C:\Qt\6.x.x\msvc2022_64
    echo.
    pause
    exit /b 1
)

echo Qt found at: !QT_PATH!
echo.

REM 复制 Qt6 Multimedia DLL
if exist "!QT_PATH!\bin\Qt6Multimedia.dll" (
    echo Copying Qt6Multimedia.dll...
    copy /Y "!QT_PATH!\bin\Qt6Multimedia.dll" "!EXE_DIR!\" >nul 2>&1
    if exist "!EXE_DIR!\Qt6Multimedia.dll" (
        echo   Success: Qt6Multimedia.dll copied to !EXE_DIR!\
    ) else (
        echo   Error: Failed to copy Qt6Multimedia.dll
    )
) else (
    echo Qt6Multimedia.dll not found in !QT_PATH!\bin\
)

REM 复制 Qt5 Multimedia DLL (如果 Qt6 不存在)
if not exist "!EXE_DIR!\Qt6Multimedia.dll" (
    if exist "!QT_PATH!\bin\Qt5Multimedia.dll" (
        echo Copying Qt5Multimedia.dll...
        copy /Y "!QT_PATH!\bin\Qt5Multimedia.dll" "!EXE_DIR!\" >nul 2>&1
        if exist "!EXE_DIR!\Qt5Multimedia.dll" (
            echo   Success: Qt5Multimedia.dll copied to !EXE_DIR!\
        ) else (
            echo   Error: Failed to copy Qt5Multimedia.dll
        )
    ) else (
        echo Qt5Multimedia.dll not found in !QT_PATH!\bin\
    )
)

REM 复制 audio 插件
if exist "!QT_PATH!\plugins\audio" (
    if not exist "!EXE_DIR!\audio" (
        echo Creating audio plugins directory...
        mkdir "!EXE_DIR!\audio"
    )
    
    if exist "!QT_PATH!\plugins\audio\qwindowsmediafoundation.dll" (
        echo Copying qwindowsmediafoundation.dll...
        copy /Y "!QT_PATH!\plugins\audio\qwindowsmediafoundation.dll" "!EXE_DIR!\audio\" >nul 2>&1
        if exist "!EXE_DIR!\audio\qwindowsmediafoundation.dll" (
            echo   Success: qwindowsmediafoundation.dll copied
        )
    )
    
    if exist "!QT_PATH!\plugins\audio\qdirectsound.dll" (
        echo Copying qdirectsound.dll...
        copy /Y "!QT_PATH!\plugins\audio\qdirectsound.dll" "!EXE_DIR!\audio\" >nul 2>&1
        if exist "!EXE_DIR!\audio\qdirectsound.dll" (
            echo   Success: qdirectsound.dll copied
        )
    )
)

echo.
echo ========================================
if exist "!EXE_DIR!\Qt6Multimedia.dll" (
    echo Qt Multimedia DLL deployment: SUCCESS
    echo Sound effects should work now!
) else if exist "!EXE_DIR!\Qt5Multimedia.dll" (
    echo Qt Multimedia DLL deployment: SUCCESS
    echo Sound effects should work now!
) else (
    echo Qt Multimedia DLL deployment: FAILED
    echo.
    echo Possible reasons:
    echo 1. Qt Multimedia module is not installed
    echo 2. Qt path is incorrect
    echo.
    echo To install Qt Multimedia module:
    echo   Run Qt Maintenance Tool and install "Qt Multimedia" component
    echo.
    echo The game will still run, but without sound effects.
)
echo ========================================
echo.
pause
