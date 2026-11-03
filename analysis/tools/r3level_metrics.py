"""Read-only level inventory. Writes derived level-metrics.csv, never game data.

Use texture paths stored in each .lvl, not a guessed level-prefix mapping.
Name markers are candidate evidence, not proof of active planar reflections.
The colour x range^2 sum is an uncalibrated heuristic, NOT radiant power.
Texture means are equally weighted over unique referenced (directory, name) pairs,
not pixels, surface area or screen coverage. Missing stats remain explicit.
"""
import collections
import csv
import glob
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import r3lights_walk as lights

OUT = os.path.join(os.path.dirname(HERE), "textures")
TEXTURE_PATH = re.compile(rb"([A-Za-z0-9_]+)[\\/]+([A-Za-z0-9_.-]+)\.(?:tga|gf|bmp)\b", re.I)


def level_names():
    return sorted(os.path.basename(p)[:-4] for p in glob.glob(os.path.join(lights.G, "*", "*.lvl"))
                  if not p.endswith("transit.lvl")) + ["fix"]


def texture_refs(data):
    return sorted({(d.decode().lower(), n.decode().lower()) for d, n in TEXTURE_PATH.findall(data)})


def reflect_stats(refs):
    return {"planar_name_candidates": [d + "/" + n for d, n in refs if re.search(r"_reflect[12]$", n)],
            "waterfx_name_candidates": [d + "/" + n for d, n in refs if "waterfx" in n or "watereffect" in n],
            "other_reflect_names": [d + "/" + n for d, n in refs if "reflect" in n and not re.search(r"_reflect[12]$", n)]}


def light_stats(name):
    parsed = lights.lights(name)
    active = [x for x in parsed if x["on"]]
    return {"lights": len(parsed), "active_lights": len(active),
            "types": " ".join(f"t{k}:{v}" for k, v in sorted(collections.Counter(x["type"] for x in parsed).items())),
            "active_range2_colour_heuristic": sum(max(0, sum(x["col"][:3]) / 3) * max(0, x["far"]) ** 2 for x in active),
            "max_far": max((x["far"] for x in active), default=0),
            "negative_colour_lights": sum(any(v < 0 for v in x["col"][:3]) for x in parsed)}


def texture_stats():
    # Multiple CNTs contain variants. Prefer 32-bit packs, then 16-bit, deterministically.
    preference = {p: i for i, p in enumerate(("tex32_1.cnt", "tex32_2.cnt", "tex16_1.cnt", "tex16_2.cnt"))}
    with open(os.path.join(OUT, "texture-stats.csv"), encoding="utf8") as f:
        rows = sorted(csv.DictReader(f), key=lambda r: preference.get(r["pack"], 99))
    stats = {}
    for row in rows:
        key = (row["dir"].lower(), os.path.splitext(row["name"])[0].lower())
        stats.setdefault(key, row)
    return stats


def level_row(name, stats):
    stem = f"{lights.G}/{name}/{name}" if name != "fix" else f"{lights.G}/fix"
    with open(stem + ".lvl", "rb") as f:
        refs = texture_refs(f.read())
    row = {"level": name}
    row.update({k: ";".join(v) for k, v in reflect_stats(refs).items()})
    row["referenced_dirs"] = " ".join(f"{d}:{n}" for d, n in collections.Counter(d for d, _ in refs).most_common())
    row["texture_refs"] = len(refs)
    matched = [stats[key] for key in refs if key in stats]
    row["stats_matched_refs"] = len(matched)
    row["stats_missing_refs"] = len(refs) - len(matched)
    row["referenced_texture_mean_luma"] = sum(float(r["mean"]) for r in matched) / len(matched) if matched else ""
    try:
        row.update(light_stats(name))
        row["light_error"] = ""
    except (OSError, ValueError, IndexError) as e:
        row["light_error"] = str(e)
    return row


def main():
    stats = texture_stats()
    rows = [level_row(n, stats) for n in level_names()]
    keys = list(dict.fromkeys(k for r in rows for k in r))
    path = os.path.join(OUT, "level-metrics.csv")
    with open(path, "w", newline="", encoding="utf8") as f:
        writer = csv.DictWriter(f, fieldnames=keys)
        writer.writeheader()
        writer.writerows(rows)
    candidates = [r["level"] for r in rows if r["planar_name_candidates"]]
    print(f"{len(rows)} files (includes fix); planar-name candidates: {candidates}")
    print("Waterfx-name files:", sum(bool(r["waterfx_name_candidates"]) for r in rows))
    print("Light parse errors:", [(r["level"], r["light_error"]) for r in rows if r["light_error"]])
    print("No file classified as definitely reflective or non-reflective from names alone.")
    print("wrote", path)


if __name__ == "__main__":
    main()
