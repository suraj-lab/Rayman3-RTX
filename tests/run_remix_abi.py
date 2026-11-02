"""Exercise the passing D3D8 proxy path against a private Remix runtime copy.
No game executable or installed game files are modified or launched.

`python tests/run_remix_abi.py --lifecycle` instead drives the shim's window-message
path (WM_ACTIVATEAPP/WM_SIZE/WM_DESTROY). Add `--fullscreen` to match Rayman's
fullscreen desktop-size device. That run collects evidence and may legitimately
reproduce the abort; it is not a pass/fail gate.
"""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r"C:\Program Files\GOG Galaxy\Games\Rayman 3")


def verify(case, launcher_exit):
    abi = (case / "abi.log").read_text(errors="replace")
    hooks = (case / "r3probe.log").read_text(errors="replace")
    assert launcher_exit == 0, "Launcher failed"
    assert "PASS: device creation and triangle submission" in abi, "Harness did not finish successfully"
    assert "Present -> 0x00000000" in abi, "Present failed"
    assert "device vtable hooked" in hooks and "DrawPrimitiveUP=1" in hooks, "Proxy hooks did not observe the draw"
    assert "Exception forwarding: caught=1 noncontinuable=1 continuable=1 continued=1 cpp=1" in abi, "Exception semantics changed"
    assert all(s in hooks for s in ("HostTrace: host RaiseException import hooked",
        "EXCEPTION code=0xE0420001 flags=0x00000001 count=2",
        "EXCEPTION code=0xE0420002 flags=0x00000000 count=2",
        "EXCEPTION code=0xE06D7363")), "Exception trace missing"
    assert "HostTrace: host ExitProcess import hooked" in hooks and "EXITPROCESS code=0" in hooks, "Normal-exit trace missing"
    assert any("exit frame[" in line and "d3d8abi.exe offset=0x" in line for line in hooks.splitlines()), "Host exit stack frame missing"
    assert "Vulkan: 1.4." in (case / "launcher.log").read_text(errors="replace"), "Vulkan renderer startup unconfirmed"
    print("PASS: full-chain device/draw/Present and hook evidence verified")
    return 0


def verify_lifecycle(case, launcher_exit):
    abi = (case / "abi.log").read_text(errors="replace")
    hooks = (case / "r3probe.log").read_text(errors="replace")
    assert "LIFECYCLE: begin" in abi, "Lifecycle probe did not run; see logs above"
    print("Harness exit:", launcher_exit)
    for line in abi.splitlines():
        if line.startswith("lifecycle "):
            print(" ", line)
    verdict = [l for l in abi.splitlines() if l.startswith("LIFECYCLE: ")]
    print("Verdict:", verdict[-1] if verdict else "no verdict line (process died during the probe)")
    exits = [l for l in hooks.splitlines() if "EXITPROCESS" in l or "UNHANDLED" in l]
    print("Exit trace:", exits or "none recorded")
    throws = [l for l in hooks.splitlines() if "EXCEPTION code=0xE06D7363" in l]
    print("Shim C++ throws:", throws or "none recorded")
    print("Evidence dir:", case)
    return 0


def main():
    lifecycle = "--lifecycle" in sys.argv
    builds = [p for p in (ROOT / "tests").glob("abi_*") if (p / "results.json").is_file()]
    build = max(builds, key=lambda p: p.stat().st_mtime)
    rows = json.loads((build / "results.json").read_text())
    assert any(row["case"] == "remix_bundled_d3d8to9_proxy" and row["passed"] for row in rows), "Source build failed its proxy gate"
    before = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in GAME.glob("*.dll")}
    stage = ROOT / "ex" / "remix_x86"
    case = Path(tempfile.mkdtemp(prefix="remix_abi_", dir=ROOT / "tests"))
    for directory in (".trex", ".trex_arm64"):
        shutil.copytree(stage / directory, case / directory)
    for name in ("d3d9.dll", "NvRemixLauncher32.exe"):
        shutil.copy2(stage / name, case / name)
    shutil.copy2(build / "d3d8abi.exe", case)
    shutil.copy2(build / "d3d8proxy.dll", case / "d3d8.dll")
    shutil.copy2(stage / "d3d8to9.dll", case / "d3d8_real.dll")
    print("Evidence:", case, flush=True)
    with (case / "launcher.log").open("w", encoding="utf8") as out:
        # Popen so a hung run can be killed with its children; a leaked harness
        # would keep burning CPU on the user's machine.
        launcher = subprocess.Popen([str(case / "NvRemixLauncher32.exe"), "-w", str(case),
                                     str(case / "d3d8abi.exe"), ".\\d3d8.dll"]
                                    + [a for a in sys.argv[1:] if a.startswith("--")], cwd=case,
                                    stdout=out, stderr=subprocess.STDOUT)
        try:
            launcher.wait(timeout=120)
        except subprocess.TimeoutExpired:
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(launcher.pid)], capture_output=True)
            raise RuntimeError(f"Run hung for 120s and was killed. Evidence: {case}")
        completed = launcher
    print("Launcher exit:", completed.returncode)
    for name in ("abi.log", "r3probe.log"):
        p = case / name
        print(name + ":\n" + (p.read_text(errors="replace")[-5000:] if p.exists() else "MISSING"))
    lines = (case / "launcher.log").read_text(errors="replace").splitlines()
    selected = [l for l in lines if any(s in l.lower() for s in
                ("error", "warn", "camera", "vulkan", "buffer size", "device:", "exception"))]
    print("Runtime diagnostics:\n" + "\n".join(selected[-25:]))
    assert before == {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in GAME.glob("*.dll")}
    print("Installed game DLL hashes unchanged")
    if lifecycle:
        return verify_lifecycle(case, completed.returncode)
    return verify(case, completed.returncode)


if __name__ == "__main__":
    raise SystemExit(main())
