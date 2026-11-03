"""Parse Rayman 3 .cnt texture containers and dump .gf texture headers. Read-only.

Format from BinarySerializer.OpenSpace (MIT) - CNT.cs / CNT_File.cs / CNT_String.cs / GFFileHeader.cs:
  u32 dirCount, u32 fileCount, u8 isXorUsed, u8 isChecksumUsed, u8 stringsXorKey
  dirs : int32 len + len bytes (xor stringsXorKey)  ... then 1 checksum byte
  files: int32 dirIndex, int32 nameLen + name bytes (xor), u8[4] xorKey, u32 checksum, u32 offset, u32 size
  file payload is xor'd with xorKey over (size - size % 4) bytes.
GF header (Rayman 3, non-Montreal): u32 format, i32 width, i32 height, u8 bytesPerPixel,
  u8 mipmapLevels, u8 rleCode, ...

usage: python analysis/tools/r3tex.py <cnt> [--nframes N] [--list]
"""
import os, struct, sys

G = r"C:\Program Files\GOG Galaxy\Games\Rayman 3\Gamedatabin"


def cstr(b, off, key):
    n = struct.unpack_from("<i", b, off)[0]
    off += 4
    s = bytes(x ^ key for x in b[off:off + n])
    return s.decode("cp1252", "replace"), off + n


def parse_cnt(path):
    b = open(path, "rb").read()
    dirs_n, files_n = struct.unpack_from("<2I", b, 0)
    is_xor, is_sum, key = b[8], b[9], b[10]
    off = 11
    dirs = []
    for _ in range(dirs_n):
        s, off = cstr(b, off, key if is_xor else 0)
        dirs.append(s)
    off += 1  # directories checksum byte
    files = []
    for _ in range(files_n):
        di = struct.unpack_from("<i", b, off)[0]
        off += 4
        name, off = cstr(b, off, key if is_xor else 0)
        xk = b[off:off + 4]; off += 4
        csum, foff, fsize = struct.unpack_from("<3I", b, off); off += 12
        files.append(dict(dir=di, name=name, xor=xk, checksum=csum, offset=foff, size=fsize))
    return b, dirs, files, is_xor, is_sum, key


def gf_header(b, f):
    raw = b[f["offset"]:f["offset"] + f["size"]]
    lim = len(raw) - (len(raw) % 4)
    data = bytes(c ^ f["xor"][i % 4] for i, c in enumerate(raw[:lim])) + raw[lim:]
    fmt, w, h = struct.unpack_from("<Iii", data, 0)
    bpp = data[12]
    mips = data[13]
    rle = data[14]
    return dict(format=fmt, width=w, height=h, bpp=bpp, mipmaps=mips, rle=rle,
                bytes=len(raw), head=data[:24].hex(" "))


def main(a):
    cnt = a[0] if a and not a[0].startswith("--") else os.path.join(G, "vignette.cnt")
    n = int(a[a.index("--nframes") + 1]) if "--nframes" in a else 8
    b, dirs, files, xor, csum, key = parse_cnt(cnt)
    print(f"{os.path.basename(cnt)}: dirs={len(dirs)} files={len(files)} xor={xor} checksum={csum} key=0x{key:02x}")
    for d in dirs[:5]:
        print("  dir:", d)
    if "--list" in a:
        for f in files:
            print(f"  {f['dir']:3d} {f['name']:<28} off=0x{f['offset']:08x} size={f['size']:8d}")
    gfs = [f for f in files if f["name"].lower().endswith(".gf")]
    print(f"  .gf entries: {len(gfs)}")
    for f in gfs[:n]:
        try:
            h = gf_header(b, f)
            print(f"  {f['name']:<26} {h['width']}x{h['height']} bpp={h['bpp']} fmt=0x{h['format']:x} "
                  f"mips={h['mipmaps']} rle={h['rle']} size={h['bytes']}")
        except Exception as e:
            print("  ", f["name"], "parse failed:", e)


if __name__ == "__main__":
    main(sys.argv[1:])
