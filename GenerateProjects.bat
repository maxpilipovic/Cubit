@echo off

echo Cleaning old project files...

rmdir /s /q bin 2>nul
rmdir /s /q bin-int 2>nul

del *.sln 2>nul

echo.
echo Generating Visual Studio solution...

premake5 vs2026

if %ERRORLEVEL% neq 0 (
    echo Premake failed.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo Done! Open Cubit.sln
pause