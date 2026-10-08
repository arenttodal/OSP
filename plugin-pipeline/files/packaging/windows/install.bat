@echo off
rem Installs every plug-in next to this file. VST3 and CLAP live in Program Files, so this
rem asks for administrator rights once.
net session >nul 2>&1
if errorlevel 1 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
cd /d "%~dp0"
set "VST3=%CommonProgramFiles%\VST3"
set "CLAP=%CommonProgramFiles%\CLAP"
if not exist "%VST3%" mkdir "%VST3%"
for /d %%B in (*.vst3) do (
    if exist "%VST3%\%%B" rmdir /s /q "%VST3%\%%B"
    xcopy /e /i /q /y "%%B" "%VST3%\%%B\" >nul
    echo Installed %%B  -^>  %VST3%
)
for %%B in (*.clap) do (
    if not exist "%CLAP%" mkdir "%CLAP%"
    copy /y "%%B" "%CLAP%\" >nul
    echo Installed %%B  -^>  %CLAP%
)
echo.
echo Restart your DAW and rescan plug-ins. The standalone .exe runs from this folder.
pause
