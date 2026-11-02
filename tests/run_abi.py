"""Build and check D3D8 translation/proxy paths without launching Rayman.
Run: python tests/run_abi.py
Each run gets a fresh directory; game files and earlier evidence stay untouched.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
VCVARS = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsarm64_x86.bat")
GAME = Path(r"C:\Program Files\GOG Galaxy\Games\Rayman 3")


def game_hashes():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in GAME.glob("*.dll")}


def main():
    before = game_hashes()
    run = Path(tempfile.mkdtemp(prefix="abi_", dir=ROOT / "tests"))
    src = ROOT / "src"
    build = run / "build.cmd"
    build.write_text(
        '@echo off\n'
        f'call "{VCVARS}" >nul\n'
        'if errorlevel 1 exit /b 1\n'
        f'cl /nologo /MT /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS "{src / "d3d8abi.cpp"}" '
        '/link /MACHINE:X86 user32.lib /OUT:d3d8abi.exe\n'
        'if errorlevel 1 exit /b 1\n'
        f'cl /nologo /LD /MT /O2 /EHsc /D_CRT_SECURE_NO_WARNINGS "{src / "d3d8proxy.cpp"}" '
        f'/link /MACHINE:X86 user32.lib /DEF:"{src / "d3d8proxy.def"}" /OUT:d3d8proxy.dll\n'
        'exit /b %ERRORLEVEL%\n', encoding="utf8")
    result = subprocess.run(["cmd", "/d", "/c", "build.cmd"], cwd=run,
                            capture_output=True, text=True, timeout=120)
    (run / "build.log").write_text(result.stdout + result.stderr, encoding="utf8")
    print("Evidence:", run)
    if result.returncode:
        print(result.stdout[-5000:], result.stderr[-1000:])
        raise RuntimeError("Build failed; no tests run")
    print("Build: PASS")
    backends = {
        "dgvoodoo": ROOT / "abitest" / "d3d8_real.dll",
        "remix_bundled_d3d8to9": ROOT / "ex" / "remix_x86" / "d3d8to9.dll",
        "crosire_d3d8to9": ROOT / "dl" / "d3d8to9.dll",
        "br3_dxwrapper": ROOT / "ex" / "better_rayman3_setup" / "app" / "d3d8.dll",
    }
    results = []
    for name, backend in backends.items():
        for proxy in (False, True):
            label = name + ("_proxy" if proxy else "_direct")
            case = run / label
            case.mkdir()
            shutil.copy2(run / "d3d8abi.exe", case)
            shutil.copy2(backend, case / ("d3d8_real.dll" if proxy else "d3d8.dll"))
            if proxy:
                shutil.copy2(run / "d3d8proxy.dll", case / "d3d8.dll")
            if name == "dgvoodoo":
                shutil.copy2(ROOT / "abitest" / "dgVoodoo.conf", case)
            if name == "br3_dxwrapper":
                # Omit LoadCustomDllPath: BetterRayman3 patches must not touch this test host.
                config = "[General]\n[Compatibility]\nD3d8to9 = 1\n"
                (case / "d3d8.ini").write_text(config)
                (case / "d3d8_real.ini").write_text(config)
            completed = subprocess.run([str(case / "d3d8abi.exe"), ".\\d3d8.dll"],
                                       cwd=case, capture_output=True, timeout=60)
            log = (case / "abi.log").read_text(errors="replace")
            hooklog = (case / "r3probe.log").read_text(errors="replace") if proxy else ""
            passed = completed.returncode == 0 and all(s in log for s in (
                "PASS: device creation and triangle submission",
                "Exception forwarding: caught=1 noncontinuable=1 continuable=1 continued=1 cpp=1"))
            if proxy:
                passed = passed and all(s in hooklog for s in (
                    "patched CreateDevice slot 15", "CreateDevice -> hr=0x00000000",
                    "device vtable hooked", "SetTransform VIEW", "FVF 0x00000044",
                    "DrawPrimitiveUP=1", "HostTrace: host RaiseException import hooked",
                    "EXCEPTION code=0xE0420001 flags=0x00000001 count=2",
                    "EXCEPTION code=0xE0420002 flags=0x00000000 count=2",
                    "EXCEPTION code=0xE06D7363",
                    "exception arg[0]=0x13572468", "exception arg[1]=0x24681357"))
                synthetic = [line for line in hooklog.splitlines() if "EXCEPTION code=0xE042000" in line]
                passed = passed and len(synthetic) == 2 and all(
                    "d3d8abi.exe offset=0x" in line and "caller=00000000" not in line
                    for line in synthetic)
            row = {"case": label, "exit": completed.returncode, "passed": passed}
            results.append(row)
            print(label + ":", "PASS" if passed else "FAIL", "exit=" + str(completed.returncode))
            if not passed:
                print(log[-1800:])
                print(hooklog[-1000:])
            else:
                exit_case = run / (label + "_exit")
                exit_case.mkdir()
                for source in case.iterdir():
                    if source.is_file() and source.suffix.lower() in (".exe", ".dll", ".ini", ".conf"):
                        shutil.copy2(source, exit_case / source.name)
                exited = subprocess.run([str(exit_case / "d3d8abi.exe"), ".\\d3d8.dll", "--exit-process"],
                                        cwd=exit_case, capture_output=True, timeout=60)
                exit_log = (exit_case / "abi.log").read_text(errors="replace")
                exit_passed = exited.returncode == 73 and "PASS: forwarding ExitProcess(73)" in exit_log
                if proxy:
                    trace = (exit_case / "r3probe.log").read_text(errors="replace")
                    exit_passed = exit_passed and all(s in trace for s in (
                        "HostTrace: host ExitProcess import hooked", "EXITPROCESS code=73", "exit frame["))
                    exit_passed = exit_passed and any("exit frame[" in line and "d3d8abi.exe offset=0x" in line
                                                    for line in trace.splitlines())
                results.append({"case": label + "_exit", "exit": exited.returncode, "passed": exit_passed})
                print(label + "_exit:", "PASS" if exit_passed else "FAIL", "exit=" + str(exited.returncode))
    negative = run / "negative_missing_dll"
    negative.mkdir()
    shutil.copy2(run / "d3d8abi.exe", negative)
    completed = subprocess.run([str(negative / "d3d8abi.exe"), ".\\missing.dll"],
                               cwd=negative, capture_output=True, timeout=30)
    assert completed.returncode == 2, "Harness failed to report missing DLL"
    assert game_hashes() == before, "Installed game DLLs changed during test"
    print("Missing-DLL negative check: PASS; installed game DLLs unchanged")
    (run / "results.json").write_text(json.dumps(results, indent=2), encoding="utf8")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
