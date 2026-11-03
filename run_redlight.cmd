@echo off
rem Red-light experiment launcher. usage: run_redlight.cmd A|B [power] [radius] [fwd]
rem Prereq: new proxy deployed (see analysis/redlight-plan.md). Do not run screen capture.
set R3_REDLIGHT=%1
if not "%2"=="" set R3_REDPOWER=%2
if not "%3"=="" set R3_REDRADIUS=%3
if not "%4"=="" set R3_REDFWD=%4
cd /d "C:\Program Files\GOG Galaxy\Games\Rayman 3"
NvRemixLauncher32.exe -w "C:\Program Files\GOG Galaxy\Games\Rayman 3" "C:\Program Files\GOG Galaxy\Games\Rayman 3\Rayman3.exe" > remix_launch.log 2>&1
