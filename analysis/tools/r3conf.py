"""Reversible edit of the game's Remix user.conf for the light-intensity experiment.

usage: python analysis/tools/r3conf.py status|apply|restore [--conf PATH] [--value N] [--key NAME]

apply   : backs up user.conf to backups/user.conf.before_lightintensity (never overwrites an existing backup),
          then sets one light-conversion key (default rtx.lightConversionIntensityFactor = 10, Remix default 1).
          Allowed keys (all present in RtxOptions.md and in the shipped d3d9.dll strings, checked 2026-11-03):
            rtx.lightConversionIntensityFactor            scales all converted legacy lights
            rtx.lightConversionDistantLightFixedIntensity  intensity of converted directional lights (W/sr, default 1)
            rtx.lightConversionSphereLightFixedRadius      radius of converted point/spot lights (default 4)
restore : copies the backup back and deletes nothing else.
Only that one key is touched; all other lines are preserved byte for byte.
"""
import hashlib, os, shutil, sys

KEYS = ("rtx.lightConversionIntensityFactor", "rtx.lightConversionDistantLightFixedIntensity",
        "rtx.lightConversionSphereLightFixedRadius",
        "rtx.legacyMaterial.enableEmissive", "rtx.legacyMaterial.emissiveIntensity",
        "rtx.legacyMaterial.emissiveColorConstant", "rtx.emissiveIntensity", "rtx.boostEmissives",
        "rtx.userBrightness.userBrightness", "rtx.enableAutoExposure", "rtx.exposureBias",
        "rtx.raytracedRenderTarget.enable", "rtx.skipDrawCallsPostRTXInjection",
        "rtx.enableAlphaBlend", "rtx.legacyMaterial.ignoreAlphaChannel",
        "rtx.captureShowMenuOnHotkey", "rtx.captureMaxFrames", "rtx.enableReplacementAssets",
        "rtx.lightmapTextures", "rtx.lightConverter", "rtx.ignoreBakedLightingTextures",
        "rtx.graphicsPreset", "rtx.vertexColorStrength", "rtx.ignoreAllVertexColorBakedLighting")
KEY = KEYS[0]
HERE = os.path.dirname(os.path.abspath(__file__))
BACKUP = os.path.join(HERE, "..", "..", "backups", "user.conf.before_lightintensity")
CONF = r"C:\Program Files\GOG Galaxy\Games\Rayman 3\user.conf"


def sha(p):
    return hashlib.sha256(open(p, "rb").read()).hexdigest()[:16]


def main(a):
    conf, val = CONF, "10"
    if "--conf" in a:
        conf = a[a.index("--conf") + 1]
    if "--value" in a:
        val = a[a.index("--value") + 1]
        [float(x) for x in val.split(",")]  # reject non-numbers before writing
    # --set "key=value" (repeatable) writes several keys in one apply; otherwise --key/--value
    sets = [x for i, x in enumerate(a) if i and a[i - 1] == "--set"]
    key = a[a.index("--key") + 1] if "--key" in a else KEY
    if not sets and key not in KEYS:
        sys.exit("key must be one of: " + ", ".join(KEYS))
    for kv in sets:
        if "=" not in kv:
            sys.exit("--set needs key=value")
        k, v = kv.split("=", 1)
        if k.strip() not in KEYS:
            sys.exit("unknown key: " + k)
        try:
            for part in v.split(","):
                t = part.strip()
                if t.lower() in ("true", "false"):
                    continue
                if t.lower().startswith("0x"):
                    int(t, 16); continue
                float(t)
        except ValueError:
            sys.exit(f"value for {k.strip()} must be True/False or numbers: {v}")
    cmd = a[0] if a else "status"
    cur = open(conf, "rb").read().decode("utf8")
    nl = "\r\n" if "\r\n" in cur else "\n"
    if cmd == "status":
        lines = [l for l in cur.splitlines() if l.startswith(KEYS)]
        print(f"{conf} sha {sha(conf)}; light keys: {lines or 'none set (Remix defaults)'}; backup exists: {os.path.exists(BACKUP)}")
    elif cmd == "apply":
        if not os.path.exists(BACKUP):
            os.makedirs(os.path.dirname(BACKUP), exist_ok=True)
            shutil.copy2(conf, BACKUP)
        wanted = sets or [f"{key}={val}"]
        pairs = [tuple(x.split("=", 1)) for x in wanted]
        keep = [l for l in cur.splitlines() if not any(l.startswith(k.strip() + " ") for k, _ in pairs)]
        add = "".join(f"{k.strip()} = {v.strip()}{nl}" for k, v in pairs)
        open(conf, "wb").write((nl.join(keep) + nl + add).encode("utf8"))
        print("applied: " + ", ".join(f"{k.strip()}={v.strip()}" for k, v in pairs) + f"; backup sha {sha(BACKUP)}; new sha {sha(conf)}")
    elif cmd == "restore":
        shutil.copy2(BACKUP, conf)
        print(f"restored; sha {sha(conf)} (backup {sha(BACKUP)})")
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv[1:])
