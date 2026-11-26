"""
patchdat.py - build a PATCH.DAT for LEGO City Undercover.  Written 2026-10-25 (Notes\39-world-expansion.md).

Why it works
------------
At boot the archive manager (0x140302B50, run as the "Dat::OpenBackground" job; manager object at
*(void**)0x141AA1568) mounts, in this order:
    upd:PATCH / PATCH / PATCHSTREAM  .DAT, then PATCH0..PATCH18     (+0x258 = 0, "patch")
    GAME8 .. GAME0, GAME  (reverse order, found by probing GAME, GAME0, GAME1 ... until one is missing)
    STREAM0 ..
then merges every file table into one hash-sorted lookup (0x140301E50 -> 0x140306770).  A PATCH.DAT
next to the exe is therefore picked up with no code change, and deleting it is the whole uninstall.
Lookups (0x140302620 -> 0x140B4D830 -> 0x140B4D780) are a binary search on a 32-bit PATH HASH only;
the name tree is never consulted to find a file.

Archive layout (all index integers big-endian; verified field-by-field against the engine's own
serializers 0x1403075E0 / 0x140B4E6F0 / 0x140B4E000 and byte-for-byte against GAME8.DAT)
-------------------------------------------------------------------------------------------
    0x00  int32 LE  -(indexOffset >> 8)     (a positive value would be an absolute offset)
    0x04  u32  LE   index length (index runs to EOF)
    0x08  zeros up to 0x108 (the engine reads a 0x108-byte header)
    then, for every file, back to back with no padding:
          24-byte local header, LE: u64 storedSize, u64 rawSize, u32 packMode, u32 flag
          (flag 0 = streamed media: .fmv, music .ogg, voice .pak; 1 = everything else)
          the payload; the index's file offset points HERE, just past the local header
          (first payload at 0x120 in every shipped archive)
    zeros to the next 256-byte boundary
    index (256-aligned):
        u32 chunkSize (= index length - 4), b".CC4", b"0TAD"          ('4CC.' 'DAT0' reversed)
        m_version  = -11
        tree: version 2, m_fileCount, m_nodeCount, m_leafNameSize, leaf names (each name + b"\0\0"),
              nodes x12 {u16 childix (LAST child, 0 = leaf), u16 sibix (PREVIOUS sibling, 0 = none),
                         u32 name offset (0xFFFFFFFF for the root), u16 parentix, u16 fileIndex},
              u32 noHash = 0
        files: version -11, m_nFiles, n x {u64 offset, u32 storedSize, u32 rawSize | 0x80000000 if packed}
        m_hashTable: n x u32, ASCENDING; file i <-> hash i (the file table is in hash order)
        m_duplicateCount = 0, m_duplicatesTableSize = 0
        m_commonRootPath: NUL-terminated ("" or e.g. "levels"; it does NOT enter the hash)
        16 zero bytes
        m_checksumsAfterRead : u32 0, u32 n, n x MD5(uncompressed bytes)
        m_checksumsBeforeWrite: u32 0, u32 n, n x MD5(stored bytes)
        m_packMode           : u32 0, u32 n, n x u8   (0 = stored, 2 = LZ2K, 6 = ZIPX/RC4 text)
        m_packedFileSize     : u32 0, u32 n, n x u32  (= storedSize)
    This builder writes every entry stored (mode 0, flag 1), a combination 6,000+ shipped files use.

Path hash (port of 0x140B4DC80): FNV-1a offset basis 0x811C9DC5 with prime 0x199933 (NOT 0x01000193),
over the path uppercased, with each run of '\\' or '/' collapsed to one '\\' and leading separators
dropped.  Engine paths look like  stuff\\text\\text.csv  or  levels\\lego_city\\lego_city\\lego_city.txt.

Usage
-----
    python patchdat.py build  <srcdir> <out.dat>    every file under srcdir, at its relative path
    python patchdat.py verify <file.dat>            re-parse and check hashes, offsets, headers and MD5s
    python patchdat.py selftest <GAMEn.DAT> ...     round-trip a shipped index through our parser/writer
    python patchdat.py hash   <engine path> ...     print the engine hash of a path
"""
import os, sys, struct, hashlib

def tt_path_hash(path):
    h = 0x811C9DC5
    s = path.encode("latin1")
    n = len(s); i = 0
    while i < n:
        j = i; sep = False
        while j < n and s[j] in (0x5C, 0x2F):
            sep = True; j += 1
        if j >= n:
            break
        if sep and i != 0:
            c = 0x5C; i = j
        else:
            c = s[j]
            if 0x61 <= c <= 0x7A or c >= 0xE0:
                c = (c + 0xE0) & 0xFF
            i = j + 1
        if c >= 0x80:
            c -= 0x100                      # the engine sign-extends (movsx)
        h = ((h ^ (c & 0xFFFFFFFF)) * 0x199933) & 0xFFFFFFFF
    return h

def norm(path):
    return "\\".join(p for p in path.replace("/", "\\").split("\\") if p).lower()

def build(files, out_path, root=""):
    """files: {engine path: bytes}.  Writes a PATCH-style archive with every entry stored."""
    items = sorted(((tt_path_hash(norm(p)), norm(p), data) for p, data in files.items()))
    for a, b in zip(items, items[1:]):
        if a[0] == b[0]:
            raise ValueError("path hash collision (duplicates table not supported): %s / %s" % (a[1], b[1]))
    n = len(items)
    file_index = {p: i for i, (h, p, d) in enumerate(items)}

    # directory tree: node 0 = root; pre-order, a directory's subtree right after it
    tree = {}
    for h, p, d in items:
        cur = tree
        parts = p.split("\\")
        for part in parts[:-1]:
            cur = cur.setdefault(part, {})
        cur[parts[-1]] = None
    nodes = [[0, 0, 0xFFFFFFFF, 0, 0]]          # childix, sibix, nameoff, parentix, fileIndex
    names = bytearray()
    def emit(subtree, parent, prefix):
        prev = 0
        for name in sorted(subtree, key=lambda k: (subtree[k] is not None, k)):
            idx = len(nodes)
            off = len(names); names.extend(name.encode("latin1") + b"\0\0")
            full = prefix + name
            fi = file_index[full] if subtree[name] is None else 0
            nodes.append([0, prev, off, parent, fi])
            prev = idx
            if subtree[name] is not None:
                emit(subtree[name], idx, full + "\\")
        nodes[parent][0] = prev                 # childix = LAST child
    emit(tree, 0, "")
    if len(nodes) > 0xFFFF or n > 0xFFFF:
        raise ValueError("too many nodes/files for u16 indices")

    # payloads, each behind a 24-byte local header (stored, raw, packMode 0, flag 1)
    blob = bytearray(b"\0" * 0x108)
    recs = []
    for h, p, d in items:
        blob += struct.pack("<QQII", len(d), len(d), 0, 1)
        recs.append((len(blob), len(d), len(d)))
        blob += d
    blob += b"\0" * (-len(blob) % 0x100)
    index_off = len(blob)

    md5s = b"".join(hashlib.md5(d).digest() for h, p, d in items)
    index = serialize_index(dict(
        names=bytes(names), nodes=[tuple(x) for x in nodes], recs=recs,   # never packed: top bit clear
        hashes=[h for h, p, d in items], dup_count=0, dup_table=b"", root=root.encode("latin1"),
        mdata=bytes(16), after_read=md5s, before_write=md5s,                # stored == raw here
        pack_mode=bytes(n), packed_size=[s for o, s, r in recs]))

    blob[0:8] = struct.pack("<iI", -(index_off >> 8), len(index))
    blob += index
    with open(out_path, "wb") as f:
        f.write(blob)
    return dict(files=n, nodes=len(nodes), size=len(blob), index_off=index_off)

def serialize_index(ix):
    """Index chunk bytes (size prefix included) from the parts parse_index() returns."""
    n = len(ix["recs"])
    out = bytearray(b".CC40TAD")
    out += struct.pack(">iIIII", -11, 2, n, len(ix["nodes"]), len(ix["names"]))
    out += ix["names"]
    for c, s, o, par, fi in ix["nodes"]:
        out += struct.pack(">HHIHH", c, s, o, par, fi)
    out += struct.pack(">I", 0)                         # noHash
    out += struct.pack(">iI", -11, n)
    for off, stored, raw in ix["recs"]:
        out += struct.pack(">QII", off, stored, raw)
    out += b"".join(struct.pack(">I", h) for h in ix["hashes"])
    out += struct.pack(">II", ix["dup_count"], len(ix["dup_table"])) + ix["dup_table"]
    out += ix["root"] + b"\0"                           # m_commonRootPath
    out += ix["mdata"]
    out += struct.pack(">II", 0, n) + ix["after_read"]
    out += struct.pack(">II", 0, n) + ix["before_write"]
    out += struct.pack(">II", 0, n) + ix["pack_mode"]
    out += struct.pack(">II", 0, n) + b"".join(struct.pack(">I", s) for s in ix["packed_size"])
    return struct.pack(">I", len(out)) + bytes(out)

def parse_index(d, io):
    """Parse the index chunk starting at offset io of buffer d (it must run to the end of d)."""
    size, = struct.unpack_from(">I", d, io)
    assert io + 4 + size == len(d), "index size field does not reach the end"
    assert d[io + 4:io + 12] == b".CC40TAD", "bad index signature"
    q = io + 12
    ver, tver, nf, nn, nl = struct.unpack_from(">iIIII", d, q); q += 20
    assert (ver, tver) == (-11, 2), "unsupported index version"
    names = d[q:q + nl]; q += nl
    nodes = [struct.unpack_from(">HHIHH", d, q + 12 * i) for i in range(nn)]; q += 12 * nn
    q += 4                                              # noHash
    fver, n = struct.unpack_from(">iI", d, q); q += 8
    assert fver == -11 and n == nf
    recs = [struct.unpack_from(">QII", d, q + 16 * i) for i in range(n)]; q += 16 * n
    hashes = list(struct.unpack_from(">%dI" % n, d, q)); q += 4 * n
    assert hashes == sorted(hashes), "hash table not ascending"
    dc, ds = struct.unpack_from(">II", d, q); q += 8
    dup = d[q:q + ds]; q += ds
    e = d.index(b"\0", q); root = d[q:e]; q = e + 1
    mdata = d[q:q + 16]; q += 16
    arrays = []
    for esz in (16, 16, 1, 4):
        tag, cnt = struct.unpack_from(">II", d, q); q += 8
        assert tag == 0 and cnt == n, "unexpected array header"
        arrays.append(d[q:q + cnt * esz]); q += cnt * esz
    assert q == len(d), "trailing bytes after the index"
    return dict(names=names, nodes=nodes, recs=recs, hashes=hashes, dup_count=dc, dup_table=dup,
                root=root, mdata=mdata, after_read=arrays[0], before_write=arrays[1], pack_mode=arrays[2],
                packed_size=list(struct.unpack(">%dI" % n, arrays[3])))

def selftest(dat_path):
    """Round-trip a shipped archive's index through parse_index/serialize_index; byte-exact or bust."""
    with open(dat_path, "rb") as f:
        first, ilen = struct.unpack("<iI", f.read(8))
        io = (-first) << 8 if first < 0 else first
        f.seek(io); index = f.read()
    assert len(index) == ilen, "index length field does not reach EOF"
    again = serialize_index(parse_index(index, 0))
    return again == index, len(index)

def verify(path, expect=None):
    """Re-parse an archive the way the engine does; returns {engine path: bytes}."""
    d = open(path, "rb").read()
    first, ilen = struct.unpack_from("<iI", d, 0)
    io = (-first) << 8 if first < 0 else first
    assert io + ilen == len(d), "index length / file size mismatch"
    ix = parse_index(d, io)
    names, nodes, recs, hashes = ix["names"], ix["nodes"], ix["recs"], ix["hashes"]
    n = len(recs)
    arrays = [ix["after_read"], ix["before_write"], ix["pack_mode"]]
    def nm(o):
        return None if o == 0xFFFFFFFF else names[o:names.index(b"\0", o)].decode("latin1")
    out = {}
    for i, (c, s, o, par, fi) in enumerate(nodes):
        if i == 0 or c != 0:
            continue
        parts = [nm(o)]; p = par
        while p != 0:
            parts.append(nm(nodes[p][2])); p = nodes[p][3]
        ep = "\\".join(reversed(parts))
        off, stored, raw = recs[fi]
        data = d[off:off + stored]
        assert raw & 0x7FFFFFFF == len(data)
        ls, lr, lm, lf = struct.unpack_from("<QQII", d, off - 24)
        assert (ls, lr, lm) == (stored, raw & 0x7FFFFFFF, arrays[2][fi]) and lf in (0, 1), \
            "local header mismatch for " + ep
        assert tt_path_hash(ep) == hashes[fi], "hash mismatch for " + ep
        assert hashlib.md5(data).digest() == arrays[1][16 * fi:16 * fi + 16], "MD5 mismatch for " + ep
        out[ep] = data
    assert len(out) == n, "tree does not reach every file"
    if expect is not None:
        for p, data in expect.items():
            assert out.get(norm(p)) == data, "content mismatch for " + p
    return out

def walk(srcdir):
    files = {}
    for dp, dn, fn in os.walk(srcdir):
        for f in fn:
            full = os.path.join(dp, f)
            files[os.path.relpath(full, srcdir)] = open(full, "rb").read()
    return files

if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "build":
        files = walk(sys.argv[2])
        info = build(files, sys.argv[3])
        verify(sys.argv[3], files)
        print("built %s: %d files, %d nodes, %d bytes (index at 0x%X); verified"
              % (sys.argv[3], info["files"], info["nodes"], info["size"], info["index_off"]))
        for p in sorted(files):
            print("   %-70s %08X %d bytes" % (norm(p), tt_path_hash(norm(p)), len(files[p])))
    elif len(sys.argv) >= 3 and sys.argv[1] == "verify":
        out = verify(sys.argv[2])
        print("OK: %d files" % len(out))
        for p in sorted(out):
            print("   %-70s %08X %d bytes" % (p, tt_path_hash(p), len(out[p])))
    elif len(sys.argv) >= 3 and sys.argv[1] == "selftest":
        for p in sys.argv[2:]:
            ok, size = selftest(p)
            print("%-60s index %d bytes: %s" % (p, size, "byte-exact round trip" if ok else "MISMATCH"))
    elif len(sys.argv) >= 3 and sys.argv[1] == "hash":
        for p in sys.argv[2:]:
            print("%08X  %s" % (tt_path_hash(norm(p)), norm(p)))
    else:
        print(__doc__)
