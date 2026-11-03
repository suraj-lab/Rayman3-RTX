"""Run with: python analysis/tools/r3usd.py tests/test_remix_inventory.py"""
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from pxr import Sdf, Usd, UsdGeom, UsdLux, UsdShade

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "analysis/tools"))
import r3usd_dump as dump
import r3level_metrics as metrics


class InventoryTests(unittest.TestCase):
    def scene(self):
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/RootNode/meshes/example")
        root.CreateVisibilityAttr().Set("invisible")
        mesh = UsdGeom.Mesh.Define(stage, "/RootNode/meshes/example/mesh")
        mesh.CreatePointsAttr().Set([(0, 0, 0), (1, 0, 0), (0, 1, 0)])
        mesh.CreateFaceVertexCountsAttr().Set([3])
        mesh.CreateFaceVertexIndicesAttr().Set([0, 1, 2])
        mesh.CreateNormalsAttr().Set([(0, 0, 1)] * 3)
        mesh.SetNormalsInterpolation("vertex")
        uv = UsdGeom.PrimvarsAPI(mesh).CreatePrimvar("st", Sdf.ValueTypeNames.TexCoord2fArray, "vertex")
        uv.Set([(0, 0), (1, 0), (0, 1)])
        inst = UsdGeom.Xform.Define(stage, "/RootNode/instances/example")
        inst.GetPrim().GetReferences().AddInternalReference(root.GetPath())
        inst.CreateVisibilityAttr().Set("inherited")
        return stage, mesh

    def test_template_not_counted_as_visible_geometry(self):
        stage, _ = self.scene()
        with patch.object(dump.Usd.Stage, "Open", return_value=stage), patch.object(Path, "read_bytes", return_value=b"fixture"):
            data = dump.dump_stage("fixture.usd")
        self.assertEqual(data["summary"]["templates"], 1)
        self.assertEqual(data["summary"]["visible_placements"], 1)
        self.assertEqual(data["summary"]["visible_tris"], 1)
        self.assertEqual(data["summary"]["template_tris"], 1)

    def test_topology_uv_and_normals_validation(self):
        stage, mesh = self.scene()
        get = lambda: dump.mesh_info(mesh.GetPrim(), UsdGeom.XformCache())
        self.assertTrue(get()["has_valid_uv"])
        self.assertTrue(get()["normals_valid"])
        UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st").Set([(0, 0)])
        mesh.GetNormalsAttr().Set([(0, 0, 0)] * 3)
        mesh.GetFaceVertexIndicesAttr().Set([0, 1, 99])
        self.assertFalse(get()["has_valid_uv"])
        self.assertFalse(get()["normals_valid"])
        self.assertFalse(get()["topology_valid"])

    def test_authored_scalar_asset_and_non_input_attributes(self):
        stage = Usd.Stage.CreateInMemory()
        mat = UsdShade.Material.Define(stage, "/Looks/test")
        shader = UsdShade.Shader.Define(stage, "/Looks/test/Shader")
        shader.CreateInput("diffuse_texture", Sdf.ValueTypeNames.Asset).Set(Sdf.AssetPath("textures/test.dds"))
        shader.CreateInput("reflection_roughness_constant", Sdf.ValueTypeNames.Float).Set(0.9)
        shader.GetPrim().CreateAttribute("enable_opacity", Sdf.ValueTypeNames.Bool).Set(True)
        attrs = dump.material_info(mat.GetPrim())["shaders"][0]["authored_attributes"]
        self.assertAlmostEqual(attrs["inputs:reflection_roughness_constant"], 0.9)
        self.assertEqual(attrs["inputs:diffuse_texture"]["asset"], "textures/test.dds")
        self.assertTrue(attrs["enable_opacity"])
        json.dumps(attrs, allow_nan=False)

    def test_light_authored_vs_schema_defaults_and_world_position(self):
        stage = Usd.Stage.CreateInMemory()
        root = UsdGeom.Xform.Define(stage, "/Root")
        root.AddScaleOp().Set((-1, 1, 1))
        light = UsdLux.SphereLight.Define(stage, "/Root/light")
        light.AddTranslateOp().Set((10, 2, 3))
        light.CreateIntensityAttr().Set(25)
        data = dump.light_info(light.GetPrim(), UsdGeom.XformCache())
        self.assertEqual(data["position_world"], [-10, 2, 3])
        self.assertEqual(data["effective"]["intensity"], 25)
        self.assertNotIn("inputs:exposure", data["authored_attributes"])
        self.assertEqual(data["effective"]["exposure"], 0)

    def test_reflections_are_path_candidates_not_generic_strings(self):
        refs = metrics.texture_refs(b"noise reflect _REFLECT1\0sfx\\gold_txy_reflect.tga\0"
                                    b"TOWER\\tow_WaterFX.tga\0templeptz\\ptztow_a_REFLECT2.tga\0")
        result = metrics.reflect_stats(refs)
        self.assertEqual(result["planar_name_candidates"], ["templeptz/ptztow_a_reflect2"])
        self.assertEqual(result["waterfx_name_candidates"], ["tower/tow_waterfx"])
        self.assertEqual(result["other_reflect_names"], ["sfx/gold_txy_reflect"])

    def test_disabled_lights_excluded_from_range_heuristic(self):
        off = dict(on=0, type=1, far=1000, col=(1, 1, 1, 1))
        on = dict(on=1, type=1, far=2, col=(1, 1, 1, 1))
        with patch.object(metrics.lights, "lights", return_value=[off, on]):
            data = metrics.light_stats("test")
        self.assertEqual(data["lights"], 2)
        self.assertEqual(data["active_lights"], 1)
        self.assertEqual(data["active_range2_colour_heuristic"], 4)


if __name__ == "__main__":
    unittest.main()
