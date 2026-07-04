@echo off
setlocal EnableDelayedExpansion

echo buildeeer

:: verifica vsstudio
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

if not exist "%VSWHERE%" (
    echo Visual Studio nao encontrado.
    echo Instale o Visual Studio 2019 ou 2022 com o workload "Desktop development with C++"
    echo Download: https://visualstudio.microsoft.com/downloads/
    pause
    exit /b 1
)

:: localiza
for /f "usebackq tokens=*" %%i in (
    `"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`
) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo Nenhuma instalacao do Visual Studio com C++ encontrada.
    pause
    exit /b 1
)

echo Visual Studio encontrado em: %VS_PATH%

:: Initialize MSVC environment (x64)
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo Falha ao inicializar ambiente MSVC.
    pause
    exit /b 1
)

echo Ambiente MSVC x64 inicializado.
echo.

:: diretorio
if not exist "build" mkdir build
cd build

:: compila
echo Compilando recursos...
:: Adicionando as pastas src e resources nos Includes (/I) para o rc.exe achar o resource.h
rc.exe /nologo /I "..\src" /I "..\resources" /fo app.res "..\resources\app.rc"
if errorlevel 1 (
    echo Compliacao de recursos falhou, continuando sem...
    set "RES_FILE="
) else (
    echo Recursos compilados.
    set "RES_FILE=app.res"
)

:: compila 2
echo Compilando C++...
:: Adicionando as pastas src e resources nos Includes (/I) para o cl.exe achar o resource.h
cl.exe /nologo /std:c++17 /utf-8 /O2 /GL /W3 /EHsc ^
    /I "..\src" /I "..\resources" ^
    /D "UNICODE" /D "_UNICODE" /D "WIN32" /D "NDEBUG" ^
    /Fe"BVYT.exe" ^
    "..\src\main.cpp" ^
    %RES_FILE% ^
    /link /SUBSYSTEM:WINDOWS /LTCG ^
    comctl32.lib wininet.lib shell32.lib shlwapi.lib user32.lib gdi32.lib ^
    /MANIFESTFILE:"..\resources\app.manifest" ^
    /MANIFEST:EMBED ^
    >compile_log.txt 2>&1

if errorlevel 1 (
    echo Falha na compilacao. Veja build\compile_log.txt para detalhes.
    type compile_log.txt
    cd ..
    pause
    exit /b 1
)

echo Compilacao concluida.

:: finalizando
cd ..
echo.
echo ============================================
echo   Build concluido com sucesso!
echo   Executavel: build\BVYT.exe
echo ============================================
echo.

echo O app baixara o yt-dlp automaticamente
echo na primeira execucao (necessita internet).
echo.

pause
endlocal
