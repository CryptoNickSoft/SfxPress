@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0src" || exit /b 1
rc /nologo /fo "..\build\app.res" app.rc || exit /b 1
cd /d "%~dp0build" || exit /b 1
cl /nologo /W3 /O2 /MT /DUNICODE /D_UNICODE /EHsc /utf-8 "..\src\main.cpp" "app.res" /link /SUBSYSTEM:WINDOWS /OUT:SfxPress.exe /MANIFEST:EMBED /MANIFESTINPUT:"..\app.manifest" user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib
if errorlevel 1 exit /b 1
echo BUILT: %~dp0build\SfxPress.exe
