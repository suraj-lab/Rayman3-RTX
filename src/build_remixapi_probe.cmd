@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat" >nul
cl /nologo /MT /O2 /EHsc /W3 /DREMIX_ALLOW_X86 remixapi_probe.cpp /link /MACHINE:X86 /OUT:remixapi_probe.exe
echo CL_EXIT=%ERRORLEVEL%
