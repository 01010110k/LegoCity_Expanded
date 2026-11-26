"""datindex.py - read-only index listing of any GAME*.DAT / PATCH.DAT (Notes\70-release.md).
entries(path) -> {engine path (lower): dict(off, stored, raw, pm, md5_raw, md5_stored)}"""
import os, sys, struct
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from patchdat import parse_index

def read_index(path):
    with open(path, "rb") as f:
        first, ilen = struct.unpack("<iI", f.read(8))
        io = (-first) << 8 if first < 0 else first
        f.seek(io); index = f.read()
    return parse_index(index, 0)

def entries(path):
    ix = read_index(path)
    names, nodes = ix["names"], ix["nodes"]
    def nm(o):
        return "" if o == 0xFFFFFFFF else names[o:names.index(b"\0", o)].decode("latin1")
    out = {}
    for i, (c, s, o, par, fi) in enumerate(nodes):
        if i == 0 or c != 0:
            continue
        parts = [nm(o)]; p = par
        while p != 0:
            parts.append(nm(nodes[p][2])); p = nodes[p][3]
        off, stored, raw = ix["recs"][fi]
        out["\\".join(reversed(parts)).lower()] = dict(
            off=off, stored=stored, raw=raw & 0x7FFFFFFF, pm=ix["pack_mode"][fi], fi=fi, hash=ix["hashes"][fi],
            md5_raw=ix["after_read"][16 * fi:16 * fi + 16], md5_stored=ix["before_write"][16 * fi:16 * fi + 16])
    return out
