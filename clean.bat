@echo off
echo Cleaning build directory...
if exist build (
    rmdir /s /q build
    echo Build directory removed.
) else (
    echo Build directory does not exist.
)
echo.
pause

