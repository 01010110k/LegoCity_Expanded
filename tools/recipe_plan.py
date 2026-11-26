"""recipe_plan.py - measure PATCH.DAT against the stock GAME*.DAT and write the plan lcutool turns into patch.recipe.
Notes\\70-release.md.  Reads the game folder only.

    python -I recipe_plan.py <gameDir> <PATCH.DAT> <workDir> [--lcutool exe]

Writes <workDir>\\plan.tsv, the pool lists / pools (<workDir>\\pool\\<group>.bin, rebuilt only when their list
changes), runs `lcutool recipe`, and prints / writes the measurement (<workDir>\\measure.json).
"""
import os, sys, json, hashlib, subprocess, collections

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from datindex import entries

ARCHIVES = ["GAME.DAT"] + ["GAME%d.DAT" % i for i in range(9)]
# stock entries that may serve as delta sources, by extension.  .pak (12 GB of audio / character bundles) is left
# out: the mod's new .pak files are its own NPC voice banks, which have nothing in common with stock bytes.
GROUPS = {
    "tex": {".nxg_textures", ".tex"},
    "gsc": {".gsc"},
    "shd": {".pc_shaders"},
    "misc": {".dno", ".led", ".res", ".txt", ".csv", ".sf", ".cpj", ".glm", ".giz", ".cd", ".as", ".cpd", ".bin",
             ".anm", ".an3", ".ai2", ".ats", ".scp", ".cfg", ".ini", ".xml", ".fnt", ".ghg", ".cut", ".cbx"},
}
EXT_GROUP = {x: g for g, xs in GROUPS.items() for x in xs}

def key_md5(e):
    """md5 of the decoded bytes: the index's 'after read' array, except where it is zero (the .res entries)."""
    return e["md5_stored"] if e["md5_raw"] == bytes(16) else e["md5_raw"]

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    game, patch, work = args[:3]
    lcutool = os.path.join(HERE, "native", "out", "lcutool.exe")
    if "--lcutool" in sys.argv:
        lcutool = sys.argv[sys.argv.index("--lcutool") + 1]
    os.makedirs(os.path.join(work, "pool"), exist_ok=True)

    P = entries(patch)
    stock = {}                                   # path -> (arc index, entry)
    for ai, a in enumerate(ARCHIVES):
        for k, e in entries(os.path.join(game, a)).items():
            stock[k] = (ai, e)                   # later archives win (no duplicates in this build anyway)
    by_md5 = {}
    for k, (ai, e) in stock.items():
        by_md5.setdefault(key_md5(e), (k, ai, e))

    # ---- sources: every pooled stock entry, then copy sources not already pooled
    src_ids = {}                                 # stock path -> id
    sources = []                                 # (arc, fi, raw, md5, group)
    pools = collections.defaultdict(list)
    for k, (ai, e) in sorted(stock.items(), key=lambda kv: (kv[1][0], kv[1][1]["off"])):
        g = EXT_GROUP.get(os.path.splitext(k)[1])
        if g:
            src_ids[k] = len(sources); sources.append([ai, e["fi"], e["raw"], key_md5(e), g, 0]); pools[g].append(src_ids[k])
    def src_of(k):
        if k not in src_ids:
            ai, e = stock[k]
            src_ids[k] = len(sources); sources.append([ai, e["fi"], e["raw"], key_md5(e), "", 0])
        return src_ids[k]

    cls = {}
    targets = []
    for k, e in P.items():
        md5 = e["md5_stored"]                    # PATCH entries are stored: stored == raw
        ext = os.path.splitext(k)[1]
        if k in stock and key_md5(stock[k][1]) == md5:
            c, mode, arg = "identical", "copy", src_of(k)
        elif md5 in by_md5:
            c, mode, arg = "identical_other_path", "copy", src_of(by_md5[md5][0])
        elif k in stock:
            c = "modified"; mode, arg = ("diff", EXT_GROUP[ext]) if ext in EXT_GROUP else ("lit", "")
        else:
            c = "new"; mode, arg = ("diff", EXT_GROUP[ext]) if ext in EXT_GROUP else ("lit", "")
        cls[k] = c
        targets.append((e["off"], e["raw"], mode, arg, k))

    # ---- pools
    for g, ids in pools.items():
        offs = 0
        lines = []
        for i in ids:
            sources[i][5] = offs; offs += sources[i][2]
            lines.append("%d\t%d\t%d\t%s" % (sources[i][0], sources[i][1], sources[i][2], sources[i][3].hex()))
        lst = os.path.join(work, "pool", g + ".tsv")
        text = "\n".join(lines) + "\n"
        binp = os.path.join(work, "pool", g + ".bin")
        stamp = hashlib.sha256(text.encode()).hexdigest()
        okp = binp + ".ok"
        if os.path.exists(binp) and os.path.exists(okp) and open(okp).read() == stamp and os.path.getsize(binp) == offs:
            print("pool %s: cached (%.2f GB)" % (g, offs / 1e9))
        else:
            open(lst, "w").write(text)
            print("pool %s: decoding %d entries (%.2f GB)..." % (g, len(ids), offs / 1e9))
            subprocess.run([lcutool, "pool", game, lst, binp], check=True)
            open(okp, "w").write(stamp)

    plan = os.path.join(work, "plan.tsv")
    with open(plan, "w", newline="\n") as f:
        for a in ARCHIVES:
            f.write("archive\t%s\n" % a)
        for i, (ai, fi, raw, md5, g, po) in enumerate(sources):
            f.write("source\t%d\t%d\t%d\t%d\t%s\t%s\t%d\n" % (i, ai, fi, raw, md5.hex(), g, po))
        for g in pools:
            f.write("pool\t%s\t%s\n" % (g, os.path.join(work, "pool", g + ".bin")))
        for off, raw, mode, arg, k in targets:
            f.write("target\t%d\t%d\t%s\t%s\n" % (off, raw, mode, arg))
    recipe = os.path.join(work, "patch.recipe")
    subprocess.run([lcutool, "recipe", plan, patch, recipe], check=True)

    # ---- measurement
    stats = {}
    for line in open(recipe + ".stats.tsv"):
        if line.startswith("#"):
            continue
        off, size, mode, c, co, x, a = line.split("\t")
        stats[int(off)] = (int(c), int(co), int(x), int(a))
    by_off = {e["off"]: k for k, e in P.items()}
    m = collections.defaultdict(lambda: dict(files=0, bytes=0, copy=0, copyout=0, xdiff=0, add=0))
    for off, (c, co, x, a) in stats.items():
        k = by_off[off]; r = m[cls[k]]
        r["files"] += 1; r["bytes"] += P[k]["raw"]; r["copy"] += c; r["copyout"] += co; r["xdiff"] += x; r["add"] += a
    out = dict(patch_files=len(P), patch_bytes=os.path.getsize(patch), recipe_bytes=os.path.getsize(recipe), classes=m)
    json.dump(out, open(os.path.join(work, "measure.json"), "w"), indent=1)
    for c, r in m.items():
        print("%-22s %5d files %9.1f MB | from game %8.1f MB, from earlier PATCH files %7.1f MB, xdiff %6.1f MB, new bytes %7.1f MB"
              % (c, r["files"], r["bytes"] / 1e6, r["copy"] / 1e6, r["copyout"] / 1e6, r["xdiff"] / 1e6, r["add"] / 1e6))
    print("recipe %.1f MB (uncompressed) for a %.1f MB PATCH.DAT" % (out["recipe_bytes"] / 1e6, out["patch_bytes"] / 1e6))

if __name__ == "__main__":
    main()
