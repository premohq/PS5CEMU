@echo off
rem Windows launcher for ps5mfr, which is a Python script.
rem Usage: ps5mfr.bat <path to decrypted game dump>
rem Or drag and drop the game dump folder onto this file.
setlocal DisableDelayedExpansion

rem Prefer the py launcher that comes with the python.org installer. Probing the version also skips the
rem Microsoft Store "python" alias, which only exists to prompt you to install Python.
set "PYTHON="
py -3 -c "import sys; sys.exit(sys.version_info < (3, 6))" >nul 2>&1
if not errorlevel 1 set "PYTHON=py -3"
if not defined PYTHON (
    python -c "import sys; sys.exit(sys.version_info < (3, 6))" >nul 2>&1
    if not errorlevel 1 set "PYTHON=python"
)
if not defined PYTHON (
    echo Error: Python 3.6 or newer is required, get it from https://www.python.org/downloads/
    set "rc=1"
    goto end
)

%PYTHON% "%~dp0ps5mfr" %*
set "rc=%errorlevel%"

:end
rem Started from Explorer (double click, or a folder dropped onto this file) the window closes as soon as
rem we exit, so wait for a key press to keep the output readable. Not needed when run from a terminal.
setlocal EnableDelayedExpansion
if /i not "!cmdcmdline:/c =!"=="!cmdcmdline!" pause
exit /b %rc%
