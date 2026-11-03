"""Enumerate Rayman 3 PC LightInfo lists from a level's .lvl/.ptr by following the in-file linked list. Read-only.

usage: python analysis/tools/r3lights_walk.py <level> [--csv out.csv]     e.g. tower_20
       python analysis/tools/r3lights_walk.py --all                       (summary per level)

Format facts (verified by hexdump on tower_20, see analysis/level-data-format.md):
  .ptr = u32 count, then count x (i32 fileID, u32 offset); `offset + 4` is the file position of a
  relocated u32 pointer in the .lvl; a pointer value v means file position v + 4.
  LightInfo is a 0x118-byte list element: type u16 @+8, far/near f32 @+0xC/+0x10, matrix type tag
  u32 @+0x30 then 4x4 f32 @+0x34 (row 3 = position), colour f32[4] @+0x9C (alpha 1.0), and a
  relocated 'next' pointer @+0x110 aimed at the next element (end of list: raw value 1).

shortcut: a light is accepted only if it has a relocated next pointer to another valid light (or is
the target of one), so a single-element list is missed; upgrade by walking from the list owners
instead. Structure validity (on/shadow <= 1, type < 8, finite floats, colour alpha ~1.0) is checked,
nothing else about the data is interpreted.
"""
import glob, math, os, struct, sys

G = r"C:\Program Files\GOG Galaxy\Games\Rayman 3\Gamedatabin"
SZ = 0x118


def load(level):
    d = f"{G}/{level}/{level}" if level != "fix" else f"{G}/fix"
    b = open(d + ".lvl", "rb").read()
    p = open(d + ".ptr", "rb").read()
    n = struct.unpack_from("<I", p, 0)[0]
    rel = {struct.unpack_from("<I", p, 4 + 8 * i + 4)[0] + 4 for i in range(n)}
    return b, rel


def lights(level):
    b, rel = load(level)

    def valid(s):
        if s < 0 or s + SZ > len(b):
            return False
        typ = struct.unpack_from("<H", b, s + 8)[0]
        far, near = struct.unpack_from("<2f", b, s + 12)
        col = struct.unpack_from("<4f", b, s + 0x9C)
        return (b[s] <= 1 and b[s + 1] <= 1 and typ < 8 and all(math.isfinite(x) for x in col + (far, near))
                and 0.99 <= col[3] <= 1.01)

    nxt = {}
    for r in rel:
        s = r - 0x110
        if valid(s) and struct.unpack_from("<I", b, r)[0] + 4 == s + SZ and valid(s + SZ):
            nxt[s] = s + SZ
    nodes = set(nxt) | set(nxt.values())
    heads = sorted(s for s in nodes if s not in set(nxt.values()))
    out = []
    for ci, h in enumerate(heads):
        s = h
        while True:
            typ = struct.unpack_from("<H", b, s + 8)[0]
            far, near = struct.unpack_from("<2f", b, s + 12)
            m = struct.unpack_from("<16f", b, s + 0x34)
            out.append(dict(chain=ci, off=s, on=b[s], shadow=b[s + 1], type=typ, far=far, near=near,
                            tag=struct.unpack_from("<I", b, s + 0x30)[0], pos=m[12:15], rot=m[:12],
                            col=struct.unpack_from("<4f", b, s + 0x9C)))
            if s not in nxt:
                break
            s = nxt[s]
    return out


def main(a):
    if a[:1] == ["--all"]:
        for f in sorted(glob.glob(G + "/*/*.lvl")):
            lv = os.path.basename(f)[:-4]
            if lv == "transit" or not os.path.exists(f[:-4] + ".ptr"):
                continue
            L = lights(lv)
            print(f"{lv:10s} lights={len(L):4d} chains={len({x['chain'] for x in L})}")
        return
    L = lights(a[0])
    if "--csv" in a:
        with open(a[a.index("--csv") + 1], "w") as fh:
            fh.write("chain,off,on,shadow,type,far,near,tag,r,g,b,a,x,y,z\n")
            for x in L:
                fh.write(f"{x['chain']},{x['off']:#x},{x['on']},{x['shadow']},{x['type']},{x['far']:.3f},{x['near']:.3f},"
                         f"{x['tag']},{x['col'][0]:.4f},{x['col'][1]:.4f},{x['col'][2]:.4f},{x['col'][3]:.2f},"
                         f"{x['pos'][0]:.3f},{x['pos'][1]:.3f},{x['pos'][2]:.3f}\n")
    print(f"{a[0]}: {len(L)} lights in {len({x['chain'] for x in L})} chains")
    for x in L[:5]:
        print(f"  chain{x['chain']} {x['off']:#x} t={x['type']} far={x['far']:.2f} near={x['near']:.2f} "
              f"col=({x['col'][0]:.3f},{x['col'][1]:.3f},{x['col'][2]:.3f}) pos=({x['pos'][0]:.1f},{x['pos'][1]:.1f},{x['pos'][2]:.1f})")


if __name__ == "__main__":
    main(sys.argv[1:])
