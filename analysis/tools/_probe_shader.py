
from pxr import Usd, UsdShade
import sys
st = Usd.Stage.Open(sys.argv[1])
for prim in st.Traverse():
    if prim.GetTypeName() == "Material":
        for c in Usd.PrimRange(prim):
            if c.IsA(UsdShade.Shader):
                print("shader:", c.GetPath())
                print("info id:", c.GetAttribute("info:id").Get())
                for a in c.GetAttributes():
                    n = a.GetName()
                    v = a.Get()
                    try: v = str(v)[:70]
                    except Exception: v = "<unprintable>"
                    print(f"   attr {n:38s} = {v}")
                print("   custom attrs:", [a.GetName() for a in c.GetAttributes() if a.IsCustom()])
        break
