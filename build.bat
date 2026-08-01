@echo off
setlocal enabledelayedexpansion
echo ========================================
echo Color Bottle Game - Build Script (Qt)
echo ========================================
echo.

REM 检查CMake是否安装
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Error: CMake is not installed or not in PATH
    echo Please install CMake from https://cmake.org/download/
    pause
    exit /b 1
)

REM 创建build目录
if not exist build (
    echo Creating build directory...
    mkdir build
)

cd build

REM 清理旧的CMake缓存（如果存在）
if exist CMakeCache.txt (
    echo Cleaning old CMake cache...
    del /Q CMakeCache.txt >nul 2>&1
)
if exist CMakeFiles (
    echo Cleaning old CMakeFiles...
    rmdir /S /Q CMakeFiles >nul 2>&1
)

REM 尝试自动检测Qt路径
set QT_FOUND=0
set QT_PATH=

REM 检查常见的Qt安装路径（按优先级排序）
REM 优先使用Qt MSVC 2022版本
REM 检查Qt 6.10.1（用户指定路径）
set USE_MINGW=0
if exist "C:\Qt\6.10.1\msvc2022_64" (
    set QT_PATH=C:\Qt\6.10.1\msvc2022_64
    set QT_FOUND=1
    set USE_MINGW=0
) else if exist "C:\Qt\6.10.1\msvc2019_64" (
    set QT_PATH=C:\Qt\6.10.1\msvc2019_64
    set QT_FOUND=1
    set USE_MINGW=0
) else if exist "C:\Qt\6.10.1\mingw_64" (
    set QT_PATH=C:\Qt\6.10.1\mingw_64
    set QT_FOUND=1
    set USE_MINGW=1
) else if exist "C:\Qt\6.7.0\msvc2019_64" (
    set QT_PATH=C:\Qt\6.7.0\msvc2019_64
    set QT_FOUND=1
    set USE_MINGW=0
) else if exist "C:\Qt\6.6.0\msvc2019_64" (
    set QT_PATH=C:\Qt\6.6.0\msvc2019_64
    set QT_FOUND=1
    set USE_MINGW=0
) else if exist "C:\Qt\6.5.0\msvc2019_64" (
    set QT_PATH=C:\Qt\6.5.0\msvc2019_64
    set QT_FOUND=1
    set USE_MINGW=0
) else if exist "C:\Qt\5.15.2\msvc2019_64" (
    set QT_PATH=C:\Qt\5.15.2\msvc2019_64
    set QT_FOUND=1
    set USE_MINGW=0
)

REM 如果找到Qt，设置CMAKE_PREFIX_PATH
if !QT_FOUND!==1 (
    echo Qt found at: !QT_PATH!
    set CMAKE_PREFIX_PATH=!QT_PATH!;%CMAKE_PREFIX_PATH%
) else (
    echo Warning: Qt path not auto-detected.
    echo If CMake fails, set CMAKE_PREFIX_PATH manually:
    echo   set CMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2019_64
    echo.
)

REM 设置环境变量以绕过Qt许可证检查（如果使用商业版本）
set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1

REM 运行CMake配置
echo.
echo Configuring project with CMake...
goto :cmake_config_!USE_MINGW!

:cmake_config_1
echo Using MinGW Makefiles generator (Qt Open Source - No License Required)...
if !QT_FOUND!==1 (
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=!QT_PATH!
) else (
    cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
)
goto :cmake_config_done

:cmake_config_0
echo Using Visual Studio 2022 (MSVC 17) generator...
if !QT_FOUND!==1 (
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=!QT_PATH!
) else (
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
)

:cmake_config_done
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Error: CMake configuration failed!
    echo.
    echo Common issues:
    echo 1. Qt not found - Make sure Qt is installed
    echo    Download from: https://www.qt.io/download
    echo 2. Set CMAKE_PREFIX_PATH to your Qt installation:
    echo    set CMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2019_64
    echo 3. CMake version too old - Update CMake to 3.16 or higher
    echo.
    pause
    exit /b 1
)

REM 编译项目
echo.
echo Building project...
goto :build_!USE_MINGW!

:build_1
echo Note: Using MinGW Makefiles (Release configuration)...
cmake --build .
goto :build_done

:build_0
echo Note: Using Release configuration...
cmake --build . --config Release --verbose

:build_done
if %ERRORLEVEL% NEQ 0 (
    echo.
    echo Error: Build failed!
    pause
    exit /b 1
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.

REM 查找可执行文件
set EXE_PATH=
set EXE_DIR=
if exist Release\ColorBottleGame.exe (
    set EXE_PATH=build\Release\ColorBottleGame.exe
    set EXE_DIR=Release
) else if exist Debug\ColorBottleGame.exe (
    set EXE_PATH=build\Debug\ColorBottleGame.exe
    set EXE_DIR=Debug
) else if exist ColorBottleGame.exe (
    set EXE_PATH=build\ColorBottleGame.exe
    set EXE_DIR=.
)

REM 部署Qt DLL文件（优先使用windeployqt确保版本一致）
if defined EXE_DIR (
    echo Deploying Qt DLL files...
    if !QT_FOUND!==1 (
        REM 清理旧的Qt DLL文件（避免版本冲突）
        echo.
        echo Cleaning old Qt DLL files to avoid version conflicts...
        if exist "!EXE_DIR!\Qt*.dll" (
            del /Q "!EXE_DIR!\Qt*.dll" >nul 2>&1
            echo   Old Qt DLL files removed
        )
        if exist "!EXE_DIR!\platforms" (
            rmdir /S /Q "!EXE_DIR!\platforms" >nul 2>&1
        )
        
        set USE_WINDEPLOYQT=0
        if exist "!QT_PATH!\bin\windeployqt.exe" (
            echo.
            echo Running windeployqt to deploy Qt libraries (ensures version consistency)...
            pushd !EXE_DIR!
            "!QT_PATH!\bin\windeployqt.exe" --release --compiler-runtime ColorBottleGame.exe
            set WINDEPLOY_RESULT=!ERRORLEVEL!
            popd
            if !WINDEPLOY_RESULT!==0 (
                echo   windeployqt completed successfully - Qt DLLs deployed
                set USE_WINDEPLOYQT=1
            ) else (
                echo   Warning: windeployqt returned error code !WINDEPLOY_RESULT!
            )
        )
        
        REM 确保 platforms 插件目录存在（即使 windeployqt 失败）
        if not exist "!EXE_DIR!\platforms" (
            if exist "!QT_PATH!\plugins\platforms" (
                echo.
                echo Creating platforms directory and copying qwindows.dll...
                mkdir "!EXE_DIR!\platforms"
                if exist "!QT_PATH!\plugins\platforms\qwindows.dll" (
                    copy /Y "!QT_PATH!\plugins\platforms\qwindows.dll" "!EXE_DIR!\platforms\" >nul 2>&1
                    if exist "!EXE_DIR!\platforms\qwindows.dll" (
                        echo   qwindows.dll copied successfully
                    )
                )
            )
        )
        
        REM 如果windeployqt失败或不存在，手动复制（不推荐，可能导致版本不匹配）
        if !USE_WINDEPLOYQT!==0 (
            echo.
            echo Warning: Using manual DLL copying (may cause version mismatch issues)
            echo   If you encounter DLL errors, ensure windeployqt works correctly
            if exist "!QT_PATH!\bin\Qt6Core.dll" (
                copy /Y "!QT_PATH!\bin\Qt6Core.dll" "!EXE_DIR!\" >nul 2>&1
                echo   Qt6Core.dll copied
            )
            if exist "!QT_PATH!\bin\Qt6Widgets.dll" (
                copy /Y "!QT_PATH!\bin\Qt6Widgets.dll" "!EXE_DIR!\" >nul 2>&1
                echo   Qt6Widgets.dll copied
            )
            if exist "!QT_PATH!\bin\Qt6Gui.dll" (
                copy /Y "!QT_PATH!\bin\Qt6Gui.dll" "!EXE_DIR!\" >nul 2>&1
                echo   Qt6Gui.dll copied
            )
            if exist "!QT_PATH!\bin\Qt6Svg.dll" (
                copy /Y "!QT_PATH!\bin\Qt6Svg.dll" "!EXE_DIR!\" >nul 2>&1
                echo   Qt6Svg.dll copied
            )
            if exist "!QT_PATH!\plugins\platforms" (
                if not exist "!EXE_DIR!\platforms" mkdir "!EXE_DIR!\platforms"
                copy /Y "!QT_PATH!\plugins\platforms\qwindows.dll" "!EXE_DIR!\platforms\" >nul 2>&1
                if exist "!EXE_DIR!\platforms\qwindows.dll" (
                    echo   Qt platform plugin copied
                )
            )
        )
        
        REM 验证Qt SVG DLL是否已部署
        echo.
        echo Verifying Qt Svg DLL deployment...
        set SVG_DLL_FOUND=0
        if exist "!EXE_DIR!\Qt6Svg.dll" (
            echo   Qt6Svg.dll found in !EXE_DIR!\
            set SVG_DLL_FOUND=1
        ) else if exist "!EXE_DIR!\Qt5Svg.dll" (
            echo   Qt5Svg.dll found in !EXE_DIR!\
            set SVG_DLL_FOUND=1
        ) else (
            echo   Warning: Qt Svg DLL not found
            if exist "!QT_PATH!\bin\Qt6Svg.dll" (
                copy /Y "!QT_PATH!\bin\Qt6Svg.dll" "!EXE_DIR!\" >nul 2>&1
                if exist "!EXE_DIR!\Qt6Svg.dll" (
                    echo   Qt6Svg.dll manually copied (WARNING: may cause version mismatch!)
                    set SVG_DLL_FOUND=1
                )
            )
        )
        
        REM 复制Qt Multimedia DLL和相关插件（用于音效）
        REM 无论 windeployqt 是否成功，都尝试手动复制 Multimedia DLL
        echo.
        echo Copying Qt Multimedia DLL and plugins for sound effects...
        set MULTIMEDIA_DLL_FOUND=0
        
        REM 首先检查是否已经存在（windeployqt 可能已经复制了）
        if exist "!EXE_DIR!\Qt6Multimedia.dll" (
            echo   Qt6Multimedia.dll already exists (deployed by windeployqt)
            set MULTIMEDIA_DLL_FOUND=1
        ) else if exist "!EXE_DIR!\Qt5Multimedia.dll" (
            echo   Qt5Multimedia.dll already exists (deployed by windeployqt)
            set MULTIMEDIA_DLL_FOUND=1
        ) else (
            REM 如果不存在，尝试从 Qt 安装目录复制
            REM 检查并复制 Qt6 Multimedia DLL
            if exist "!QT_PATH!\bin\Qt6Multimedia.dll" (
                echo   Copying Qt6Multimedia.dll from Qt installation...
                copy /Y "!QT_PATH!\bin\Qt6Multimedia.dll" "!EXE_DIR!\" >nul 2>&1
                if exist "!EXE_DIR!\Qt6Multimedia.dll" (
                    echo   Qt6Multimedia.dll copied successfully
                    set MULTIMEDIA_DLL_FOUND=1
                ) else (
                    echo   Failed to copy Qt6Multimedia.dll
                )
            )
            
            REM 检查并复制 Qt5 Multimedia DLL
            if !MULTIMEDIA_DLL_FOUND!==0 (
                if exist "!QT_PATH!\bin\Qt5Multimedia.dll" (
                    echo   Copying Qt5Multimedia.dll from Qt installation...
                    copy /Y "!QT_PATH!\bin\Qt5Multimedia.dll" "!EXE_DIR!\" >nul 2>&1
                    if exist "!EXE_DIR!\Qt5Multimedia.dll" (
                        echo   Qt5Multimedia.dll copied successfully
                        set MULTIMEDIA_DLL_FOUND=1
                    ) else (
                        echo   Failed to copy Qt5Multimedia.dll
                    )
                )
            )
        )
        
        REM 复制 Multimedia 插件（audio 插件）
        REM 检查是否已经存在 audio 目录
        if not exist "!EXE_DIR!\audio" (
            if exist "!QT_PATH!\plugins\audio" (
                echo   Copying audio plugins...
                mkdir "!EXE_DIR!\audio"
                if exist "!QT_PATH!\plugins\audio\qwindowsmediafoundation.dll" (
                    copy /Y "!QT_PATH!\plugins\audio\qwindowsmediafoundation.dll" "!EXE_DIR!\audio\" >nul 2>&1
                    if exist "!EXE_DIR!\audio\qwindowsmediafoundation.dll" (
                        echo   qwindowsmediafoundation.dll copied
                    )
                )
                if exist "!QT_PATH!\plugins\audio\qdirectsound.dll" (
                    copy /Y "!QT_PATH!\plugins\audio\qdirectsound.dll" "!EXE_DIR!\audio\" >nul 2>&1
                    if exist "!EXE_DIR!\audio\qdirectsound.dll" (
                        echo   qdirectsound.dll copied
                    )
                )
            )
        ) else (
            echo   Audio plugins directory already exists
        )
        
        REM 验证 Multimedia DLL 是否存在
        echo.
        echo Verifying Qt Multimedia DLL deployment...
        if exist "!EXE_DIR!\Qt6Multimedia.dll" (
            echo   Qt6Multimedia.dll found in !EXE_DIR!\ - Sound effects enabled
            set MULTIMEDIA_DLL_FOUND=1
        ) else if exist "!EXE_DIR!\Qt5Multimedia.dll" (
            echo   Qt5Multimedia.dll found in !EXE_DIR!\ - Sound effects enabled
            set MULTIMEDIA_DLL_FOUND=1
        ) else (
            echo   Warning: Qt Multimedia DLL not found in !EXE_DIR!\
            echo   Sound effects will be disabled at runtime
            echo   To enable sound effects:
            echo     1. Install Qt Multimedia module
            echo     2. Or manually copy Qt6Multimedia.dll to !EXE_DIR!\
            set MULTIMEDIA_DLL_FOUND=0
        )
    ) else (
        echo   Warning: Qt path not found, skipping DLL deployment
    )
    
    REM 复制音效文件
    echo.
    echo Copying sound effects...
    if exist "..\sounds" (
        if not exist "!EXE_DIR!\sounds" mkdir "!EXE_DIR!\sounds"
        if exist "..\sounds\food.wav" (
            copy /Y "..\sounds\food.wav" "!EXE_DIR!\sounds\" >nul 2>&1
            echo   food.wav copied
        )
        if exist "..\sounds\gameover.wav" (
            copy /Y "..\sounds\gameover.wav" "!EXE_DIR!\sounds\" >nul 2>&1
            echo   gameover.wav copied
        )
    ) else (
        echo   Warning: sounds directory not found
    )
    echo.
)

echo Executable location:
if defined EXE_PATH (
    echo   !EXE_PATH!
    echo.
    if defined SVG_DLL_FOUND (
        if !SVG_DLL_FOUND!==1 (
            echo Qt Svg DLL deployment: OK
        ) else (
            echo Qt Svg DLL deployment: FAILED - Game may not run correctly
            echo Please ensure Qt Svg module is installed and windeployqt works
        )
    )
    echo.
    echo To run the game, execute the .exe file above.
    echo.
    echo NOTE: If you encounter DLL version mismatch errors:
    echo   1. Delete all Qt*.dll files from !EXE_DIR!\
    echo   2. Delete the platforms directory from !EXE_DIR!\
    echo   3. Run: !QT_PATH!\bin\windeployqt.exe --release !EXE_DIR!\ColorBottleGame.exe
    echo.
    echo   Ensure the Qt version used for compilation matches the deployed DLLs.
) else (
    echo   (Not found)
)

echo.
pause
