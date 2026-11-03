"""Match Remix-captured texture hashes to Rayman 3 .gf texture names. Read-only.

Capture textures (rtx-remix/captures/textures/<HASH>.dds) are uncompressed R8G8B8A8_UNORM with mips.
We decode mip0, build a small RGB signature, and compare it against every .gf of the same size, decoding
those with analysis/tools/gfdecode.py. Output: hash -> best name + distance, plus any glow-named matches.

usage: python analysis/tools/r3match.py [--max-dist 12] [--list]
"""
import glob, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gfdecode as gf

G = r"C:\Program Files\GOG Galaxy\Games\Rayman 3"
CAP = os.path.join(G, "rtx-remix", "captures", "textures")
GRID = 16


def dds_signature(path):
    b = open(path, "rb").read()
    if b[:4] != b"DDS ":
        return None
    h, w = struct.unpack_from("<II", b, 12)
    if b[84:88] == b"DX10":
        dxgi = struct.unpack_from("<I", b, 128)[0]
        if dxgi not in (28, 87, 88):      # RGBA8 / BGRA8 / BGRX8 only
            return None
        hdr = 148
        bgra = dxgi == 87
    else:
        hdr = 128
        bgra = True
    if w * h * 4 > len(b) - hdr:
        return None
    return grid_sig(b, hdr, w, h, 4, bgra)


def grid_sig(buf, base, w, h, bpp, bgra):
    """GRID x GRID mean RGB, from the first (mip0) w*h pixels."""
    cells = [[[0, 0, 0, 0] for _ in range(GRID)] for _ in range(GRID)]
    for y in range(h):
        gy = y * GRID // h
        row = base + y * w * bpp
        for x in range(w):
            o = row + x * bpp
            r, g, bl = (buf[o + 2], buf[o + 1], buf[o]) if bgra else (buf[o], buf[o + 1], buf[o + 2])
            c = cells[gy][x * GRID // w]
            c[0] += r; c[1] += g; c[2] += bl; c[3] += 1
    sig = []
    for gy in range(GRID):
        for gx in range(GRID):
            c = cells[gy][gx]
            n = c[3] or 1
            sig.append((c[0] / n, c[1] / n, c[2] / n))
    return (w, h, sig)


def gf_signature(buf, w, h, bpp):
    # buf is planar channel-major; expand to an interleaved RGB byte string then reuse grid_sig
    px = bytearray(w * h * 4)
    for i in range(w * h):
        px[i * 4 + 0] = buf[i * bpp + 2]      # B,G,R -> stored as BGRA order
        px[i * 4 + 1] = buf[i * bpp + 1]
        px[i * 4 + 2] = buf[i * bpp + 0]
        px[i * 4 + 3] = 255
    return grid_sig(bytes(px), 0, w, h, 4, True)


def dist(a, b):
    s = 0.0
    for (r1, g1, b1), (r2, g2, b2) in zip(a[2], b[2]):
        s += abs(r1 - r2) + abs(g1 - g2) + abs(b1 - b2)
    return s / (len(a[2]) * 3)


def main(a):
    max_dist = float(a[a.index("--max-dist") + 1]) if "--max-dist" in a else 12.0
    # index every .gf header by size (cheap: header only)
    by_size = {}
    for pack in ("tex32_1.cnt", "tex32_2.cnt", "tex16_1.cnt", "tex16_2.cnt"):
        b, es = gf.entries(pack)
        for e in es:
            if not e["name"].lower().endswith(".gf"):
                continue
            try:
                fmt, w, h, bpp, mips, rle, used, _ = gf.decode(gf.payload(b, e))
            except Exception:
                continue
            by_size.setdefault((w, h), []).append((pack, e, fmt, bpp))

    caps = sorted(glob.glob(os.path.join(CAP, "*.dds")))
    print(f"captures: {len(caps)} textures\n")
    print(f"{'hash':18s} {'size':>9s} {'best match':26s} {'dist':>6s}  pack")
    cache = {}
    out = []
    for p in caps:
        sig = dds_signature(p)
        if not sig:
            print(f"  {os.path.basename(p):18s} unsupported format"); continue
        key = (sig[0], sig[1])
        best = None
        for pack, e, fmt, bpp in by_size.get(key, []):
            ck = (pack, e["name"])
            if ck not in cache:
                try:
                    f2, w2, h2, b2, m2, r2, u2, buf = gf.decode(gf.payload(*gf.entries(pack)[:1], e) if False else gf.payload(gf.entries(pack)[0], e))
                    cache[ck] = gf_signature(buf, w2, h2, b2)
                except Exception:
                    cache[ck] = None
            gsig = cache[ck]
            if not gsig:
                continue
            d = dist(sig, gsig)
            if best is None or d < best[0]:
                best = (d, e["name"], pack)
        h = os.path.splitext(os.path.basename(p))[0]
        if best:
            flag = "" if best[0] <= max_dist else "  (weak)"
            print(f"{h:18s} {key[0]}x{key[1]:<6d} {best[1]:26s} {best[0]:6.1f}  {best[2]}{flag}")
            out.append((h, best[1], best[0]))
        else:
            print(f"{h:18s} {key[0]}x{key[1]:<6d} (no size match)")
    print(f"\nmatched {len(out)}/{len(caps)}; strong matches (<= {max_dist}): {sum(1 for _,_,d in out if d<=max_dist)}")
    if "--list" in a:
        glow = [x for x in out if any(k in x[1].lower() for k in
                ("glow","light","flame","fire","spark","orb","star","lamp","lum","crystal","flare"))]
        print("\nglow/light-named matches (rtx.lightConverter candidates):")
        for h, n, d in glow:
            print(f"  {h}  {n}  (dist {d:.1f})")


if __name__ == "__main__":
    main(sys.argv[1:])
