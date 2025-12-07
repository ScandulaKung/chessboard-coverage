@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

echo ========================================
echo Chessboard Coverage - CMake Build Script
echo ========================================
echo.

REM Check if CMake is available
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found!
    echo Please install CMake and add it to PATH
    echo Download from: https://cmake.org/download/
    echo.
    pause
    exit /b 1
)

REM Set SFML path (modify according to your installation)
set SFML_PATH=C:\SFML-2_5_1

REM Set build type: Debug or Release
set BUILD_TYPE=Release

REM Set generator: Visual Studio 2022 or Ninja
REM For Visual Studio, set to "Visual Studio 17 2022"
REM For Ninja, set to "Ninja"
set GENERATOR=Visual Studio 17 2022

REM Set build directory
set BUILD_DIR=build_chessboard

echo [Configuration]
echo SFML Path: %SFML_PATH%
echo Build Type: %BUILD_TYPE%
echo Generator: %GENERATOR%
echo Build Directory: %BUILD_DIR%
echo.

REM Check SFML path
if not exist "%SFML_PATH%" (
    echo [ERROR] SFML path not found: %SFML_PATH%
    echo Please modify SFML_PATH variable in build_chessboard.bat
    echo.
    pause
    exit /b 1
)

REM Check SFML directory structure
if not exist "%SFML_PATH%\include" (
    echo [ERROR] SFML path incorrect, include directory not found
    echo Please ensure SFML is properly extracted
    echo.
    pause
    exit /b 1
)

if not exist "%SFML_PATH%\lib" (
    echo [ERROR] SFML path incorrect, lib directory not found
    echo Please ensure SFML is properly extracted
    echo.
    pause
    exit /b 1
)

REM Create resources folder if it doesn't exist
if not exist "resources" (
    echo Creating resources folder...
    mkdir resources
)

echo [Step 1] Preparing build directory...
if exist "%BUILD_DIR%" (
    echo Build directory exists, will overwrite existing configuration...
) else (
    echo Creating build directory...
    mkdir "%BUILD_DIR%" 2>nul
)

echo.
echo [Step 2] Entering build directory...
cd "%BUILD_DIR%"

echo.
echo [Step 3] Preparing CMakeLists file...
REM Backup original CMakeLists.txt if it exists
if exist ..\CMakeLists.txt (
    if not exist ..\CMakeLists_original_backup.txt (
        copy /Y ..\CMakeLists.txt ..\CMakeLists_original_backup.txt >nul 2>&1
    )
)
REM Copy chessboard CMakeLists as the active one
copy /Y ..\CMakeLists_chessboard.txt ..\CMakeLists.txt >nul 2>&1

echo.
echo [Step 4] Configuring CMake (will overwrite existing configuration)...
if "%GENERATOR%"=="Ninja" (
    cmake .. -G "%GENERATOR%" ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DSFML_ROOT="%SFML_PATH%" ^
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
) else (
    cmake .. -G "%GENERATOR%" ^
        -DSFML_ROOT="%SFML_PATH%" ^
        -A x64 ^
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

set CMAKE_CONFIG_EXIT_CODE=!ERRORLEVEL!
echo CMake configuration exit code: !CMAKE_CONFIG_EXIT_CODE!

if !CMAKE_CONFIG_EXIT_CODE!==0 goto :cmake_config_success

echo.
echo ========================================
echo [ERROR] CMake configuration failed!
echo Exit code: !CMAKE_CONFIG_EXIT_CODE!
echo ========================================
echo.
echo Please check:
echo 1. Is CMake installed
echo 2. Is SFML path correct: %SFML_PATH%
echo 3. Is Visual Studio 2022 installed (if using Visual Studio generator)
echo 4. Check the error messages above for details
echo.
cd ..
pause
exit /b !CMAKE_CONFIG_EXIT_CODE!

:cmake_config_success

echo.
echo [Step 4] CMake configuration completed successfully!
echo Continuing to Step 5...
echo.

echo.
echo ========================================
echo [Step 5] Building project...
echo ========================================
echo Using configuration: %BUILD_TYPE%
if "%GENERATOR%"=="Ninja" (
    cmake --build . --config %BUILD_TYPE% --parallel
) else (
    echo Running: cmake --build . --config %BUILD_TYPE% --parallel
    cmake --build . --config %BUILD_TYPE% --parallel
)

set BUILD_EXIT_CODE=!ERRORLEVEL!
echo Build exit code: !BUILD_EXIT_CODE!

if !BUILD_EXIT_CODE!==0 goto :build_success

echo.
echo ========================================
echo [ERROR] Build failed with exit code: !BUILD_EXIT_CODE!
echo ========================================
echo.
echo Please check the error messages above.
echo.
echo Common issues:
echo 1. Compilation errors in source code
echo 2. Missing SFML libraries (check SFML path)
echo 3. Incorrect compiler settings
echo 4. Missing dependencies
echo.
echo To see detailed errors, check the build output above.
echo.
cd ..
pause
exit /b !BUILD_EXIT_CODE!

:build_success

echo.
echo [Step 5.5] Verifying build output...
if exist "%BUILD_TYPE%\chessboard-coverage_gui.exe" (
    echo Build successful: Found %BUILD_TYPE%\chessboard-coverage_gui.exe
) else if exist "x64\%BUILD_TYPE%\chessboard-coverage_gui.exe" (
    echo Build successful: Found x64\%BUILD_TYPE%\chessboard-coverage_gui.exe
) else (
    echo [WARNING] Build completed but exe not found in expected locations
    echo Searching for exe...
    for /r %%F in (chessboard-coverage_gui.exe) do (
        if exist "%%F" (
            echo Found exe at: %%F
            goto :exe_verified
        )
    )
    echo [ERROR] Executable not found after build!
    echo Build may have failed silently.
    cd ..
    pause
    exit /b 1
    :exe_verified
)

echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.

echo.
echo [Step 6] Finding executable...
set EXE_FOUND=0
set EXE_PATH=

echo Searching for chessboard-coverage_gui.exe...
echo Current directory: %CD%

REM Try multiple possible locations for Visual Studio generator
if "%GENERATOR%"=="Ninja" (
    if exist "%BUILD_TYPE%\chessboard-coverage_gui.exe" (
        set "EXE_PATH=%CD%\%BUILD_TYPE%\chessboard-coverage_gui.exe"
        set EXE_FOUND=1
        echo Found: !EXE_PATH!
    ) else if exist "chessboard-coverage_gui.exe" (
        set "EXE_PATH=%CD%\chessboard-coverage_gui.exe"
        set EXE_FOUND=1
        echo Found: !EXE_PATH!
    )
) else (
    REM Visual Studio generator - check common locations
    echo Checking: %BUILD_TYPE%\chessboard-coverage_gui.exe
    if exist "%BUILD_TYPE%\chessboard-coverage_gui.exe" (
        set "EXE_PATH=%CD%\%BUILD_TYPE%\chessboard-coverage_gui.exe"
        set EXE_FOUND=1
        echo Found: !EXE_PATH!
    ) else (
        echo Checking: x64\%BUILD_TYPE%\chessboard-coverage_gui.exe
        if exist "x64\%BUILD_TYPE%\chessboard-coverage_gui.exe" (
            set "EXE_PATH=%CD%\x64\%BUILD_TYPE%\chessboard-coverage_gui.exe"
            set EXE_FOUND=1
            echo Found: !EXE_PATH!
        ) else (
            echo Checking: chessboard-coverage_gui.exe
            if exist "chessboard-coverage_gui.exe" (
                set "EXE_PATH=%CD%\chessboard-coverage_gui.exe"
                set EXE_FOUND=1
                echo Found: !EXE_PATH!
            ) else (
                echo Checking: ..\chessboard-coverage_gui.exe
                if exist "..\chessboard-coverage_gui.exe" (
                    set "EXE_PATH=%CD%\..\chessboard-coverage_gui.exe"
                    set EXE_FOUND=1
                    echo Found: !EXE_PATH!
                ) else (
                    REM Additional check: try to find exe in any subdirectory
                    echo Searching in subdirectories...
                    for /r %%F in (chessboard-coverage_gui.exe) do (
                        if exist "%%F" (
                            set "EXE_PATH=%%F"
                            set EXE_FOUND=1
                            echo Found: !EXE_PATH!
                            goto :exe_found
                        )
                    )
                    :exe_found
                )
            )
        )
    )
)

if "!EXE_FOUND!"=="1" (
    echo.
    echo ========================================
    echo Found executable: !EXE_PATH!
    echo ========================================
) else (
    echo.
    echo ========================================
    echo [WARNING] Executable not found!
    echo ========================================
    echo.
    echo Searched in:
    echo   - %BUILD_TYPE%\chessboard-coverage_gui.exe
    echo   - x64\%BUILD_TYPE%\chessboard-coverage_gui.exe
    echo   - chessboard-coverage_gui.exe
    echo   - ..\chessboard-coverage_gui.exe
    echo.
    echo Current directory: %CD%
    echo.
    echo Possible reasons:
    echo 1. Build failed - check error messages above
    echo 2. Build succeeded but exe in different location
    echo 3. Try building manually: cmake --build . --config %BUILD_TYPE%
    echo.
    echo To find the exe manually, search in:
    echo   - build_chessboard\%BUILD_TYPE%\
    echo   - build_chessboard\x64\%BUILD_TYPE%\
    echo.
)

echo.
echo [Step 7] Copying DLL files...
if exist "%SFML_PATH%\bin\sfml-graphics-2.dll" (
    REM Determine exe directory
    if "!EXE_FOUND!"=="1" (
        for %%F in ("!EXE_PATH!") do set "EXE_DIR=%%~dpF"
        echo Copying DLLs to: !EXE_DIR!
        copy /Y "%SFML_PATH%\bin\sfml-graphics-2.dll" "!EXE_DIR!" >nul 2>&1
        copy /Y "%SFML_PATH%\bin\sfml-window-2.dll" "!EXE_DIR!" >nul 2>&1
        copy /Y "%SFML_PATH%\bin\sfml-system-2.dll" "!EXE_DIR!" >nul 2>&1
        echo DLL files copied to exe directory.
    ) else (
        REM Try copying to common build output locations
        if exist "x64\%BUILD_TYPE%" (
            echo Copying DLLs to: x64\%BUILD_TYPE%\
            copy /Y "%SFML_PATH%\bin\sfml-graphics-2.dll" "x64\%BUILD_TYPE%\" >nul 2>&1
            copy /Y "%SFML_PATH%\bin\sfml-window-2.dll" "x64\%BUILD_TYPE%\" >nul 2>&1
            copy /Y "%SFML_PATH%\bin\sfml-system-2.dll" "x64\%BUILD_TYPE%\" >nul 2>&1
            echo DLL files copied to x64\%BUILD_TYPE%\.
        ) else if exist "%BUILD_TYPE%" (
            echo Copying DLLs to: %BUILD_TYPE%\
            copy /Y "%SFML_PATH%\bin\sfml-graphics-2.dll" "%BUILD_TYPE%\" >nul 2>&1
            copy /Y "%SFML_PATH%\bin\sfml-window-2.dll" "%BUILD_TYPE%\" >nul 2>&1
            copy /Y "%SFML_PATH%\bin\sfml-system-2.dll" "%BUILD_TYPE%\" >nul 2>&1
            echo DLL files copied to %BUILD_TYPE%\.
        ) else (
            echo [WARNING] Cannot determine exe location, please copy DLL files manually
            echo Copy the following files from %SFML_PATH%\bin to exe directory:
            echo   - sfml-graphics-2.dll
            echo   - sfml-window-2.dll
            echo   - sfml-system-2.dll
        )
    )
) else (
    echo [WARNING] SFML DLL files not found
    echo Please check %SFML_PATH%\bin directory
)

cd ..

REM Restore original CMakeLists.txt if it was backed up
if exist ..\CMakeLists_original_backup.txt (
    copy /Y ..\CMakeLists_original_backup.txt ..\CMakeLists.txt >nul 2>&1
    del ..\CMakeLists_original_backup.txt >nul 2>&1
)

echo.
echo [Step 8] Copying resources and config files...
REM Copy resources folder if it exists
if exist "resources" (
    if not exist "%BUILD_DIR%\%BUILD_TYPE%\resources" (
        echo Copying resources folder to build output...
        xcopy /E /I /Y "resources" "%BUILD_DIR%\%BUILD_TYPE%\resources\" >nul 2>&1
    )
    if not exist "%BUILD_DIR%\x64\%BUILD_TYPE%\resources" (
        xcopy /E /I /Y "resources" "%BUILD_DIR%\x64\%BUILD_TYPE%\resources\" >nul 2>&1
    )
)
REM Copy texture_config.txt if it exists
if exist "texture_config.txt" (
    if not exist "%BUILD_DIR%\%BUILD_TYPE%\texture_config.txt" (
        copy /Y "texture_config.txt" "%BUILD_DIR%\%BUILD_TYPE%\" >nul 2>&1
    )
    if not exist "%BUILD_DIR%\x64\%BUILD_TYPE%\texture_config.txt" (
        copy /Y "texture_config.txt" "%BUILD_DIR%\x64\%BUILD_TYPE%\" >nul 2>&1
    )
)

echo.
echo [Step 9] Copying executable to project root...
if "!EXE_FOUND!"=="1" (
    echo Copying !EXE_PATH! to project root...
    if exist "!EXE_PATH!" (
        copy /Y "!EXE_PATH!" "chessboard-coverage_gui.exe" >nul 2>&1
        if errorlevel 1 (
            echo [WARNING] Failed to copy exe to project root
            echo Error: %ERRORLEVEL%
            echo Source: !EXE_PATH!
            echo Destination: %CD%\chessboard-coverage_gui.exe
            echo You can still run it from: !EXE_PATH!
        ) else (
            if exist "chessboard-coverage_gui.exe" (
                echo Successfully copied chessboard-coverage_gui.exe to project root
            ) else (
                echo [WARNING] Copy command succeeded but exe not found in project root
            )
        )
        
        REM Also copy DLL files to project root if exe was copied
        if exist "chessboard-coverage_gui.exe" (
            echo Copying DLL files to project root...
            if exist "%SFML_PATH%\bin\sfml-graphics-2.dll" (
                copy /Y "%SFML_PATH%\bin\sfml-graphics-2.dll" "." >nul 2>&1
                copy /Y "%SFML_PATH%\bin\sfml-window-2.dll" "." >nul 2>&1
                copy /Y "%SFML_PATH%\bin\sfml-system-2.dll" "." >nul 2>&1
                echo DLL files copied to project root
            )
        )
    ) else (
        echo [ERROR] Exe file not found at: !EXE_PATH!
        echo Please check if the build completed successfully.
    )
) else (
    echo [WARNING] Executable not found, skipping copy to project root
    echo Please search for chessboard-coverage_gui.exe in %BUILD_DIR% directory
    echo.
    echo Trying to find exe manually...
    if exist "%BUILD_DIR%\%BUILD_TYPE%\chessboard-coverage_gui.exe" (
        echo Found exe at: %BUILD_DIR%\%BUILD_TYPE%\chessboard-coverage_gui.exe
        echo Copying to project root...
        copy /Y "%BUILD_DIR%\%BUILD_TYPE%\chessboard-coverage_gui.exe" "chessboard-coverage_gui.exe" >nul 2>&1
        if exist "chessboard-coverage_gui.exe" (
            echo Successfully copied chessboard-coverage_gui.exe to project root
            set EXE_FOUND=1
            set "EXE_PATH=%CD%\chessboard-coverage_gui.exe"
        )
    ) else if exist "%BUILD_DIR%\x64\%BUILD_TYPE%\chessboard-coverage_gui.exe" (
        echo Found exe at: %BUILD_DIR%\x64\%BUILD_TYPE%\chessboard-coverage_gui.exe
        echo Copying to project root...
        copy /Y "%BUILD_DIR%\x64\%BUILD_TYPE%\chessboard-coverage_gui.exe" "chessboard-coverage_gui.exe" >nul 2>&1
        if exist "chessboard-coverage_gui.exe" (
            echo Successfully copied chessboard-coverage_gui.exe to project root
            set EXE_FOUND=1
            set "EXE_PATH=%CD%\chessboard-coverage_gui.exe"
        )
    )
)

echo.
echo ========================================
echo Build Complete!
echo ========================================
echo.
if "!EXE_FOUND!"=="1" (
    echo Executable location: !EXE_PATH!
    if exist "chessboard-coverage_gui.exe" (
        echo.
        echo Executable also copied to project root: chessboard-coverage_gui.exe
        echo You can run it directly from the project root directory.
    )
    echo.
    echo To run the program:
    if exist "chessboard-coverage_gui.exe" (
        echo   chessboard-coverage_gui.exe
        echo   or
    )
    echo   "!EXE_PATH!"
) else (
    echo Please search for chessboard-coverage_gui.exe in %BUILD_DIR% directory
)
echo.
echo Note: The program will automatically create default texture files
echo       in the resources/ folder on first run.
echo.
echo Build summary:
echo   - Build directory: %BUILD_DIR%
echo   - Build type: %BUILD_TYPE%
echo   - Generator: %GENERATOR%
if "!EXE_FOUND!"=="1" (
    echo   - Executable: !EXE_PATH!
    if exist "chessboard-coverage_gui.exe" (
        echo   - Also copied to: %CD%\chessboard-coverage_gui.exe
    )
)
echo.
pause

