@echo off
setlocal
set "VSDIR=C:\BuildTools"
if not exist "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" set "VSDIR=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
call "%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul || exit /b 1
cd /d "%~dp0src" || exit /b 1
rc /nologo /fo "..\build\app.res" app.rc || exit /b 1
cd /d "%~dp0build" || exit /b 1
cl /nologo /W3 /O2 /MT /DUNICODE /D_UNICODE /EHsc /utf-8 "..\src\main.cpp" "app.res" /link /SUBSYSTEM:WINDOWS /OUT:SfxPress.exe /MANIFEST:EMBED /MANIFESTINPUT:"..\app.manifest" user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib ole32.lib advapi32.lib version.lib
if errorlevel 1 exit /b 1
echo BUILT: %~dp0build\SfxPress.exe

rem --- Необязательная подпись: задайте SIGN_PFX (путь к .pfx) и, при нужде, SIGN_PASSWORD ---
if not defined SIGN_PFX goto :nosign
set "SIGNTOOL=signtool.exe"
where signtool >nul 2>nul || set "SIGNTOOL=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe"
if not exist "%SIGNTOOL%" (
    echo SIGN: signtool.exe не найден — подпись пропущена
    goto :nosign
)
if defined SIGN_PASSWORD (
    "%SIGNTOOL%" sign /fd sha256 /f "%SIGN_PFX%" /p "%SIGN_PASSWORD%" /tr http://timestamp.digicert.com /td sha256 "%~dp0build\SfxPress.exe"
) else (
    "%SIGNTOOL%" sign /fd sha256 /f "%SIGN_PFX%" /tr http://timestamp.digicert.com /td sha256 "%~dp0build\SfxPress.exe"
)
if errorlevel 1 (
    echo SIGN: ошибка подписи
    exit /b 1
)
echo SIGNED: %~dp0build\SfxPress.exe
:nosign
