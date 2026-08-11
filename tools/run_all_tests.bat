@echo off
setlocal
rem ============================================================
rem  Azur Agent one-click test runner (thin wrapper).
rem
rem  Delegates to the pure-Python runner tools\run_all_tests.py,
rem  which always uses the project's azur_agent virtual env.
rem  Double-clicking this .bat is equivalent to:
rem      azur_agent\Scripts\python.exe tools\run_all_tests.py
rem
rem  Usage:  tools\run_all_tests.bat [--build-dir DIR] [...]
rem  Exit code = number of failed steps (0 = all passed).
rem
rem  When double-clicked the console stays open so you can read
rem  the results; when run from an existing terminal / CI it does
rem  not pause (and still exits with the real exit code).
rem
rem  NOTE: this .bat must stay ASCII-only; Chinese text breaks
rem  cmd.exe parsing on GBK codepage systems.
rem ============================================================

set "ROOT=%~dp0.."
set "PY=%~dp0run_all_tests.py"

if not exist "%ROOT%\azur_agent\Scripts\python.exe" (
    echo [ERROR] virtual env interpreter not found.
    echo         create it first: python -m venv azur_agent
    pause
    exit /b 1
)

"%ROOT%\azur_agent\Scripts\python.exe" "%PY%" %*
set "RESULT=%ERRORLEVEL%"

rem Only pause when this .bat was double-clicked (a new console for us).
echo %cmdcmdline% | findstr /i "%~f0" >nul
if not errorlevel 1 pause

exit /b %RESULT%
