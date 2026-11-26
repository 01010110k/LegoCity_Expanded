// lcutool.cpp - the release builder's native half (Notes\70-release.md).  Build: build_native.bat.
//
//   lcutool pool   <gameDir> <list.tsv> <out.bin>        decode stock entries (lines: arc<TAB>fi<TAB>raw<TAB>md5hex) back to back;
//                                                        each decoded md5 must equal md5hex
//   lcutool recipe <plan.tsv> <PATCH.DAT> <out.recipe>   delta PATCH.DAT against the plan's sources / pools
//   lcutool apply  <gameDir> <recipe> <out.dat>          rebuild PATCH.DAT exactly as Setup does
//   lcutool pack   <list.tsv> <out.lcupak>               lines: srcPath<TAB>payloadPath   (solid, LZMS, 8 MB chunks)
//   lcutool unpack <pak> <destDir>                        extract + verify every file
//   lcutool split  <file> <maxBytes>                      file -> file.001, file.002 ... (and deletes file)
#include "lcurel.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <map>
#include <fstream>
#include <sstream>

using namespace rel;

static std::vector<std::string> SplitTab(const std::string& s)
{
    std::vector<std::string> o; size_t a = 0;
    for (;;) { size_t b = s.find('\t', a); o.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
    return o;
}
static std::vector<std::vector<std::string>> ReadTsv(const std::wstring& p)
{
    std::vector<std::vector<std::string>> rows;
    std::ifstream f(p, std::ios::binary);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        rows.push_back(SplitTab(line));
    }
    return rows;
}
static void Die(const std::string& m) { fprintf(stderr, "ERROR: %s\n", m.c_str()); exit(1); }

struct Mapped {
    const uint8_t* p = nullptr; uint64_t n = 0; HANDLE f = INVALID_HANDLE_VALUE, m = nullptr;
    bool Open(const std::wstring& path)
    {
        f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER s; GetFileSizeEx(f, &s); n = (uint64_t)s.QuadPart;
        if (n == 0) return true;
        m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!m) return false;
        p = (const uint8_t*)MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
        return p != nullptr;
    }
    ~Mapped() { if (p) UnmapViewOfFile(p); if (m) CloseHandle(m); if (f != INVALID_HANDLE_VALUE) CloseHandle(f); }
};

// ------------------------------------------------------------------------------------------- pool ---
static int CmdPool(const std::wstring& game, const std::wstring& list, const std::wstring& out)
{
    auto rows = ReadTsv(list);
    static const char* names[10] = { "GAME.DAT", "GAME0.DAT", "GAME1.DAT", "GAME2.DAT", "GAME3.DAT", "GAME4.DAT", "GAME5.DAT", "GAME6.DAT", "GAME7.DAT", "GAME8.DAT" };
    std::vector<std::unique_ptr<Dat>> dats;
    std::string err;
    for (auto n : names) { auto d = std::make_unique<Dat>(); if (!d->Open(JoinPath(game, W(n)), err)) Die(err); dats.push_back(std::move(d)); }
    GameCodec codec; if (!codec.Init(JoinPath(game, L"LEGOLCUR_DX11.exe"), err)) Die(err);
    File o; if (!o.OpenWrite(out)) Die("cannot create output");
    uint64_t pos = 0; size_t k = 0;
    std::vector<uint8_t> stored, raw;
    for (auto& r : rows) {
        int arc = atoi(r[0].c_str()); uint32_t fi = (uint32_t)strtoul(r[1].c_str(), nullptr, 10), rawSize = (uint32_t)strtoul(r[2].c_str(), nullptr, 10);
        uint8_t want[16]; if (!Unhex(r[3], want, 16)) Die("bad md5 in list");
        const DatEntry& e = dats[arc]->entries.at(fi);
        stored.resize(e.stored);
        if (!dats[arc]->file.ReadAt(e.off, stored.data(), e.stored)) Die("read failed");
        if (e.raw != rawSize || !codec.Decode(e.pm, stored.data(), stored.size(), e.raw, raw, err)) Die("decode failed: " + err + " line " + std::to_string(k));
        uint8_t md5[16]; Md5(raw.data(), raw.size(), md5);
        if (memcmp(md5, want, 16) != 0) Die("md5 mismatch for " + std::string(names[arc]) + " fi " + r[1]);
        if (!o.WriteAt(pos, raw.data(), raw.size())) Die("write failed");
        pos += raw.size();
        if (++k % 500 == 0) printf("  pool: %zu / %zu entries, %.2f GB\n", k, rows.size(), pos / 1e9);
    }
    printf("pool: %zu entries, %llu bytes\n", k, (unsigned long long)pos);
    return 0;
}

// --------------------------------------------------------------------------------------- delta core ---
static const int B = 32;               // block / window size
static const uint64_t M = 0x100000001B3ull;
static uint64_t MB_ = 0;               // M^B
static uint64_t HashAt(const uint8_t* p) { uint64_t h = 0; for (int i = 0; i < B; ++i) h = h * M + p[i]; return h; }

struct Region { uint64_t start, end; uint32_t id; };   // a source inside a pool (or a target inside the output)

struct BlockIndex {
    const uint8_t* base = nullptr;
    std::vector<uint64_t> slots; int bits = 0;
    std::vector<Region> regions;       // sorted by start
    void Build(const uint8_t* b, const std::vector<Region>& regs)
    {
        base = b; regions = regs;
        uint64_t blocks = 0;
        for (auto& r : regs) blocks += (r.end - r.start) / B;
        bits = 10; while ((1ull << bits) < blocks * 2) ++bits;
        slots.assign(1ull << bits, 0);
        uint64_t mask = (1ull << bits) - 1;
        for (auto& r : regs)
            for (uint64_t p = r.start; p + B <= r.end; p += B) {
                uint64_t s = (HashAt(b + p) * 0x9E3779B97F4A7C15ull) >> (64 - bits);
                for (int k = 0; k < 4; ++k) { uint64_t& sl = slots[(s + k) & mask]; if (!sl) { sl = p + 1; break; } }
            }
    }
    const Region* RegionOf(uint64_t pos) const
    {
        auto it = std::upper_bound(regions.begin(), regions.end(), pos, [](uint64_t v, const Region& r) { return v < r.start; });
        if (it == regions.begin()) return nullptr;
        --it; return pos < it->end ? &*it : nullptr;
    }
};

struct DOp { uint8_t type; uint32_t src; uint64_t off; uint32_t len; };   // src = source id, off = local (or out offset)
struct TargetResult { std::vector<DOp> ops; std::vector<uint8_t> lits; };

struct Matcher {
    const BlockIndex* pool = nullptr;  // may be null
    const BlockIndex* out = nullptr;
    // Longest match for t[i..] among candidates; returns len (0 = none), sets kind (0 pool, 1 out), gpos (global pos)
    static size_t Extend(const uint8_t* t, size_t i, size_t n, size_t lo, const uint8_t* base, uint64_t gpos, const Region& r,
                         size_t& back)
    {
        back = 0;
        while (i - back > lo && gpos - back > r.start && t[i - back - 1] == base[gpos - back - 1]) ++back;
        size_t f = 0;
        while (i + f < n && gpos + f < r.end && t[i + f] == base[gpos + f]) ++f;
        return back + f;
    }
};

static void DeltaTarget(const uint8_t* t, size_t n, uint64_t tStart, const BlockIndex* pool, const BlockIndex* outIx,
                        const std::vector<uint32_t>& poolIds, TargetResult& res)
{
    size_t lit = 0, i = 0;
    bool havePrev = false; int prevKind = 0; uint64_t prevEnd = 0; const Region* prevReg = nullptr;
    const uint8_t* prevBase = nullptr;
    auto flushGap = [&](size_t a, size_t b) {
        size_t L = b - a; if (!L) return;
        if (havePrev && prevEnd + L <= prevReg->end) {
            size_t same = 0;
            for (size_t k = 0; k < L; ++k) same += (t[a + k] == prevBase[prevEnd + k]);
            if (same * 2 >= L) {
                DOp op; op.len = (uint32_t)L;
                if (prevKind == 0) { op.type = OP_XDIFF; op.src = poolIds[prevReg->id]; op.off = prevEnd - prevReg->start; }
                else { op.type = OP_XDIFFOUT; op.src = 0; op.off = prevEnd; }
                res.ops.push_back(op);
                for (size_t k = 0; k < L; ++k) res.lits.push_back((uint8_t)(t[a + k] - prevBase[prevEnd + k]));
                prevEnd += L;
                return;
            }
        }
        DOp op; op.type = OP_ADD; op.src = 0; op.off = 0; op.len = (uint32_t)L;
        res.ops.push_back(op);
        res.lits.insert(res.lits.end(), t + a, t + b);
        if (havePrev) prevEnd += L;    // keep the alignment guess moving with the target
    };
    uint64_t h = (n >= (size_t)B) ? HashAt(t) : 0;
    while (i + B <= n) {
        size_t bestLen = 0, bestBack = 0; int bestKind = -1; uint64_t bestPos = 0; const Region* bestReg = nullptr;
        // 1. aligned continuation of the previous match (renamed codes, edited fields)
        if (havePrev && i > lit) {
            uint64_t c = prevEnd + (i - lit);
            if (c + 16 <= prevReg->end && i + 16 <= n && memcmp(t + i, prevBase + c, 16) == 0) {
                size_t bk; size_t len = Matcher::Extend(t, i, n, lit, prevBase, c, *prevReg, bk);
                if (len >= 16) { bestLen = len; bestBack = bk; bestKind = prevKind; bestPos = c; bestReg = prevReg; }
            }
        }
        // 2. block index lookups
        const BlockIndex* ixs[2] = { pool, outIx };
        for (int kind = 0; kind < 2; ++kind) {
            const BlockIndex* ix = ixs[kind];
            if (!ix || ix->slots.empty()) continue;
            uint64_t mask = (1ull << ix->bits) - 1, s = (h * 0x9E3779B97F4A7C15ull) >> (64 - ix->bits);
            for (int k = 0; k < 4; ++k) {
                uint64_t sl = ix->slots[(s + k) & mask];
                if (!sl) break;
                uint64_t g = sl - 1;
                if (memcmp(ix->base + g, t + i, B) != 0) continue;
                const Region* r = ix->RegionOf(g);
                if (!r) continue;
                if (kind == 1 && r->end > tStart) continue;          // only earlier files of the output
                size_t bk; size_t len = Matcher::Extend(t, i, n, lit, ix->base, g, *r, bk);
                if (len > bestLen) { bestLen = len; bestBack = bk; bestKind = kind; bestPos = g; bestReg = r; }
            }
        }
        if (bestKind >= 0 && bestLen >= 16) {
            size_t s0 = i - bestBack; uint64_t g0 = bestPos - bestBack;
            flushGap(lit, s0);
            DOp op; op.len = (uint32_t)bestLen;
            const uint8_t* base = bestKind == 0 ? pool->base : outIx->base;
            if (bestKind == 0) { op.type = OP_COPY; op.src = poolIds[bestReg->id]; op.off = g0 - bestReg->start; }
            else { op.type = OP_COPYOUT; op.src = 0; op.off = g0; }
            // merge with a directly preceding contiguous copy
            if (!res.ops.empty()) {
                DOp& p = res.ops.back();
                if (p.type == op.type && p.src == op.src && p.off + p.len == op.off && (uint64_t)p.len + op.len < 0xFFFFFFFFull) { p.len += op.len; op.len = 0; }
            }
            if (op.len) res.ops.push_back(op);
            havePrev = true; prevKind = bestKind; prevEnd = g0 + bestLen; prevReg = bestReg; prevBase = base;
            i = s0 + bestLen; lit = i;
            if (i + B <= n) h = HashAt(t + i);
            continue;
        }
        if (i + B < n) h = h * M - MB_ * t[i] + t[i + B];
        ++i;
    }
    flushGap(lit, n);
}

// ------------------------------------------------------------------------------------------ recipe ---
static void PutU8(std::vector<uint8_t>& v, uint8_t x) { v.push_back(x); }
static void PutU16(std::vector<uint8_t>& v, uint16_t x) { v.insert(v.end(), (uint8_t*)&x, (uint8_t*)&x + 2); }
static void PutU32(std::vector<uint8_t>& v, uint32_t x) { v.insert(v.end(), (uint8_t*)&x, (uint8_t*)&x + 4); }
static void PutU64(std::vector<uint8_t>& v, uint64_t x) { v.insert(v.end(), (uint8_t*)&x, (uint8_t*)&x + 8); }

static int CmdRecipe(const std::wstring& planPath, const std::wstring& patchPath, const std::wstring& outPath)
{
    MB_ = 1; for (int i = 0; i < B; ++i) MB_ *= M;
    auto rows = ReadTsv(planPath);
    std::vector<std::string> archives;
    struct Src { int arc; uint32_t fi, raw; uint8_t md5[16]; std::string group; uint64_t poolOff; };
    std::vector<Src> srcs;
    std::map<std::string, std::wstring> poolFiles;
    struct Tgt { uint64_t off, size; std::string mode, arg; };
    std::vector<Tgt> tgts;
    for (auto& r : rows) {
        if (r[0] == "archive") archives.push_back(r[1]);
        else if (r[0] == "source") {
            Src s; s.arc = atoi(r[2].c_str()); s.fi = (uint32_t)strtoul(r[3].c_str(), nullptr, 10); s.raw = (uint32_t)strtoul(r[4].c_str(), nullptr, 10);
            if (!Unhex(r[5], s.md5, 16)) Die("bad md5");
            s.group = r.size() > 6 ? r[6] : ""; s.poolOff = r.size() > 7 && !r[7].empty() ? _strtoui64(r[7].c_str(), nullptr, 10) : 0;
            if ((size_t)atoi(r[1].c_str()) != srcs.size()) Die("source ids must be 0..n-1 in order");
            srcs.push_back(s);
        }
        else if (r[0] == "pool") poolFiles[r[1]] = W(r[2]);
        else if (r[0] == "target") { Tgt t; t.off = _strtoui64(r[1].c_str(), nullptr, 10); t.size = _strtoui64(r[2].c_str(), nullptr, 10); t.mode = r[3]; t.arg = r.size() > 4 ? r[4] : ""; tgts.push_back(t); }
    }
    std::sort(tgts.begin(), tgts.end(), [](const Tgt& a, const Tgt& b) { return a.off < b.off; });
    Mapped patch; if (!patch.Open(patchPath)) Die("cannot map PATCH.DAT");
    // layout checks: head, contiguous entries, zero padding, index to EOF
    int32_t first; uint32_t ilen; memcpy(&first, patch.p, 4); memcpy(&ilen, patch.p + 4, 4);
    uint64_t indexOff = first < 0 ? ((uint64_t)(uint32_t)(-first)) << 8 : (uint64_t)first;
    if (indexOff + ilen != patch.n) Die("PATCH.DAT index does not reach EOF");
    uint64_t headLen = tgts.front().off - 24, pos = headLen;
    for (auto& t : tgts) { if (t.off - 24 != pos) Die("entries are not contiguous"); pos = t.off + t.size; }
    for (uint64_t p = pos; p < indexOff; ++p) if (patch.p[p]) Die("non-zero padding before the index");

    // the output index: every earlier target's data, for COPYOUT
    printf("indexing PATCH.DAT itself (%zu files)...\n", tgts.size());
    std::vector<Region> outRegs;
    for (size_t k = 0; k < tgts.size(); ++k) outRegs.push_back({ tgts[k].off, tgts[k].off + tgts[k].size, (uint32_t)k });
    BlockIndex outIx; outIx.Build(patch.p, outRegs);

    std::vector<TargetResult> res(tgts.size());
    auto runGroup = [&](const std::string& group, const BlockIndex* pool, const std::vector<uint32_t>& poolIds) {
        std::vector<size_t> todo;
        for (size_t k = 0; k < tgts.size(); ++k)
            if ((tgts[k].mode == "diff" && tgts[k].arg == group) || (group.empty() && tgts[k].mode == "lit")) todo.push_back(k);
        std::atomic<size_t> next{ 0 }, done{ 0 };
        unsigned nt = std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> th;
        for (unsigned w = 0; w < nt; ++w) th.emplace_back([&]() {
            for (;;) {
                size_t j = next++; if (j >= todo.size()) break;
                size_t k = todo[j];
                DeltaTarget(patch.p + tgts[k].off, (size_t)tgts[k].size, tgts[k].off, pool, &outIx, poolIds, res[k]);
                size_t d = ++done; if (d % 500 == 0) printf("  [%s] %zu / %zu\n", group.c_str(), d, todo.size());
            }
        });
        for (auto& x : th) x.join();
        printf("group '%s': %zu targets\n", group.c_str(), todo.size());
    };
    runGroup("", nullptr, {});
    for (auto& pf : poolFiles) {
        Mapped pm; if (!pm.Open(pf.second)) Die("cannot map pool " + pf.first);
        std::vector<Region> regs; std::vector<uint32_t> ids;
        for (size_t s = 0; s < srcs.size(); ++s)
            if (srcs[s].group == pf.first) { regs.push_back({ srcs[s].poolOff, srcs[s].poolOff + srcs[s].raw, (uint32_t)ids.size() }); ids.push_back((uint32_t)s); }
        if (!regs.empty() && regs.back().end != pm.n) Die("pool " + pf.first + " size does not match the plan");
        printf("indexing pool '%s' (%.2f GB, %zu sources)...\n", pf.first.c_str(), pm.n / 1e9, regs.size());
        BlockIndex pix; pix.Build(pm.p, regs);
        runGroup(pf.first, &pix, ids);
    }
    // copies
    for (size_t k = 0; k < tgts.size(); ++k)
        if (tgts[k].mode == "copy") {
            uint32_t s = (uint32_t)strtoul(tgts[k].arg.c_str(), nullptr, 10);
            if (srcs[s].raw != tgts[k].size) Die("copy size mismatch");
            res[k].ops.push_back({ OP_COPY, s, 0, (uint32_t)tgts[k].size });
        }

    // prune + renumber the sources by first use
    std::vector<int64_t> remap(srcs.size(), -1); std::vector<uint32_t> used;
    for (auto& r : res) for (auto& o : r.ops) if (o.type == OP_COPY || o.type == OP_XDIFF) { if (remap[o.src] < 0) { remap[o.src] = (int64_t)used.size(); used.push_back(o.src); } o.src = (uint32_t)remap[o.src]; }

    std::vector<uint8_t> v;
    v.insert(v.end(), (const uint8_t*)"LCURCP01", (const uint8_t*)"LCURCP01" + 8);
    PutU32(v, (uint32_t)archives.size());
    for (auto& a : archives) { PutU16(v, (uint16_t)a.size()); v.insert(v.end(), a.begin(), a.end()); }
    PutU32(v, (uint32_t)used.size());
    for (auto s : used) { PutU8(v, (uint8_t)srcs[s].arc); PutU32(v, srcs[s].fi); PutU32(v, srcs[s].raw); v.insert(v.end(), srcs[s].md5, srcs[s].md5 + 16); }
    uint8_t sha[32]; Sha256(patch.p, (size_t)patch.n, sha);
    PutU64(v, patch.n); v.insert(v.end(), sha, sha + 32);
    PutU32(v, (uint32_t)headLen); v.insert(v.end(), patch.p, patch.p + headLen);
    std::vector<uint8_t> ops, lits;
    struct Stat { uint64_t copy = 0, copyout = 0, add = 0, xdiff = 0, nops = 0; } st;
    PutU32(v, (uint32_t)tgts.size());
    for (size_t k = 0; k < tgts.size(); ++k) {
        uint64_t opsOff = ops.size(), litOff = lits.size();
        for (auto& o : res[k].ops) {
            PutU8(ops, o.type);
            if (o.type == OP_COPY || o.type == OP_XDIFF) { PutU32(ops, o.src); PutU32(ops, (uint32_t)o.off); PutU32(ops, o.len); }
            else if (o.type == OP_ADD) PutU32(ops, o.len);
            else { PutU64(ops, o.off); PutU32(ops, o.len); }
            if (o.type == OP_COPY) st.copy += o.len; else if (o.type == OP_COPYOUT) st.copyout += o.len;
            else if (o.type == OP_ADD) st.add += o.len; else st.xdiff += o.len;
            ++st.nops;
        }
        lits.insert(lits.end(), res[k].lits.begin(), res[k].lits.end());
        PutU64(v, tgts[k].off); PutU64(v, tgts[k].size);
        v.insert(v.end(), patch.p + tgts[k].off - 24, patch.p + tgts[k].off);
        uint8_t md5[16]; Md5(patch.p + tgts[k].off, (size_t)tgts[k].size, md5); v.insert(v.end(), md5, md5 + 16);
        PutU64(v, opsOff); PutU32(v, (uint32_t)res[k].ops.size()); PutU64(v, litOff);
    }
    PutU64(v, indexOff); PutU32(v, ilen); v.insert(v.end(), patch.p + indexOff, patch.p + indexOff + ilen);
    PutU64(v, ops.size()); v.insert(v.end(), ops.begin(), ops.end());
    PutU64(v, lits.size()); v.insert(v.end(), lits.begin(), lits.end());
    File o; if (!o.OpenWrite(outPath) || !o.WriteAt(0, v.data(), v.size())) Die("cannot write recipe");
    printf("recipe: %zu bytes (%zu sources used, %llu ops, ops %zu B, literals %zu B)\n", v.size(), used.size(), (unsigned long long)st.nops, ops.size(), lits.size());
    printf("bytes: copy %llu, copyout %llu, xdiff %llu, add %llu\n", (unsigned long long)st.copy, (unsigned long long)st.copyout, (unsigned long long)st.xdiff, (unsigned long long)st.add);
    // per-target stats for the measurement
    std::wstring statPath = outPath + L".stats.tsv";
    FILE* sf = _wfopen(statPath.c_str(), L"wb");
    if (sf) {
        fprintf(sf, "# dataOff\tsize\tmode\tcopy\tcopyout\txdiff\tadd\n");
        for (size_t k = 0; k < tgts.size(); ++k) {
            uint64_t c = 0, co = 0, x = 0, a = 0;
            for (auto& op : res[k].ops) { if (op.type == OP_COPY) c += op.len; else if (op.type == OP_COPYOUT) co += op.len; else if (op.type == OP_ADD) a += op.len; else x += op.len; }
            fprintf(sf, "%llu\t%llu\t%s\t%llu\t%llu\t%llu\t%llu\n", (unsigned long long)tgts[k].off, (unsigned long long)tgts[k].size, tgts[k].mode.c_str(),
                    (unsigned long long)c, (unsigned long long)co, (unsigned long long)x, (unsigned long long)a);
        }
        fclose(sf);
    }
    return 0;
}

// ------------------------------------------------------------------------------------------- apply ---
static int CmdApply(const std::wstring& game, const std::wstring& recipePath, const std::wstring& out)
{
    std::vector<uint8_t> d; if (!ReadWhole(recipePath, d)) Die("cannot read recipe");
    Recipe r; std::string err; if (!r.Parse(d, err)) Die(err);
    RecipeBuilder b;
    DWORD t0 = GetTickCount(); uint64_t last = 0;
    b.progress = [&](uint64_t done, uint64_t total) { if (done - last > 200000000ull || done == total) { last = done; printf("  %.1f%%\n", 100.0 * done / total); } };
    if (!b.Build(r, game, out, err)) Die(err);
    printf("apply OK: %llu bytes, sha256 %s, %.1f s, %llu decodes (%.2f GB decoded)\n", (unsigned long long)r.outSize, Hex(r.outSha, 32).c_str(),
           (GetTickCount() - t0) / 1000.0, (unsigned long long)b.decodes, b.decodedBytes / 1e9);
    return 0;
}

// -------------------------------------------------------------------------------------------- pack ---
static bool NoCompress(const std::string& p)
{
    static const char* ext[] = { ".ogg", ".mp3", ".m4a", ".opus", ".flac", ".png", ".jpg", ".jpeg", ".zip", ".7z", ".gz" };
    std::string l = p; for (auto& c : l) c = (char)tolower(c);
    for (auto e : ext) { size_t n = strlen(e); if (l.size() >= n && l.compare(l.size() - n, n, e) == 0) return true; }
    return false;
}

static int CmdPack(const std::wstring& listPath, const std::wstring& outPath)
{
    auto rows = ReadTsv(listPath);
    struct In { std::wstring src; std::string dst; uint64_t size, mtime; bool store; };
    std::vector<In> ins;
    for (auto& r : rows) {
        In i; i.src = W(r[0]); i.dst = r[1];
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(i.src.c_str(), GetFileExInfoStandard, &fa)) Die("missing " + r[0]);
        i.size = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        i.mtime = ((uint64_t)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
        i.store = NoCompress(i.dst);
        ins.push_back(i);
    }
    // chunk plan: compressible files run together into solid chunks; stored files get their own chunks
    struct Piece { size_t file; uint64_t off; uint32_t len; };
    struct Chunk { std::vector<Piece> pieces; uint32_t raw = 0; bool store = false; std::vector<uint8_t> outData; uint8_t method = 0; };
    const uint32_t CH = 8u << 20;
    std::vector<Chunk> chunks;
    auto plan = [&](bool store) {
        Chunk cur; cur.store = store;
        for (size_t f = 0; f < ins.size(); ++f) {
            if (ins[f].store != store) continue;
            uint64_t o = 0;
            while (o < ins[f].size) {
                uint32_t k = (uint32_t)std::min<uint64_t>(CH - cur.raw, ins[f].size - o);
                cur.pieces.push_back({ f, o, k }); cur.raw += k; o += k;
                if (cur.raw == CH) { chunks.push_back(std::move(cur)); cur = Chunk(); cur.store = store; }
            }
        }
        if (cur.raw) chunks.push_back(std::move(cur));
    };
    plan(false); plan(true);
    // stream offsets per file follow the chunk order
    std::vector<uint64_t> fileOff(ins.size(), UINT64_MAX);
    { uint64_t s = 0; for (auto& c : chunks) for (auto& p : c.pieces) { if (p.off == 0) fileOff[p.file] = s; s += p.len; } }
    for (size_t f = 0; f < ins.size(); ++f) if (ins[f].size == 0) fileOff[f] = 0;
    std::vector<std::vector<uint8_t>> sha(ins.size());
    // hashes
    {
        std::atomic<size_t> next{ 0 };
        std::vector<std::thread> th;
        for (unsigned w = 0; w < std::max(1u, std::thread::hardware_concurrency()); ++w) th.emplace_back([&]() {
            for (;;) { size_t f = next++; if (f >= ins.size()) break; uint8_t s[32]; if (!Sha256File(ins[f].src, s)) Die("read failed"); sha[f].assign(s, s + 32); }
        });
        for (auto& t : th) t.join();
    }
    File o; if (!o.OpenWrite(outPath)) Die("cannot create output");
    uint64_t pos = 16;
    std::vector<uint64_t> chunkOff(chunks.size());
    const size_t BATCH = 32;
    for (size_t c0 = 0; c0 < chunks.size(); c0 += BATCH) {
        size_t c1 = std::min(chunks.size(), c0 + BATCH);
        std::atomic<size_t> next{ c0 };
        std::vector<std::thread> th;
        for (unsigned w = 0; w < std::max(1u, std::thread::hardware_concurrency()); ++w) th.emplace_back([&]() {
            for (;;) {
                size_t c = next++; if (c >= c1) break;
                Chunk& ch = chunks[c];
                std::vector<uint8_t> raw(ch.raw); size_t at = 0;
                for (auto& p : ch.pieces) {
                    File f; if (!f.OpenRead(ins[p.file].src) || !f.ReadAt(p.off, raw.data() + at, p.len)) Die("read failed");
                    at += p.len;
                }
                std::vector<uint8_t> comp;
                if (!ch.store && LzmsCompress(raw.data(), raw.size(), comp) && comp.size() < raw.size() * 97 / 100) { ch.outData.swap(comp); ch.method = 1; }
                else { ch.outData.swap(raw); ch.method = 0; }
            }
        });
        for (auto& t : th) t.join();
        for (size_t c = c0; c < c1; ++c) {
            chunkOff[c] = pos;
            if (!o.WriteAt(pos, chunks[c].outData.data(), chunks[c].outData.size())) Die("write failed");
            pos += chunks[c].outData.size();
            std::vector<uint8_t>().swap(chunks[c].outData);
            chunks[c].outData.resize(0);
            chunks[c].pieces.shrink_to_fit();
        }
        printf("  pack: %zu / %zu chunks, %.1f MB written\n", c1, chunks.size(), pos / 1e6);
    }
    // directory (stored sizes recomputed from offsets)
    std::vector<uint8_t> dir;
    PutU32(dir, (uint32_t)chunks.size());
    for (size_t c = 0; c < chunks.size(); ++c) {
        uint64_t end = c + 1 < chunks.size() ? chunkOff[c + 1] : pos;
        PutU64(dir, chunkOff[c]); PutU32(dir, (uint32_t)(end - chunkOff[c])); PutU32(dir, chunks[c].raw); PutU8(dir, chunks[c].method);
    }
    PutU32(dir, (uint32_t)ins.size());
    for (size_t f = 0; f < ins.size(); ++f) {
        PutU16(dir, (uint16_t)ins[f].dst.size()); dir.insert(dir.end(), ins[f].dst.begin(), ins[f].dst.end());
        PutU64(dir, fileOff[f]); PutU64(dir, ins[f].size); dir.insert(dir.end(), sha[f].begin(), sha[f].end()); PutU64(dir, ins[f].mtime);
    }
    if (!o.WriteAt(pos, dir.data(), dir.size())) Die("write failed");
    uint8_t head[16]; memcpy(head, "LCUPAK02", 8); memcpy(head + 8, &pos, 8);
    o.WriteAt(0, head, 16);
    printf("pack: %zu files, %zu chunks, %llu bytes\n", ins.size(), chunks.size(), (unsigned long long)(pos + dir.size()));
    return 0;
}

static void MakeDirs(const std::wstring& path)
{
    for (size_t i = 3; i < path.size(); ++i)
        if (path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
}

static int CmdUnpack(const std::wstring& pakPath, const std::wstring& dest)
{
    Pak p; std::string err; if (!p.Open(pakPath, err)) Die(err);
    std::vector<size_t> order(p.files.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return p.files[a].streamOff < p.files[b].streamOff; });
    for (size_t i : order) {
        std::wstring out = JoinPath(dest, W(p.files[i].path));
        MakeDirs(out);
        if (!p.ExtractToFile(p.files[i], out, err)) Die(err);
    }
    printf("unpack OK: %zu files, %zu part(s)\n", p.files.size(), p.PartCount());
    return 0;
}

static int CmdSplit(const std::wstring& path, uint64_t maxBytes)
{
    File in; if (!in.OpenRead(path)) Die("cannot open");
    uint64_t n = in.Size(), pos = 0; int part = 1;
    std::vector<uint8_t> buf(16u << 20);
    while (pos < n) {
        wchar_t suf[8]; swprintf_s(suf, L".%03d", part++);
        File o; if (!o.OpenWrite(path + suf)) Die("cannot create part");
        uint64_t end = std::min(n, pos + maxBytes), op = 0;
        while (pos < end) { size_t k = (size_t)std::min<uint64_t>(buf.size(), end - pos); in.ReadAt(pos, buf.data(), k); o.WriteAt(op, buf.data(), k); pos += k; op += k; }
    }
    in.Close();
    DeleteFileW(path.c_str());
    printf("split into %d parts\n", part - 1);
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { printf("usage: see the header of lcutool.cpp\n"); return 2; }
    std::wstring c = argv[1];
    if (c == L"pool" && argc == 5) return CmdPool(argv[2], argv[3], argv[4]);
    if (c == L"recipe" && argc == 5) return CmdRecipe(argv[2], argv[3], argv[4]);
    if (c == L"apply" && argc == 5) return CmdApply(argv[2], argv[3], argv[4]);
    if (c == L"pack" && argc == 4) return CmdPack(argv[2], argv[3]);
    if (c == L"unpack" && argc == 4) return CmdUnpack(argv[2], argv[3]);
    if (c == L"split" && argc == 4) return CmdSplit(argv[2], _wcstoui64(argv[3], nullptr, 10));
    printf("bad arguments\n"); return 2;
}
