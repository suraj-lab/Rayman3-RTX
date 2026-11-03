"""Decode Rayman 3 .gf textures from a .cnt pack to PNG and report brightness stats. Read-only.

Formats (from BinarySerializer.OpenSpace, MIT, plus this project's own verification):
  GFFile   = GFFileHeader, [palette], RLE-compressed image
  Header   = u32 format, i32 width, i32 height, u8 bytesPerPixel, u8 mipmapLevels, u8 rleCode
  format   = the bit layout written as a decimal number: 0x22b8 = 8888, 0x378 = 888,
             0x115c = 4444, 0x235 = 565
  RLE      = channel-major across ALL mip levels (channel 0 of every mip, then channel 1, ...);
             a byte equal to rleCode escapes: next byte = value, then count.
  Channels = bpp 4: B,G,R,A ; bpp 3: B,G,R ; bpp 2: low byte then high byte (little-endian).

Verified by exact payload consumption and by matching 16-bit vs 32-bit renders of the same texture
(wood_light: a glowing glass lamp) and by the menu art in mnu_gen.

usage:
  python analysis/tools/gfdecode.py --name wood_light --out wood_light.png [--pack tex32_1.cnt] [--flip]
  python analysis/tools/gfdecode.py --stats [--pack tex32_1.cnt]
"""
import os, struct, sys, zlib

G = r"C:\Program Files\GOG Galaxy\Games\Rayman 3\Gamedatabin"
FMT_8888, FMT_888, FMT_4444, FMT_565 = 8888, 888, 4444, 565


def cstr(b, off, key):
    n = struct.unpack_from("<i", b, off)[0]
    off += 4
    return bytes(x ^ key for x in b[off:off + n]).decode("cp1252", "replace"), off + n


def entries(pack):
    b = open(os.path.join(G, pack), "rb").read()
    dirs_n, files_n = struct.unpack_from("<2I", b, 0)
    is_xor, _csum, key = b[8], b[9], b[10]
    off = 11
    dirs = []
    for _ in range(dirs_n):
        s, off = cstr(b, off, key if is_xor else 0)
        dirs.append(s)
    off += 1
    out = []
    for _ in range(files_n):
        di = struct.unpack_from("<i", b, off)[0]; off += 4
        name, off = cstr(b, off, key if is_xor else 0)
        xk = b[off:off + 4]; off += 4
        _c, foff, fsize = struct.unpack_from("<3I", b, off); off += 12
        out.append(dict(dir=dirs[di] if 0 <= di < len(dirs) else "", name=name,
                        xor=xk, off=foff, size=fsize))
    return b, out


def payload(b, e):
    raw = b[e["off"]:e["off"] + e["size"]]
    lim = len(raw) - (len(raw) % 4)
    return bytes(c ^ e["xor"][i % 4] for i, c in enumerate(raw[:lim])) + raw[lim:]


def decode(data):
    """Return (fmt, w, h, bpp, mips, rle, bytes_used, planar_buffer)."""
    fmt, w, h = struct.unpack_from("<Iii", data, 0)
    bpp = data[12]
    mips = data[13] or 1
    rle = data[14]
    if w <= 0 or h <= 0 or bpp not in (2, 3, 4) or w * h > 4096 * 4096:
        raise ValueError(f"implausible header fmt={fmt:#x} {w}x{h} bpp={bpp}")
    sizes = []
    mw, mh = w, h
    for _ in range(mips):
        sizes.append(mw * mh)
        mw = max(1, mw // 2)
        mh = max(1, mh // 2)
    total = sum(sizes)
    buf = bytearray(total * bpp)
    p = 15
    for ch in range(bpp):
        px = 0
        while px < total:
            if p >= len(data):
                raise ValueError("stream ended early")
            v = data[p]; p += 1
            if v == rle:
                val = data[p]; cnt = data[p + 1]; p += 2
                for _ in range(cnt):
                    if px >= total:
                        break
                    buf[ch + px * bpp] = val
                    px += 1
            else:
                buf[ch + px * bpp] = v
                px += 1
    return fmt, w, h, bpp, mips, rle, p, bytes(buf)


def rgb_rows(fmt, w, h, bpp, buf, flip=False):
    rows = []
    ys = range(h - 1, -1, -1) if flip else range(h)
    for y in ys:
        row = bytearray()
        for x in range(w):
            i = (y * w + x) * bpp
            if bpp == 4 or bpp == 3:
                row += bytes((buf[i + 2], buf[i + 1], buf[i]))
            else:
                v = buf[i] | (buf[i + 1] << 8)
                if fmt == FMT_565:
                    row += bytes((((v >> 11) & 31) * 255 // 31,
                                  ((v >> 5) & 63) * 255 // 63,
                                  (v & 31) * 255 // 31))
                else:
                    row += bytes((((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17))
        rows.append(bytes(row))
    return rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(t, d):
        c = t + d
        return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n"
                           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6))
                           + chunk(b"IEND", b""))


def stats_of(fmt, w, h, bpp, buf):
    rows = rgb_rows(fmt, w, h, bpp, buf)
    tot = white = bright = 0
    n = w * h
    for r in rows:
        for i in range(0, len(r), 3):
            lum = (r[i] * 299 + r[i + 1] * 587 + r[i + 2] * 114) // 1000
            tot += lum
            if lum >= 240: white += 1
            elif lum >= 180: bright += 1
    return tot / n, 100.0 * white / n, 100.0 * bright / n


def main(a):
    packs = [a[a.index("--pack") + 1]] if "--pack" in a else \
        ["tex32_1.cnt", "tex32_2.cnt", "tex16_1.cnt", "tex16_2.cnt"]
    if "--stats" in a:
        print(f"{'name':26s} {'pack':12s} {'size':>9s} {'mean':>6s} {'white%':>7s} {'bright%':>8s}")
        rows = []
        for pk in packs:
            b, es = entries(pk)
            for e in es:
                if not e["name"].lower().endswith(".gf"):
                    continue
                try:
                    fmt, w, h, bpp, mips, rle, used, buf = decode(payload(b, e))
                except Exception:
                    continue
                mean, wht, brt = stats_of(fmt, w, h, bpp, buf)
                rows.append((mean, os.path.splitext(e["name"])[0], pk, f"{w}x{h}", wht, brt))
        rows.sort(reverse=True)
        for mean, nm, pk, size, wht, brt in rows[:25]:
            print(f"{nm:26s} {pk:12s} {size:>9s} {mean:6.1f} {wht:7.1f} {brt:8.1f}")
        print(f"decoded {len(rows)} textures across {len(packs)} packs")
        return

    name = a[a.index("--name") + 1].lower() if "--name" in a else sys.exit("need --name or --stats")
    out = a[a.index("--out") + 1] if "--out" in a else None
    for pk in packs:
        b, es = entries(pk)
        for e in es:
            if os.path.splitext(e["name"])[0].lower() != name:
                continue
            fmt, w, h, bpp, mips, rle, used, buf = decode(payload(b, e))
            mean, wht, brt = stats_of(fmt, w, h, bpp, buf)
            print(f"{pk}: {e['name']} dir={e['dir']} fmt={fmt:#x} {w}x{h} bpp={bpp} mips={mips} rle={rle} "
                  f"used={used}/{e['size']} mean={mean:.1f} white={wht:.1f}% bright={brt:.1f}%")
            if out:
                write_png(out, w, h, rgb_rows(fmt, w, h, bpp, buf, flip="--flip" in a))
                print("wrote", out)
            return
    sys.exit(f"texture {name} not found")


if __name__ == "__main__":
    main(sys.argv[1:])
