"""make_release.py - regenerate the whole public release from the toolkit in one command (Notes\\70-release.md).

    python -I Tools\\release\\make_release.py [--version 0.1.0] [--out D:\\release_out] [--work D:\\release_work]
                                              [--game <game folder, read only>] [--skip-verify] [--no-build] [--no-repo]

What it does (the game folder is only READ):
  1. builds Tools\\release\\native (lcutool.exe, Setup.exe)
  2. checks the inputs: the game exe = the shipped Steam build, the installed PATCH.DAT (its sha is the release's)
  3. patch.recipe: recipe_plan.py (stock pools cached in <work>\\pool), then `lcutool apply` rebuilds PATCH.DAT from the
     game archives and must give the same sha256 (cached by a stamp)
  4. the three payloads (core / graphics / radio .lcupak, split below 1.9 GiB for GitHub), Setup.exe, docs, licences,
     SHA256SUMS.txt, RELEASE_NOTES.md in <out>\\LCUMod-<version>\\
  5. the source repo folder <out>\\repo (git init + one local commit, no remote)
  6. the personal-data scan over everything produced (and every file packed) -> <work>\\scan_report.txt; fails on a hit
"""
import os, sys, re, json, shutil, hashlib, struct, subprocess, argparse, time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
TOOLKIT = os.path.dirname(TOOLS)
sys.path.insert(0, HERE)
from datindex import read_index, entries

DRIVE = os.path.join(TOOLKIT, "Drive")
STAGE = os.path.join(DRIVE, "staging", "release1")          # the DLL + proxy as they ship (MERGE.md there)
RADIO = os.path.join(TOOLKIT, "Radio")
NATIVE = os.path.join(HERE, "native")
DOCS = os.path.join(HERE, "docs")
LCUTOOL = os.path.join(NATIVE, "out", "lcutool.exe")
SETUP = os.path.join(NATIVE, "out", "Setup.exe")
DEFAULT_GAME = r"C:\Program Files (x86)\Steam\steamapps\common\LEGO City Undercover"
ARCHIVES = ["GAME.DAT"] + ["GAME%d.DAT" % i for i in range(9)]
PART = 1900 * 1024 * 1024                                     # GitHub release assets must stay under 2 GiB
MOD_NAME = "LegoCity Expanded"                                # public name (2026-11-25: github.com/01010110k/LegoCity_Expanded)
AUTHOR = "Victor Knai"                                        # the author's public name for LICENSE / credits / commits (2026-11-25)
AUTHOR_EMAIL = os.environ.get("LCU_AUTHOR_EMAIL", "01010110k@users.noreply.github.com")   # GitHub noreply, never a real address

def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            h.update(b)
    return h.hexdigest()

def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)

def run(cmd, **kw):
    log(">", " ".join('"%s"' % c if " " in c else c for c in cmd))
    subprocess.run(cmd, check=True, **kw)

# --------------------------------------------------------------------------------------------- inputs ---
def check_inputs(game):
    man = json.load(open(os.path.join(TOOLKIT, "GameFolderSnapshot", "original-manifest.json"), encoding="utf-8-sig"))
    stock = {m["Name"]: m for m in man}
    exe = os.path.join(game, "LEGOLCUR_DX11.exe")
    s = sha256(exe)
    assert s.upper() == stock["LEGOLCUR_DX11.exe"]["SHA256"], "the game exe is not the snapshot's Steam build"
    return stock

def dat_index_sha(path):
    with open(path, "rb") as f:
        head = f.read(8)
        first, ilen = struct.unpack("<iI", head)
        io = (-first) << 8 if first < 0 else first
        f.seek(io); ix = f.read()
    return hashlib.sha256(head + ix).hexdigest(), os.path.getsize(path)

# --------------------------------------------------------------------------------------------- recipe ---
def make_recipe(game, patch, work, patch_sha, skip_verify):
    recipe = os.path.join(work, "patch.recipe")
    stamp = os.path.join(work, "patch.recipe.for")
    if not (os.path.exists(recipe) and os.path.exists(stamp) and open(stamp).read() == patch_sha):
        run([sys.executable, "-I", os.path.join(HERE, "recipe_plan.py"), game, patch, work])
        open(stamp, "w").write(patch_sha)
    else:
        log("recipe: cached for PATCH.DAT", patch_sha[:12])
    ok = os.path.join(work, "patch.recipe.verified")
    rs = sha256(recipe)
    if skip_verify:
        log("recipe: verification SKIPPED (--skip-verify)")
    elif os.path.exists(ok) and open(ok).read() == rs:
        log("recipe: verified earlier (apply -> sha matched)")
    else:
        out = os.path.join(work, "PATCH.apply.DAT")
        run([LCUTOOL, "apply", game, recipe, out])
        got = sha256(out)
        assert got == patch_sha, "the recipe does not rebuild PATCH.DAT (%s != %s)" % (got, patch_sha)
        os.remove(out)
        open(ok, "w").write(rs)
        log("recipe: apply rebuilt PATCH.DAT byte for byte (sha256 %s)" % got)
    return recipe

# --------------------------------------------------------------------------------------------- payload ---
def release_ini(src, dst):
    """The public lcudrive.ini: the clean shipped defaults, telemetry off, the radio beside the DLL."""
    t = open(src, encoding="utf-8").read()
    n1 = len(re.findall(r"(?m)^telemetry = 1", t))
    t = re.sub(r"(?m)^telemetry = 1", "telemetry = 0", t)
    t2 = re.sub(r"(?m)^(manifest\s*=\s*)\.\.\\Radio\\radio_runtime\.tsv", r"\1Radio\\radio_runtime.tsv", t)
    assert n1 == 1 and t2 != t, "release_ini: anchors not found"
    open(dst, "w", encoding="utf-8", newline="").write(t2)

def release_d3dx(src, dst):
    t = open(src, encoding="utf-8", errors="strict").read()
    t2, n = re.subn(r"(?m)^dump_usage=1", "dump_usage=0", t)
    assert n == 1, "release_d3dx: dump_usage anchor"
    open(dst, "w", encoding="utf-8", newline="").write(t2)

def walk(root, prefix):
    out = []
    for dp, dn, fn in os.walk(root):
        dn.sort()
        for f in sorted(fn):
            full = os.path.join(dp, f)
            out.append((full, prefix + "\\" + os.path.relpath(full, root)))
    return out

def build_lists(game, work, recipe, patch, version):
    stage = os.path.join(work, "stage"); os.makedirs(stage, exist_ok=True)
    src = os.path.join(STAGE, "src")
    core, graphics, radio = [], [], []
    # core
    core.append((os.path.join(STAGE, "dinput8.dll"), "dinput8.dll"))
    core.append((os.path.join(STAGE, "lcudrive.dll"), "LCUMod\\lcudrive.dll"))
    ini = os.path.join(stage, "lcudrive.ini"); release_ini(os.path.join(src, "test", "lcudrive.shipped.ini"), ini)
    core.append((ini, "LCUMod\\lcudrive.ini"))
    for f in ["lcudrive_spots.txt", "lcudrive_animals.txt", "lcudrive_weather.txt", "npc_runtime.tsv", "radio_provisional.tsv"]:
        core.append((os.path.join(src, f), "LCUMod\\" + f))
    # npc_vo: byte copies of PATCH.DAT entries -> Setup copies them out of the PATCH.DAT it built
    P = entries(patch)
    by_sha = {}
    with open(patch, "rb") as f:
        for k, e in P.items():
            if k.startswith("audio\\samples\\vo\\"):
                f.seek(e["off"]); d = f.read(e["raw"])
                by_sha[hashlib.sha256(d).hexdigest()] = (e["off"], e["raw"])
    frompatch = []
    vo = os.path.join(DRIVE, "npc_vo")
    for n in sorted(os.listdir(vo)):
        p = os.path.join(vo, n); s = sha256(p)
        if s in by_sha:
            frompatch.append(("LCUMod\\npc_vo\\" + n, by_sha[s][0], by_sha[s][1], s, "core"))
        else:
            core.append((p, "LCUMod\\npc_vo\\" + n))
    log("npc_vo: %d files from PATCH.DAT, %d packed" % (len(frompatch), sum(1 for c in core if "npc_vo" in c[1])))
    # graphics
    d3d = os.path.join(game, "d3d11.dll")
    assert sha256(d3d) == sha256(os.path.join(TOOLS, "3Dmigoto", "x64", "d3d11.dll")), "d3d11.dll is not stock 3Dmigoto 1.3.16 x64"
    graphics.append((d3d, "d3d11.dll"))
    dx = os.path.join(stage, "d3dx.ini"); release_d3dx(os.path.join(game, "d3dx.ini"), dx)
    graphics.append((dx, "d3dx.ini"))
    graphics += walk(os.path.join(game, "ShaderFixes"), "ShaderFixes")
    graphics += walk(os.path.join(game, "Mods", "Rain"), "Mods\\Rain")
    graphics += walk(os.path.join(game, "Mods", "RadioLogo"), "Mods\\RadioLogo")
    # radio: the manifest and every clip it lists
    man = os.path.join(RADIO, "radio_runtime.tsv")
    radio.append((man, "LCUMod\\Radio\\radio_runtime.tsv"))
    seen = set()
    for line in open(man, encoding="utf-8"):
        c = line.rstrip("\r\n").split("\t")
        if c[0] == "C" and len(c) > 4 and c[4] not in seen:
            seen.add(c[4])
            p = os.path.join(RADIO, c[4].replace("/", "\\"))
            assert os.path.exists(p), "radio clip missing: " + c[4]
            radio.append((p, "LCUMod\\Radio\\" + c[4].replace("/", "\\")))
    return core, graphics, radio, frompatch

def write_release_txt(path, version, stock, game, patch, patch_sha, comps, frompatch):
    L = ["# release.txt - read by Setup.exe (Notes\\70-release.md)",
         "name\t%s" % MOD_NAME, "version\t%s" % version,
         "exe\tLEGOLCUR_DX11.exe\t%d\t%s" % (stock["LEGOLCUR_DX11.exe"]["Bytes"], stock["LEGOLCUR_DX11.exe"]["SHA256"].lower())]
    for a in ARCHIVES:
        ish, size = dat_index_sha(os.path.join(game, a))
        assert size == stock[a]["Bytes"]
        L.append("dat\t%s\t%d\t%s\t%s" % (a, size, ish, stock[a]["SHA256"].lower()))
    L.append("patch\t%d\t%s" % (os.path.getsize(patch), patch_sha))
    titles = {"core": "Core: the mod, its world (PATCH.DAT) and street voices - required",
              "graphics": "Graphics pack: day and night, lights, rain, on-screen meters (3Dmigoto)",
              "radio": "Radio stations"}
    for cid, nbytes in comps:
        L.append("component\t%s\t%s\t%d\t1" % (cid, titles[cid], nbytes))
    for p, off, size, s, comp in frompatch:
        L.append("frompatch\t%s\t%d\t%d\t%s\t%s" % (p, off, size, s, comp))
    for n in ["dxgi.dll", "ReShade.ini", "ReShadePreset.ini", "reshade-shaders", "d3dx_user.ini", "d3d9.dll", "nvapi64.dll"]:
        L.append("injector\t" + n)
    for n in ["d3d11_log.txt", "d3dx_user.ini", "ShaderUsage.txt"]:
        L.append("generated\t" + n)
    open(path, "w", newline="\n").write("\n".join(L) + "\n")

def pack(lst, out, work, name):
    """lcutool pack, cached by a stamp of (paths, sizes, mtimes); split above PART."""
    tsv = os.path.join(work, "stage", name + ".tsv")
    text = "".join("%s\t%s\n" % (s, d) for s, d in lst)
    stamp_v = hashlib.sha256((text + "".join("%d %d\n" % (os.path.getsize(s), os.stat(s).st_mtime_ns) for s, d in lst)).encode()).hexdigest()
    cache = os.path.join(work, "paks", name + ".lcupak")
    os.makedirs(os.path.dirname(cache), exist_ok=True)
    if os.path.exists(cache) and os.path.exists(cache + ".stamp") and open(cache + ".stamp").read() == stamp_v:
        log("pack %s: cached" % name)
    else:
        open(tsv, "w", encoding="utf-8", newline="\n").write(text)
        run([LCUTOOL, "pack", tsv, cache])
        open(cache + ".stamp", "w").write(stamp_v)
    size = os.path.getsize(cache)
    outs = []
    if size > PART:
        tmp = out
        shutil.copyfile(cache, tmp)
        run([LCUTOOL, "split", tmp, str(PART)])
        i = 1
        while os.path.exists(tmp + ".%03d" % i):
            outs.append(tmp + ".%03d" % i); i += 1
    else:
        shutil.copyfile(cache, out); outs.append(out)
    return outs, sum(os.path.getsize(s) for s, d in lst)

# --------------------------------------------------------------------------------------------- docs ---
def ldraw_credits():
    parts = []
    for s in ["sets1", "sets2", "sets3"]:
        p = os.path.join(TOOLKIT, "World", "kit", s, "CREDITS.txt")
        if os.path.exists(p):
            t = open(p, encoding="utf-8").read().splitlines()
            body = [l for l in t[1:] if l.strip()]
            # keep the model / author lines (the intro paragraph is said once in CREDITS.md)
            keep = [l for l in body if re.match(r"^\d{3,6}\b", l) or "model author" in l]
            parts += keep
    out = []
    for l in parts:
        out.append(("- " + l.strip()) if re.match(r"^\d", l) else ("    - " + l.strip()))
    return "\n".join(out)

def fill(text, vals):
    for k, v in vals.items():
        text = text.replace("{" + k + "}", str(v))
    assert not re.search(r"\{[A-Z_]+\}", text), "unfilled placeholder: " + re.search(r"\{[A-Z_]+\}", text).group(0)
    return text

def md_to_txt(md):
    t = re.sub(r"\*\*(.+?)\*\*", r"\1", md)
    t = re.sub(r"`([^`]+)`", r"\1", t)
    return t.replace("\n", "\r\n")

# --------------------------------------------------------------------------------------------- scan ---
def personal_tokens():
    """What counts as personal on THIS PC, read at run time so that no personal value is written in this script:
    the Windows user name, the Steam account folder names under Steam/userdata."""
    toks = []
    u = os.environ.get("USERNAME", "")
    if len(u) >= 3:
        toks.append((re.escape(u.encode()), "the Windows user name"))
        if len(u) >= 5:
            # the author's public name (as used for LICENSE / credits) may start like the Windows user name:
            # that one spelling is allowed, nothing else
            ok = b""
            if AUTHOR.lower().startswith(u[:5].lower()) and len(AUTHOR) > 5:
                ok = b"|" + re.escape(AUTHOR[5:].encode())
            toks.append((b"(?i)" + re.escape(u[:5].encode()) + b"(?!r(y|ies|ious|ia)" + ok + b")", "the Windows user name (prefix)"))
    try:
        import winreg
        k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam")
        steam = winreg.QueryValueEx(k, "SteamPath")[0]
        for d in os.listdir(os.path.join(steam, "userdata")):
            if d.isdigit() and len(d) >= 5:
                toks.append((re.escape(d.encode()), "a Steam account id"))
    except OSError:
        pass
    return toks

SCAN = [
    (rb"(?i)[a-z]:\\{1,2}users\\{1,2}(?!public)(?!<)[a-z0-9_.-]+", "a C:/Users/<name> path"),
    (rb"(?i)[a-z0-9._%+-]+@(?:gmail|outlook|hotmail|yahoo|icloud|proton|live|msn)\.[a-z.]+", "an e-mail address"),
    (rb"(?i)\\userdata\\\d{5,}", "a Steam userdata id path"),
] + personal_tokens()
# third-party binaries shipped unchanged whose build path names THEIR developer (not ours): allowed by sha256
ALLOW_SHA = {}
# reported, not a failure: the toolkit's folder name (no personal data in it)
INFO = [(rb"(?i)LCU-Modding", "the toolkit's folder name")]
def scan_bytes(data):
    hits = []
    for pat, what in SCAN:
        for enc in ("raw", "utf16"):
            p = pat if enc == "raw" else None
            if enc == "utf16":
                # utf-16le text (both byte alignments): search the decoded text instead
                m = None
                for start in (0, 1):
                    txt = data[start:].decode("utf-16le", "ignore").encode("latin1", "ignore")
                    m = m or re.search(pat, txt)
            else:
                m = re.search(p, data)
            if m:
                hits.append("%s (%s): %r" % (what, enc, m.group(0)[:60]))
    return hits

def scan_paths(paths, report, allow=()):
    bad = 0
    for p in paths:
        try:
            d = open(p, "rb").read()
        except OSError:
            continue
        hits = scan_bytes(d)
        hits = [h for h in hits if not any(a in h for a in allow)]
        if hits and hashlib.sha256(d).hexdigest() in ALLOW_SHA:
            report.append("allowed %s: %s (%s)" % (p, "; ".join(hits), ALLOW_SHA[hashlib.sha256(d).hexdigest()]))
            hits = []
        for pat, what in INFO:
            if re.search(pat, d):
                report.append("info %s: %s" % (p, what))
        if hits:
            bad += 1
            report.append("HIT %s\n    %s" % (p, "\n    ".join(hits)))
    return bad

# --------------------------------------------------------------------------------------------- repo ---
REPO_GITIGNORE = """# never commit game files, builds or payloads
*.DAT
*.dat
*.lcupak
*.lcupak.*
*.recipe
*.exe
*.dll
*.obj
*.lib
*.exp
*.res
*.pdb
out/
pool/
*.log
*.csv
__pycache__/
"""

def make_repo(out_root, version, docs_vals, report):
    repo = os.path.join(out_root, "repo")
    if os.path.exists(repo):
        shutil.rmtree(repo, onerror=lambda f, p, e: (os.chmod(p, 0o666), f(p)))
    os.makedirs(repo)
    # installer + release tooling
    inst = os.path.join(repo, "installer"); os.makedirs(inst)
    for f in ["lcurel.h", "lcutool.cpp", "setup.cpp", "setup.rc", "setup.manifest", "build_native.bat"]:
        shutil.copyfile(os.path.join(NATIVE, f), os.path.join(inst, f))
    tools = os.path.join(repo, "tools"); os.makedirs(tools)
    for f in ["make_release.py", "recipe_plan.py", "datindex.py", "publish_github.ps1"]:
        shutil.copyfile(os.path.join(HERE, f), os.path.join(tools, f))
    shutil.copyfile(os.path.join(TOOLS, "patchdat.py"), os.path.join(repo, "tools", "patchdat.py"))
    shutil.copytree(DOCS, os.path.join(repo, "docs"))
    # the mod's DLL + proxy sources, as they ship (staging\release1\src), tests left out (they read toolkit data)
    mod = os.path.join(repo, "mod"); os.makedirs(os.path.join(mod, "proxy"))
    src = os.path.join(STAGE, "src")
    for f in sorted(os.listdir(src)):
        if f.endswith((".h", ".cpp")) or f == "build.bat":
            shutil.copyfile(os.path.join(src, f), os.path.join(mod, f))
    for f in ["dinput8_proxy.cpp", "dinput8.def", "build_proxy.bat", "levelslots.h", "levelslots_table.h"]:
        t = open(os.path.join(src, "proxy", f), encoding="utf-8").read()
        # the developer build's fallback path is the author's own folder: a neutral one in the public copy
        t = re.sub(r'L"C:\\\\Users\\\\[^\\"]+\\\\LCU-Modding\\\\Drive\\\\lcudrive\.dll"', r'L"C:\\\\LCU-Modding\\\\Drive\\\\lcudrive.dll"', t)
        open(os.path.join(mod, "proxy", f), "w", encoding="utf-8", newline="").write(t)
    for f in ["lcudrive_spots.txt", "lcudrive_animals.txt", "lcudrive_weather.txt", "npc_runtime.tsv", "radio_provisional.tsv"]:
        shutil.copyfile(os.path.join(src, f), os.path.join(mod, f))
    shutil.copyfile(docs_vals["_INI"], os.path.join(mod, "lcudrive.ini"))     # the public default settings
    # landing page = the player README + building from source
    readme = open(os.path.join(out_root, "LCUMod-%s" % version, "README.md"), encoding="utf-8").read()
    readme += "\n---\n\n" + open(os.path.join(DOCS, "BUILDING.md"), encoding="utf-8").read()
    open(os.path.join(repo, "README.md"), "w", encoding="utf-8", newline="\n").write(readme)
    for f in ["CREDITS.md", "CHANGELOG.md", "LICENSE"]:
        shutil.copyfile(os.path.join(out_root, "LCUMod-%s" % version, f), os.path.join(repo, f))
    shutil.copytree(os.path.join(out_root, "LCUMod-%s" % version, "licenses"), os.path.join(repo, "licenses"))
    open(os.path.join(repo, ".gitignore"), "w", newline="\n").write(REPO_GITIGNORE)
    os.remove(os.path.join(repo, "docs", "BUILDING.md"))
    # scan before committing
    files = [os.path.join(dp, f) for dp, dn, fn in os.walk(repo) for f in fn]
    bad = scan_paths(files, report)
    if bad:
        return repo, bad
    env = dict(os.environ, GIT_AUTHOR_NAME=AUTHOR, GIT_AUTHOR_EMAIL=AUTHOR_EMAIL,
               GIT_COMMITTER_NAME=AUTHOR, GIT_COMMITTER_EMAIL=AUTHOR_EMAIL)
    run(["git", "init", "-q", "-b", "main"], cwd=repo)
    run(["git", "add", "-A"], cwd=repo)
    run(["git", "-c", "commit.gpgsign=false", "commit", "-q", "-m", "%s %s: first public source release" % (MOD_NAME, version)], cwd=repo, env=env)
    return repo, 0

# --------------------------------------------------------------------------------------------- main ---
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="0.1.0")
    ap.add_argument("--game", default=DEFAULT_GAME)
    ap.add_argument("--patch", default=None, help="the PATCH.DAT to release (default: the game folder's)")
    ap.add_argument("--work", default=r"D:\release_work")
    ap.add_argument("--out", default=r"D:\release_out")
    ap.add_argument("--skip-verify", action="store_true")
    ap.add_argument("--no-build", action="store_true")
    ap.add_argument("--no-repo", action="store_true")
    a = ap.parse_args()
    game, work, out_root, version = a.game, a.work, a.out, a.version
    patch = a.patch or os.path.join(game, "PATCH.DAT")
    os.makedirs(work, exist_ok=True)
    if not a.no_build:
        run(["cmd", "/c", os.path.join(NATIVE, "build_native.bat")])
    for f in [os.path.join(STAGE, "lcudrive.dll"), os.path.join(STAGE, "dinput8.dll")]:
        assert os.path.exists(f), "build staging\\release1 first (build_staged.bat): " + f
    stock = check_inputs(game)
    ALLOW_SHA[sha256(os.path.join(TOOLS, "3Dmigoto", "x64", "d3d11.dll"))] = "stock 3Dmigoto 1.3.16 d3d11.dll: its own developer's build path"
    patch_sha = sha256(patch)
    log("PATCH.DAT", patch_sha, os.path.getsize(patch))
    recipe = make_recipe(game, patch, work, patch_sha, a.skip_verify)

    core, graphics, radio, frompatch = build_lists(game, work, recipe, patch, version)
    rel_txt = os.path.join(work, "stage", "release.txt")
    comps = [("core", sum(os.path.getsize(s) for s, d in core) + sum(f[2] for f in frompatch) + os.path.getsize(patch)),
             ("graphics", sum(os.path.getsize(s) for s, d in graphics)),
             ("radio", sum(os.path.getsize(s) for s, d in radio))]
    write_release_txt(rel_txt, version, stock, game, patch, patch_sha, comps, frompatch)
    # docs that also go inside LCUMod\
    rel = os.path.join(out_root, "LCUMod-%s" % version)
    if os.path.exists(rel):
        shutil.rmtree(rel)
    os.makedirs(os.path.join(rel, "licenses"))
    for n, src in [("LDraw-CAlicense.txt", os.path.join(TOOLKIT, "World", "kit", "ldraw", "dl", "lib", "ldraw", "CAlicense.txt")),
                   ("LDraw-CAlicense4.txt", os.path.join(TOOLKIT, "World", "kit", "ldraw", "dl", "lib", "ldraw", "CAlicense4.txt"))]:
        shutil.copyfile(src, os.path.join(rel, "licenses", n))
    for n in ["3Dmigoto-COPYING.txt", "3Dmigoto-LICENSE.GPL.txt"]:     # 3Dmigoto is GPL v3 (its COPYING.txt + LICENSE.GPL.txt, fetched 2026-11-25)
        shutil.copyfile(os.path.join(HERE, "licenses_extra", n), os.path.join(rel, "licenses", n))
    payload_doc_vals = dict(VERSION=version, LDRAW_CREDITS=ldraw_credits())
    stage = os.path.join(work, "stage")
    for n in ["CREDITS.md", "LICENSE"]:
        open(os.path.join(stage, n), "w", encoding="utf-8", newline="\n").write(fill(open(os.path.join(DOCS, n), encoding="utf-8").read(), payload_doc_vals))
    core += [(rel_txt, "@setup\\release.txt"), (recipe, "@setup\\patch.recipe"),
             (os.path.join(stage, "CREDITS.md"), "LCUMod\\CREDITS.md"), (os.path.join(stage, "LICENSE"), "LCUMod\\LICENSE.txt")]
    core += [(os.path.join(rel, "licenses", n), "LCUMod\\licenses\\" + n) for n in sorted(os.listdir(os.path.join(rel, "licenses")))]
    # README goes in after the sizes are known: pack graphics / radio first, core last (README inside core)
    gl, gbytes = pack(graphics, os.path.join(rel, "graphics.lcupak"), work, "graphics")
    rl, rbytes = pack(radio, os.path.join(rel, "radio.lcupak"), work, "radio")
    shutil.copyfile(SETUP, os.path.join(rel, "Setup.exe"))
    core_est = os.path.getsize(os.path.join(work, "paks", "core.lcupak")) if os.path.exists(os.path.join(work, "paks", "core.lcupak")) else 0
    def mb(paths): return "%.0f" % (sum(os.path.getsize(p) for p in paths) / 1048576)
    installed = comps[0][1] + comps[1][1] + comps[2][1]
    vals = dict(VERSION=version, SETUP_MB="%.1f" % (os.path.getsize(SETUP) / 1048576), GRAPHICS_MB=mb(gl), RADIO_MB=mb(rl),
                CORE_MB="%.0f" % (core_est / 1048576) if core_est else "CORE_MB",
                INSTALLED_GB="%.1f" % (installed / 1073741824), DISK_GB="%.1f" % ((installed + 300 * 1048576) / 1073741824),
                LDRAW_CREDITS=payload_doc_vals["LDRAW_CREDITS"])
    readme_md = fill(open(os.path.join(DOCS, "README.md"), encoding="utf-8").read(), vals)
    open(os.path.join(stage, "README.txt"), "w", encoding="utf-8", newline="").write(md_to_txt(readme_md))
    core.append((os.path.join(stage, "README.txt"), "LCUMod\\README.txt"))
    cl, cbytes = pack(core, os.path.join(rel, "core.lcupak"), work, "core")
    if vals["CORE_MB"] != mb(cl):     # the first run did not know the core size yet: once more with it
        vals["CORE_MB"] = mb(cl)
        readme_md = fill(open(os.path.join(DOCS, "README.md"), encoding="utf-8").read(), vals)
        open(os.path.join(stage, "README.txt"), "w", encoding="utf-8", newline="").write(md_to_txt(readme_md))
        for p in cl: os.remove(p)
        cl, cbytes = pack(core, os.path.join(rel, "core.lcupak"), work, "core")
    open(os.path.join(rel, "README.md"), "w", encoding="utf-8", newline="\n").write(readme_md)
    open(os.path.join(rel, "README.txt"), "w", encoding="utf-8", newline="").write(md_to_txt(readme_md))
    for n in ["CREDITS.md", "LICENSE"]:
        shutil.copyfile(os.path.join(stage, n), os.path.join(rel, n))
    open(os.path.join(rel, "CHANGELOG.md"), "w", encoding="utf-8", newline="\n").write(fill(open(os.path.join(DOCS, "CHANGELOG.md"), encoding="utf-8").read(), vals))
    assets = [os.path.join(rel, "Setup.exe")] + cl + gl + rl
    notes = fill(open(os.path.join(DOCS, "RELEASE_NOTES.md"), encoding="utf-8").read(), dict(vals, ASSETS="\n".join(
        "| `%s` | %.1f MB |" % (os.path.basename(p), os.path.getsize(p) / 1048576) for p in assets)))
    open(os.path.join(rel, "RELEASE_NOTES.md"), "w", encoding="utf-8", newline="\n").write(notes)
    sums = [p for p in sorted(os.listdir(rel)) if os.path.isfile(os.path.join(rel, p))]
    with open(os.path.join(rel, "SHA256SUMS.txt"), "w", newline="\n") as f:
        for p in sums:
            f.write("%s  %s\n" % (sha256(os.path.join(rel, p)), p))
    for p in sorted(os.listdir(rel)):
        q = os.path.join(rel, p)
        if os.path.isfile(q):
            assert os.path.getsize(q) < 2 * 1024 ** 3, "asset over 2 GiB: " + p
            log("asset %-24s %10.1f MB" % (p, os.path.getsize(q) / 1048576))

    # personal-data scan: every file in the release folder, every file packed, the recipe
    report = ["personal-data scan %s" % time.strftime("%Y-%m-%d %H:%M")]
    files = [os.path.join(dp, f) for dp, dn, fn in os.walk(rel) for f in fn if not f.endswith((".lcupak",)) and ".lcupak." not in f]
    packed = [s for s, d in core + graphics + radio]
    bad = scan_paths(files + packed, report)
    report.append("release folder + %d packed files: %d with hits" % (len(packed), bad))
    if not a.no_repo:
        repo, rbad = make_repo(out_root, version, dict(vals, _INI=os.path.join(work, "stage", "lcudrive.ini")), report)
        report.append("repo %s: %d with hits" % (repo, rbad))
        bad += rbad
    open(os.path.join(work, "scan_report.txt"), "w", encoding="utf-8").write("\n".join(report) + "\n")
    print("\n".join(report))
    if bad:
        sys.exit("PERSONAL DATA FOUND - see %s" % os.path.join(work, "scan_report.txt"))
    log("release ready:", rel)

if __name__ == "__main__":
    main()
