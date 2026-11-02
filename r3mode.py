"""Switch Rayman 3 between the normal Ray3Fix stack and the RTX Remix experiment.

usage: r3mode.py normal|remix|status
"""
import hashlib
import os
import shutil
import sys

G = r"C:\Program Files\GOG Galaxy\Games\Rayman 3"
STAGE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ex", "remix_x86")
DGVOODOO = os.path.join(G, "_remix_off", "D3D8.dll.dgvoodoo")
D3D8 = os.path.join(G, "D3D8.dll")
D3D8TO9_SIZE = 124928
REMIX_FILES = ["d3d9.dll", "NvRemixLauncher32.exe"]


def is_d3d8to9(path):
    # Any D3D8.dll that is not the saved dgVoodoo copy is a d3d8to9 variant.
    # (crosire's build is 124928 bytes, Remix's bundled one 134256 - do not
    #  match on size, that silently skipped the restore once already.)
    if not os.path.exists(path):
        return False
    if not os.path.exists(DGVOODOO):
        return True
    return hashlib.sha256(open(path, "rb").read()).hexdigest() != \
           hashlib.sha256(open(DGVOODOO, "rb").read()).hexdigest()


REAL_D3D8 = os.path.join(G, "d3d8_real.dll")
PROXY_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "src", "d3d8proxy.dll")


def normal():
    for name in REMIX_FILES + ["d3d8_real.dll"]:
        p = os.path.join(G, name)
        if os.path.exists(p):
            os.remove(p)
    if is_d3d8to9(D3D8) or not os.path.exists(D3D8):
        if os.path.exists(D3D8):
            os.remove(D3D8)
        shutil.copy2(DGVOODOO, D3D8)
    scr = os.path.join(G, "scripts")
    pr = os.path.join(scr, "R3Probe.asi")
    if os.path.exists(pr):
        os.remove(pr)
    off = os.path.join(scr, "BetterRayman3.asi.disabled")
    if os.path.exists(off):
        shutil.move(off, os.path.join(scr, "BetterRayman3.asi"))
    assert not is_d3d8to9(D3D8), "normal(): D3D8.dll is still a d3d8to9 build"
    return "normal: dgVoodoo D3D8 + BetterRayman3, no Remix"


def remix():
    """Capture chain: our proxy -> d3d8to9 -> Remix d3d9 shim -> ARM64 bridge."""
    os.makedirs(os.path.join(G, "_remix_off"), exist_ok=True)
    if os.path.exists(D3D8) and not is_d3d8to9(D3D8):
        shutil.move(D3D8, DGVOODOO)
    shutil.copy2(PROXY_SRC, D3D8)                              # capture in front
    shutil.copy2(os.path.join(STAGE, "d3d8to9.dll"), REAL_D3D8)  # D3D8 -> D3D9
    for name in REMIX_FILES:
        shutil.copy2(os.path.join(STAGE, name), os.path.join(G, name))
    scr = os.path.join(G, "scripts")
    br3 = os.path.join(scr, "BetterRayman3.asi")
    if os.path.exists(br3):
        shutil.move(br3, os.path.join(scr, "BetterRayman3.asi.disabled"))
    log = os.path.join(G, "r3probe.log")
    if os.path.exists(log):
        os.remove(log)
    return "remix: D3D8.dll=proxy -> d3d8_real=d3d8to9 -> d3d9=Remix -> bridge (NvRemixLauncher32)"


def probe():
    """Probe mode: d3d8proxy -> d3d8_real(dgVoodoo), BetterRayman3 stepped aside.

    The proxy is named D3D8.dll so the loader resolves it before the game's
    first D3D call; an ASI arrives too late and misses CreateDevice.
    """
    os.makedirs(os.path.join(G, "_remix_off"), exist_ok=True)
    if not os.path.exists(DGVOODOO):
        raise SystemExit("probe(): no saved dgVoodoo copy at " + DGVOODOO)
    if os.path.exists(D3D8):
        os.remove(D3D8)
    shutil.copy2(DGVOODOO, REAL_D3D8)
    shutil.copy2(PROXY_SRC, D3D8)
    for name in REMIX_FILES:
        p = os.path.join(G, name)
        if os.path.exists(p):
            os.remove(p)
    scr = os.path.join(G, "scripts")
    pr = os.path.join(scr, "R3Probe.asi")
    if os.path.exists(pr):
        os.remove(pr)
    br3 = os.path.join(scr, "BetterRayman3.asi")
    if os.path.exists(br3):
        shutil.move(br3, os.path.join(scr, "BetterRayman3.asi.disabled"))
    log = os.path.join(G, "r3probe.log")
    if os.path.exists(log):
        os.remove(log)
    assert os.path.getsize(D3D8) == os.path.getsize(PROXY_SRC), "probe(): proxy not in place"
    return "probe: D3D8.dll = d3d8proxy -> d3d8_real.dll (dgVoodoo), BR3 disabled"


def d3d8_kind():
    if not os.path.exists(D3D8):
        return "missing"
    h = hashlib.sha256(open(D3D8, "rb").read()).hexdigest()
    if os.path.exists(PROXY_SRC) and h == hashlib.sha256(open(PROXY_SRC, "rb").read()).hexdigest():
        return "d3d8proxy (logging)"
    if os.path.exists(DGVOODOO) and h == hashlib.sha256(open(DGVOODOO, "rb").read()).hexdigest():
        return "dgVoodoo"
    return "d3d8to9"


def status():
    rows = [
        ("D3D8.dll", d3d8_kind()),
        ("d3d9.dll (remix shim)", "present" if os.path.exists(os.path.join(G, "d3d9.dll")) else "absent"),
        ("BetterRayman3.asi", "present" if os.path.exists(os.path.join(G, "scripts", "BetterRayman3.asi")) else "absent"),
        (".trex_arm64", "present" if os.path.isdir(os.path.join(G, ".trex_arm64")) else "absent"),
        ("d3d8_real.dll", "present" if os.path.exists(REAL_D3D8) else "absent"),
        ("videos", "%d bik" % len(os.listdir(os.path.join(G, "videos"))) if os.path.isdir(os.path.join(G, "videos")) else "absent"),
        ("R3Probe.asi", "present" if os.path.exists(os.path.join(G, "scripts", "R3Probe.asi")) else "absent"),
        ("r3probe.log", "%d bytes" % os.path.getsize(os.path.join(G, "r3probe.log")) if os.path.exists(os.path.join(G, "r3probe.log")) else "absent"),
    ]
    return "\n".join("  %-22s %s" % r for r in rows)


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "status"
    print({"normal": normal, "remix": remix, "probe": probe, "status": status}.get(mode, status)())
