@echo off
rem Build DAO on Windows.
rem   MinGW-w64 / MSYS2 : uses make if available, otherwise gcc directly
rem   Visual Studio     : run from a "Developer Command Prompt"
setlocal
cd /d "%~dp0"
if not exist build mkdir build

where make >nul 2>nul && where gcc >nul 2>nul
if %errorlevel%==0 (
    make
    goto :done
)

where gcc >nul 2>nul
if %errorlevel%==0 (
    set "CF=-O2 -municode"
    windres res\dao.rc -O coff -o build\dao_res.o || goto :fail
    windres res\host.rc -O coff -o build\host_res.o || goto :fail
    gcc %CF% -mwindows -o build\dao.exe src\main.c src\engine.c src\i18n.c build\dao_res.o -lwinhttp -lcomctl32 -lshell32 -lole32 -luxtheme -ldwmapi -lgdi32 -luser32 -luuid -ladvapi32 || goto :fail
    gcc %CF% -mwindows -o build\dao-host.exe src\host.c build\host_res.o || goto :fail
    gcc %CF% -o build\dao-cli.exe src\cli.c -lwinhttp || goto :fail
    windres installer\setup.rc -O coff -o build\setup_res.o || goto :fail
    gcc %CF% -mwindows -o build\DAO-Setup.exe installer\setup.c build\setup_res.o -lole32 -luuid -lshell32 -ldwmapi -lgdi32 -luser32 -ladvapi32 || goto :fail
    goto :done
)

where cl >nul 2>nul
if %errorlevel%==0 (
    set "CF=/nologo /O2 /utf-8 /W3 /DUNICODE /D_UNICODE"
    rc /nologo /fo build\dao.res res\dao.rc || goto :fail
    rc /nologo /fo build\host.res res\host.rc || goto :fail
    cl %CF% /Fobuild\ /Fe:build\dao.exe src\main.c src\engine.c src\i18n.c build\dao.res winhttp.lib comctl32.lib shell32.lib ole32.lib uxtheme.lib dwmapi.lib gdi32.lib user32.lib advapi32.lib /link /SUBSYSTEM:WINDOWS || goto :fail
    cl %CF% /Fobuild\ /Fe:build\dao-host.exe src\host.c build\host.res user32.lib /link /SUBSYSTEM:WINDOWS || goto :fail
    cl %CF% /Fobuild\ /Fe:build\dao-cli.exe src\cli.c winhttp.lib || goto :fail
    rc /nologo /fo build\setup.res installer\setup.rc || goto :fail
    cl %CF% /Fobuild\ /Fe:build\DAO-Setup.exe installer\setup.c build\setup.res ole32.lib uuid.lib shell32.lib dwmapi.lib gdi32.lib user32.lib advapi32.lib /link /SUBSYSTEM:WINDOWS || goto :fail
    goto :done
)

echo No compiler found. Install MSYS2 (https://www.msys2.org) and run:
echo     pacman -S mingw-w64-ucrt-x86_64-gcc make
echo or open a Visual Studio "Developer Command Prompt".
exit /b 1

:fail
echo Build failed.
exit /b 1

:done
echo.
echo Done. Output is in the build folder.
endlocal
