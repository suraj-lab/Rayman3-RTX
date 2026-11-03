"""Wrapper that runs a USD script with the RTX Remix Toolkit's bundled Python 3.12 + USD bindings.

The toolkit ships omni.usd.libs (cp312) inside extscache; pxr imports once that package root is on
PYTHONPATH and its bin directory is on PATH. No pip install, no wheel, no system Python.

usage: python analysis/tools/r3usd.py <script.py> [args...]
"""
import glob, os, subprocess, sys

REMIX = r"C:\Program Files\NVIDIA Corporation\RTX Remix"
PY = os.path.join(REMIX, "kit", "python", "python.exe")


def libs_dir():
    hits = glob.glob(os.path.join(REMIX, "extscache", "omni.usd.libs-*"))
    if not hits:
        sys.exit("omni.usd.libs not found under " + REMIX)
    return sorted(hits)[-1]


def main():
    if not os.path.exists(PY):
        sys.exit("kit python not found: " + PY)
    libs = libs_dir()
    env = dict(os.environ)
    env["PYTHONPATH"] = os.pathsep.join(filter(None, [libs, env.get("PYTHONPATH", "")]))
    env["PATH"] = os.pathsep.join([os.path.join(libs, "bin"), env.get("PATH", "")])
    raise SystemExit(subprocess.call([PY] + sys.argv[1:], env=env))


if __name__ == "__main__":
    main()
