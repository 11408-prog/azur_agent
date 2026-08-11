@echo off
setlocal
rem ============================================================
rem  Azur Agent unified test runner: runs C++ + Python tests.
rem
rem  Usage:  tools\run_tests.bat [build-dir]
rem    Defaults to build/ (configured with AZUR_BUILD_TESTS=ON).
rem    VSCode preset users can pass build-vscode after enabling tests.
rem
rem  Equivalent low-level command:
rem    ctest --test-dir build --output-on-failure
rem  NOTE: this .bat must stay ASCII-only; Chinese text breaks
rem  cmd.exe parsing on GBK codepage systems.
rem ============================================================

set "BUILD_DIR=%~1"
if "%BUILD_DIR%"=="" set "BUILD_DIR=build"

rem azur_agent_tests.exe needs Qt6 DLLs at runtime; put Qt/MinGW on PATH.
set "PATH=C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;%PATH%"

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo [ERROR] CMakeCache.txt not found in "%BUILD_DIR%".
    echo         Configure first: cmake -B %BUILD_DIR% -G "MinGW Makefiles"
    echo           -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DAZUR_BUILD_TESTS=ON
    exit /b 1
)

echo [TEST] build dir: %BUILD_DIR%
echo [TEST] running ctest (C++ unit tests + Python unit tests)...
"C:\Qt\Tools\CMake_64\bin\ctest.exe" --test-dir "%BUILD_DIR%" --output-on-failure
set "RESULT=%ERRORLEVEL%"

if %RESULT%==0 (
    echo [TEST] all passed.
) else (
    echo [TEST] failures found, exit code=%RESULT%.
)
exit /b %RESULT%
