"""Preserve a live Rayman runtime-error dialog as a local minidump, without terminating it."""
from pathlib import Path
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
GAME = Path(r"C:\Program Files\GOG Galaxy\Games\Rayman 3")


def main():
    p = subprocess.run(["powershell", "-NoProfile", "-Command",
        "Get-Process Rayman3 -ErrorAction SilentlyContinue | Select-Object Id,Path | ConvertTo-Json -Compress"],
        capture_output=True, text=True, check=True)
    process = json.loads(p.stdout)
    assert isinstance(process, dict), "Expected exactly one Rayman process"
    assert Path(process["Path"]).resolve() == (GAME / "Rayman3.exe").resolve(), "Unexpected process path"
    pid = process["Id"]
    out = Path(tempfile.mkdtemp(prefix="runtime_error_", dir=ROOT / "tests"))
    for rel in ["r3probe.log", "remix_launch.log", "rtx-remix/logs/bridge32.log",
                "rtx-remix/logs/bridge64.log", "rtx-remix/logs/remix-dxvk.log"]:
        source = GAME / rel
        if source.is_file():
            target = out / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    manifest = {"pid": pid, "executable": process["Path"], "modules": {
        name: hashlib.sha256((GAME / name).read_bytes()).hexdigest()
        for name in ["Rayman3.exe", "D3D8.dll", "d3d8_real.dll", "d3d9.dll"]}}
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    dbg = ctypes.WinDLL("dbghelp", use_last_error=True)
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.CreateFileW.argtypes = [w.LPCWSTR, w.DWORD, w.DWORD, w.LPVOID, w.DWORD, w.DWORD, w.HANDLE]
    kernel.CreateFileW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    dbg.MiniDumpWriteDump.argtypes = [w.HANDLE, w.DWORD, w.HANDLE, w.DWORD, w.LPVOID, w.LPVOID, w.LPVOID]
    dbg.MiniDumpWriteDump.restype = w.BOOL
    handle = kernel.OpenProcess(0x0400 | 0x0010 | 0x0040, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    file_handle = None
    try:
        file_handle = kernel.CreateFileW(str(out / "Rayman3.dmp"), 0x40000000, 0, None, 1, 0x80, None)
        if file_handle == ctypes.c_void_p(-1).value:
            file_handle = None
            raise ctypes.WinError(ctypes.get_last_error())
        # Normal dump plus thread information: stacks/modules, not a full-memory dump.
        ok = dbg.MiniDumpWriteDump(handle, pid, file_handle, 0x1000, None, None, None)
        error = ctypes.get_last_error() if not ok else 0
        manifest.update(dump_success=bool(ok), win32_error=error)
    finally:
        if file_handle is not None:
            kernel.CloseHandle(file_handle)
        kernel.CloseHandle(handle)
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf8")
    print("Evidence:", out)
    print("MiniDumpWriteDump:", bool(ok), "error:", error)
    print("Dump bytes:", (out / "Rayman3.dmp").stat().st_size)
    print("Rayman was not terminated or relaunched; dialog was not dismissed.")
    if not ok:
        raise ctypes.WinError(error)


if __name__ == "__main__":
    main()
