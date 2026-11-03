@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_amd64.bat" >nul
dumpbin /exports ViGEmClient.dll
