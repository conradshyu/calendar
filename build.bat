@echo off
rem Builds WinCalendar from the command line (Visual Studio 2026 / 18 or 2022 / 17).
setlocal
set CONFIG=%1
if "%CONFIG%"=="" set CONFIG=Release
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set MSBUILD=
if exist %VSWHERE% for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -prerelease -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD="%%i"
if not defined MSBUILD set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
%MSBUILD% "%~dp0Calendar.sln" /m /nologo /v:minimal /p:Configuration=%CONFIG% /p:Platform=x64
if errorlevel 1 (echo Build failed. & exit /b 1)
echo.
echo Built: %~dp0bin\%CONFIG%\WinCalendar.exe
