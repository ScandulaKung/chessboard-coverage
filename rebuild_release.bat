@echo off
setlocal
cd /d "%~dp0"

set EXE=build\Release\ColorBottleGame.exe

echo ========================================
echo Force rebuild Release ColorBottleGame
echo ========================================
echo.

REM 结束可能残留的进程（任务管理器里不一定显眼）
taskkill /F /IM ColorBottleGame.exe >nul 2>&1

if exist "%EXE%" (
  echo Deleting old exe: %EXE%
  del /F /Q "%EXE%"
  if exist "%EXE%" (
    echo.
    echo [FAIL] Cannot delete exe - still locked by another process.
    echo Check: Task Manager, antivirus, Explorer preview, another terminal.
    echo Full path: %CD%\%EXE%
    pause
    exit /b 1
  )
  echo Deleted OK.
) else (
  echo Old exe not present ^(will be created^).
)

echo.
echo Building Release...
if not exist build (
  echo build\ missing. Run build.bat once to configure CMake first.
  pause
  exit /b 1
)

pushd build
cmake --build . --config Release
set ERR=%ERRORLEVEL%
popd

if %ERR% NEQ 0 (
  echo.
  echo [FAIL] Build failed, exit code %ERR%
  pause
  exit /b %ERR%
)

if not exist "%EXE%" (
  echo.
  echo [FAIL] Build reported OK but exe missing:
  echo   %CD%\%EXE%
  echo Also check: build\Debug\ColorBottleGame.exe
  pause
  exit /b 1
)

echo.
echo [OK] Updated:
for %%F in ("%EXE%") do echo   %%~fF
for %%F in ("%EXE%") do echo   Size=%%~zF  Time=%%~tF
echo.
echo NOTE: Debug build is a different file:
echo   build\Debug\ColorBottleGame.exe
echo Do not run old-qt\Release\ColorBottleGame.exe
echo.
pause
