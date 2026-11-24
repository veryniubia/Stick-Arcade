@echo off
pushd "%~dp0.."
if exist ".venv\Scripts\python.exe" (
    ".venv\Scripts\python.exe" simulator\run.py %*
) else (
    python simulator\run.py %*
)
if errorlevel 1 pause
popd
