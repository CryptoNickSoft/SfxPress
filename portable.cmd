@echo off
setlocal
set ROOT=%~dp0
set DIST=%ROOT%dist\SfxPress

if not exist "%ROOT%build\SfxPress.exe" (
    echo Build first: build.cmd
    exit /b 1
)

if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%" 2>nul

copy /y "%ROOT%build\SfxPress.exe" "%DIST%\" >nul
xcopy /y /i /q /e "%ROOT%build\Bin" "%DIST%\Bin\" >nul
xcopy /y /i /q /e "%ROOT%build\Example" "%DIST%\Example\" >nul

echo Portable set: %DIST%
echo   SfxPress.exe
echo   Bin\7z.exe
for %%f in ("%DIST%\Bin\Modules\*.sfx") do echo   Bin\Modules\%%~nxf
exit /b 0
