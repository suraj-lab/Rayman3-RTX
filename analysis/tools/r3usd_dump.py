"""Read Remix captures with r3usd.py; never edit the stage or game files.

Usage: r3usd.py r3usd_dump.py <capture.usd OR captures-directory> [output.json]
Geometry totals separate hidden templates from visible scene placements. Authored
shader inputs are not MDL defaults. A capture is a snapshot, not a whole level.
"""
import collections
import hashlib
import json
import math
from pathlib import Path
import sys

from pxr import Sdf, Usd, UsdGeom, UsdLux, UsdShade


def value(v):
    if isinstance(v, Sdf.AssetPath):
        return {"asset": v.path, "resolved": v.resolvedPath}
    if v is None or isinstance(v, (str, bool, int, float)):
        return v
    return [value(x) for x in v]


def attributes(prim):
    return {a.GetName(): value(a.Get()) for a in prim.GetAuthoredAttributes()
            if a.GetName().startswith(("inputs:", "info:", "remix_category:"))
            or a.GetName() in ("enable_opacity", "filter_mode", "wrap_mode_u", "wrap_mode_v")}


def material_info(prim):
    shaders = [p for p in Usd.PrimRange(prim) if p.IsA(UsdShade.Shader)]
    return {"path": str(prim.GetPath()), "shaders": [
        {"path": str(p.GetPath()), "authored_attributes": attributes(p)} for p in shaders]}


def mesh_info(prim, xforms):
    m = UsdGeom.Mesh(prim)
    pts = m.GetPointsAttr().Get() or []
    counts = list(m.GetFaceVertexCountsAttr().Get() or [])
    indices = list(m.GetFaceVertexIndicesAttr().Get() or [])
    path = str(prim.GetPath())
    role = "template" if path.startswith("/RootNode/meshes/") else "placement"
    visible = UsdGeom.Imageable(prim).ComputeVisibility() != UsdGeom.Tokens.invisible
    sizes = {"constant": 1, "uniform": len(counts), "vertex": len(pts),
             "varying": len(pts), "faceVarying": len(indices)}
    uv = []
    colour = None
    metadata = {}
    for pv in UsdGeom.PrimvarsAPI(prim).GetPrimvars():
        name = str(pv.GetPrimvarName())
        if name.startswith("_remix_metadata:"):
            metadata[name.split(":", 1)[1]] = value(pv.Get())
        elif name == "st" or str(pv.GetTypeName()) in ("texCoord2f[]", "texCoord2d[]", "texCoord2h[]"):
            arr = pv.ComputeFlattened()
            finite = arr is not None and all(math.isfinite(float(c)) for v in arr for c in v)
            expected = sizes.get(str(pv.GetInterpolation()))
            uv.append({"name": name, "interpolation": str(pv.GetInterpolation()),
                       "count": len(arr) if arr is not None else 0,
                       "valid": bool(finite and expected is not None and len(arr) == expected)})
        elif name == "displayColor":
            arr = pv.ComputeFlattened()
            if arr is not None and len(arr):
                # Stored values only; not a screen-space or physically calibrated luminance.
                colour = {"count": len(arr), "mean_rgb": [sum(v[i] for v in arr) / len(arr) for i in range(3)]}
    normals = m.GetNormalsAttr().Get()
    normal_valid = (normals is not None and len(normals) == sizes.get(str(m.GetNormalsInterpolation()))
                    and all(math.isfinite(float(c)) for v in normals for c in v)
                    and all(sum(float(c) ** 2 for c in v) > 1e-12 for v in normals))
    binding, _ = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()
    return {"prim": path, "role": role, "visible": visible,
            "verts": len(pts), "faces": len(counts),
            "tris": sum(max(0, c - 2) for c in counts),
            "topology_valid": (all(c >= 3 for c in counts) and sum(counts) == len(indices)
                               and all(0 <= i < len(pts) for i in indices)),
            "points_finite": all(math.isfinite(float(c)) for v in pts for c in v),
            "uv_sets": uv, "has_valid_uv": any(v["valid"] for v in uv),
            "normals_valid": bool(normal_valid), "vertex_colour": colour,
            "material": str(binding.GetPath()) if binding else None,
            "transform_determinant": xforms.GetLocalToWorldTransform(prim).GetDeterminant(),
            "metadata": metadata}


def light_info(prim, xforms):
    api = UsdLux.LightAPI(prim)
    # Include schema fallbacks, but keep them separate from authored values.
    effective = {"color": value(api.GetColorAttr().Get()),
                 "intensity": api.GetIntensityAttr().Get(),
                 "exposure": api.GetExposureAttr().Get(),
                 "normalize": api.GetNormalizeAttr().Get()}
    if prim.IsA(UsdLux.SphereLight):
        effective["radius"] = UsdLux.SphereLight(prim).GetRadiusAttr().Get()
    if prim.IsA(UsdLux.DistantLight):
        effective["angle"] = UsdLux.DistantLight(prim).GetAngleAttr().Get()
    return {"path": str(prim.GetPath()), "type": prim.GetTypeName(),
            "position_world": value(xforms.GetLocalToWorldTransform(prim).ExtractTranslation()),
            "transform_local": value(UsdGeom.Xformable(prim).GetLocalTransformation()),
            "authored_attributes": attributes(prim), "effective": effective}


def dump_stage(path):
    stage = Usd.Stage.Open(str(path))
    if not stage:
        raise ValueError("Cannot open capture: " + str(path))
    mats, meshes, lights = [], [], []
    xforms = UsdGeom.XformCache()
    for prim in stage.Traverse():
        if prim.IsA(UsdShade.Material):
            mats.append(material_info(prim))
        elif prim.IsA(UsdGeom.Mesh):
            meshes.append(mesh_info(prim, xforms))
        elif prim.HasAPI(UsdLux.LightAPI):
            lights.append(light_info(prim, xforms))
    templates = [m for m in meshes if m["role"] == "template"]
    visible = [m for m in meshes if m["role"] == "placement" and m["visible"]]
    return {"capture": str(path), "sha256": hashlib.sha256(Path(path).read_bytes()).hexdigest(),
            "meters_per_unit_metadata": stage.GetMetadata("metersPerUnit"),
            "up_axis": stage.GetMetadata("upAxis"),
            "summary": {"materials": len(mats), "templates": len(templates),
                        "visible_placements": len(visible),
                        "template_tris": sum(m["tris"] for m in templates),
                        "visible_tris": sum(m["tris"] for m in visible),
                        "visible_verts": sum(m["verts"] for m in visible),
                        "invalid_topology": sum(not m["topology_valid"] for m in visible),
                        "missing_or_invalid_uv": sum(not m["has_valid_uv"] for m in visible),
                        "missing_or_invalid_normals": sum(not m["normals_valid"] for m in visible),
                        "baked_colour_placements": sum(bool(m["metadata"].get("isVertexColorBakedLighting")) for m in visible),
                        "lights_by_type": dict(collections.Counter(l["type"] for l in lights))},
            "materials": mats, "meshes": meshes, "lights": lights}


def main():
    source = Path(sys.argv[1])
    paths = sorted(source.glob("capture_*.usd")) if source.is_dir() else [source]
    if not paths:
        sys.exit("No captures in " + str(source))
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).resolve().parents[1] / "textures/capture-usd.json"
    captures = [dump_stage(p) for p in paths]
    data = captures[0] if len(captures) == 1 else {"captures": captures}
    data["schema_version"] = 2
    data["limitations"] = ["Snapshot geometry is not a level inventory.",
        "No MDL evaluation: absent authored inputs do not mean absent shader defaults.",
        "USD units and captured lights are not a calibration of game-space lighting."]
    # Serialise fully before writing, so an unsupported attribute cannot truncate the report.
    text = json.dumps(data, indent=1, allow_nan=False)
    out.write_text(text + "\n", encoding="utf8")
    for c in captures:
        print(Path(c["capture"]).name, json.dumps(c["summary"]))
    print("wrote", out)


if __name__ == "__main__":
    main()
