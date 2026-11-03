@echo off
rem Build the MIT-licensed ViGEmClient (upstream nefarius/ViGEmClient v1.16.18.0) from the pinned
rem source snapshot in analysis/vigem-client-src, so we ship no third-party binary we cannot audit.
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_amd64.bat" >nul
cd /d "%~dp0"
cl /nologo /LD /MT /O2 /EHsc /DUNICODE=0 /DWIN32_LEAN_AND_MEAN /DVIGEM_EXPORTS ^
   /I ..\analysis\vigem-client-src\include ^
   ..\analysis\vigem-client-src\src\ViGEmClient.cpp ^
   /link /OUT:ViGEmClient.dll /MACHINE:X64 /DEF:ViGEmClient.def setupapi.lib
echo CL_EXIT=%ERRORLEVEL%
