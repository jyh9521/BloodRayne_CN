#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
podtool.py — Terminal Reality POD3 archive unpacker / packer
Verified against BloodRayne: Terminal Cut (rayne1.exe, 2021 re-release).

POD3 layout
-----------
  0x000  char[4]   magic "POD3"
  0x004  u32       header CRC  = crc32_mpeg2(file[0x008:0x120])
  0x008  char[80]  comment
  0x058  u32       file count
  0x05C  u32       (audit/trail count — preserved verbatim)
  0x060  u32       revision
  0x064  u32       priority
  0x068  ...       reserved (preserved verbatim)
  0x108  u32       index offset
  0x10C  u32       (unknown CRC-like field — preserved verbatim)
  0x110  u32       name table size in bytes  ** must be recomputed on pack **
                   Getting this wrong is silent: CRCs still verify, but the
                   engine reads a truncated name table and refuses to mount
                   ("Cannot mount ...", PODMAIN.CPP). Confirmed against all
                   six shipped language archives plus PCART/STARTUP/WORLD.
  0x120  ...       file data, concatenated in index order
  <idx>  entry[n]  20 bytes each:
                     u32 name offset (relative to end of index)
                     u32 size
                     u32 offset
                     u32 unix timestamp
                     u32 crc32_mpeg2(data)
  <idx+n*20>        NUL-terminated name table
  <after names>     audit trail, 312 bytes per record (count at 0x05C):
                     char[32]  user name
                     u32       record timestamp
                     u32       action
                     char[264] file name
                     u32       file timestamp
                     u32       file size
                    Dev bookkeeping only; the engine ignores it. We copy it
                    through verbatim so repacks stay byte-identical.

Checksum: CRC-32/MPEG-2 — poly 0x04C11DB7, init 0xFFFFFFFF, no reflection,
no final XOR.

Usage
-----
  podtool.py list   <archive.pod>
  podtool.py unpack <archive.pod> <outdir>
  podtool.py pack   <indir> <out.pod> [--template <orig.pod>]
  podtool.py verify <archive.pod>
"""

import os
import struct
import sys
import time

HEADER_SIZE = 0x120
ENTRY_SIZE = 20

_CRC_TABLE = []
for _i in range(256):
    _c = _i << 24
    for _ in range(8):
        _c = ((_c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if _c & 0x80000000 else (_c << 1) & 0xFFFFFFFF
    _CRC_TABLE.append(_c)


def crc32_mpeg2(data, crc=0xFFFFFFFF):
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) ^ b) & 0xFF]
    return crc


class Pod3:
    def __init__(self, raw):
        if raw[:4] != b"POD3":
            raise ValueError("not a POD3 archive")
        self.raw = raw
        self.header = bytearray(raw[:HEADER_SIZE])
        n = struct.unpack_from("<I", raw, 0x58)[0]
        idx = struct.unpack_from("<I", raw, 0x108)[0]
        names = idx + n * ENTRY_SIZE
        self.entries = []
        name_end = names
        for i in range(n):
            no, size, off, ts, crc = struct.unpack_from("<IIIII", raw, idx + i * ENTRY_SIZE)
            end = raw.index(b"\x00", names + no)
            name_end = max(name_end, end + 1)
            name = raw[names + no:end].decode("latin-1")
            self.entries.append({
                "name": name, "offset": off, "size": size,
                "timestamp": ts, "crc": crc,
            })
        self.nametab = raw[names:name_end]
        self.audit = raw[name_end:]

    @classmethod
    def load(cls, path):
        with open(path, "rb") as f:
            return cls(f.read())

    def data(self, e):
        return self.raw[e["offset"]:e["offset"] + e["size"]]

    def comment(self):
        return self.header[8:0x58].split(b"\x00")[0].decode("latin-1")


def cmd_list(path):
    pod = Pod3.load(path)
    print("comment : %s" % pod.comment())
    print("files   : %d" % len(pod.entries))
    print()
    for e in pod.entries:
        print("%10d  %08X  %s" % (e["size"], e["crc"], e["name"]))


def cmd_verify(path):
    pod = Pod3.load(path)
    bad = 0
    for e in pod.entries:
        if crc32_mpeg2(pod.data(e)) != e["crc"]:
            print("BAD CRC: %s" % e["name"])
            bad += 1
    hdr = crc32_mpeg2(pod.raw[8:HEADER_SIZE])
    stored = struct.unpack_from("<I", pod.raw, 4)[0]
    hdr_ok = hdr == stored
    print("header CRC : %s (%08X vs %08X)" % ("OK" if hdr_ok else "BAD", hdr, stored))
    print("entry CRCs : %d/%d OK" % (len(pod.entries) - bad, len(pod.entries)))

    # Structural checks. CRCs cover the data but not the header's own
    # bookkeeping, so a wrong name table size verifies clean and still fails
    # to mount. Check it explicitly.
    n = struct.unpack_from("<I", pod.raw, 0x58)[0]
    idx = struct.unpack_from("<I", pod.raw, 0x108)[0]
    declared = struct.unpack_from("<I", pod.raw, 0x110)[0]
    actual = len(pod.nametab)
    size_ok = declared == actual
    print("name table : %s (declared %d, actual %d)"
          % ("OK" if size_ok else "MISMATCH -> the engine will refuse to mount",
             declared, actual))

    count_ok = n == len(pod.entries)
    idx_ok = idx == HEADER_SIZE + sum(e["size"] for e in pod.entries)
    print("file count : %s (%d)" % ("OK" if count_ok else "BAD", n))
    print("index off  : %s (%d)" % ("OK" if idx_ok else "BAD", idx))

    ok = hdr_ok and size_ok and count_ok and idx_ok and not bad
    print("=> %s" % ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


def cmd_unpack(path, outdir):
    pod = Pod3.load(path)
    manifest = []
    for e in pod.entries:
        rel = e["name"].replace("\\", "/")
        dest = os.path.join(outdir, rel)
        os.makedirs(os.path.dirname(dest) or ".", exist_ok=True)
        with open(dest, "wb") as f:
            f.write(pod.data(e))
        manifest.append("%s\t%d" % (e["name"], e["timestamp"]))
        print("  %10d  %s" % (e["size"], e["name"]))
    # Preserve original names (case + backslashes) and timestamps for repacking.
    with open(os.path.join(outdir, "_pod_manifest.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(manifest) + "\n")
    with open(os.path.join(outdir, "_pod_audit.bin"), "wb") as f:
        f.write(pod.audit)
    print("\n%d files -> %s" % (len(pod.entries), outdir))


def cmd_pack(indir, outpath, template=None):
    manifest_path = os.path.join(indir, "_pod_manifest.txt")
    order = []
    if os.path.exists(manifest_path):
        with open(manifest_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if not line:
                    continue
                name, _, ts = line.partition("\t")
                order.append((name, int(ts) if ts else int(time.time())))
    else:
        now = int(time.time())
        for root, _dirs, files in os.walk(indir):
            for fn in sorted(files):
                if fn.startswith("_pod_"):
                    continue
                full = os.path.join(root, fn)
                rel = os.path.relpath(full, indir).replace("/", "\\")
                order.append((rel, now))

    audit = b""
    audit_path = os.path.join(indir, "_pod_audit.bin")
    if os.path.exists(audit_path):
        with open(audit_path, "rb") as f:
            audit = f.read()

    if template:
        header = bytearray(Pod3.load(template).header)
    else:
        header = bytearray(HEADER_SIZE)
        header[:4] = b"POD3"

    blobs = []
    offset = HEADER_SIZE
    index = bytearray()
    nametab = bytearray()
    for name, ts in order:
        src = os.path.join(indir, name.replace("\\", "/"))
        with open(src, "rb") as f:
            data = f.read()
        index += struct.pack("<IIIII", len(nametab), len(data), offset,
                             ts, crc32_mpeg2(data))
        nametab += name.encode("latin-1") + b"\x00"
        blobs.append(data)
        offset += len(data)

    struct.pack_into("<I", header, 0x58, len(order))
    struct.pack_into("<I", header, 0x108, offset)
    struct.pack_into("<I", header, 0x110, len(nametab))
    struct.pack_into("<I", header, 4, crc32_mpeg2(bytes(header[8:HEADER_SIZE])))

    with open(outpath, "wb") as f:
        f.write(header)
        for b in blobs:
            f.write(b)
        f.write(index)
        f.write(nametab)
        f.write(audit)
    print("packed %d files -> %s (%d bytes)" % (len(order), outpath,
                                                os.path.getsize(outpath)))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "list":
        cmd_list(argv[2])
    elif cmd == "verify":
        return cmd_verify(argv[2])
    elif cmd == "unpack":
        cmd_unpack(argv[2], argv[3])
    elif cmd == "pack":
        tmpl = None
        if "--template" in argv:
            tmpl = argv[argv.index("--template") + 1]
        cmd_pack(argv[2], argv[3], tmpl)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
