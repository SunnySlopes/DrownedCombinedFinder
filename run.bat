@echo off
setlocal
cd /d "%~dp0"
if not exist "build\river-finder.exe" (
    echo Build first: pwsh -File build.ps1
    exit /b 1
)
if exist "finder.json" (
    "build\river-finder.exe" --config finder.json %*
) else (
    "build\river-finder.exe" %*
)
exit /b %errorlevel%
