// lcurel.h - shared code for the release tools (lcutool.exe, Setup.exe).  Notes\70-release.md.
//
//   Hash        SHA-256 / MD5 through Windows CNG (bcrypt) - no crypto of our own
//   Codec       LZMS through the Windows Compression API (cabinet.dll, Windows 8+) - no zlib needed
//   File        64-bit positioned reads / writes
//   Dat         the index of a GAME*.DAT (file table, pack modes); the same parse as Tools\patchdat.py
//   GameCodec   decodes a stock entry: stored, ZIPX (RC4) or LZ2K - LZ2K by calling the decompressor inside the
//               player's OWN LEGOLCUR_DX11.exe (mapped at its preferred base, as Tools\lz2k_harness does)
//   Pak         the payload archive (.lcupak, optionally split into .001 .002 ... parts)
//   Recipe      rebuilds PATCH.DAT from the player's GAME*.DAT + the recipe (copy / add / xdiff ops)
#pragma once
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <compressapi.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
#include <list>
#include <memory>
#include <functional>
#include <unordered_map>
#include <algorithm>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "cabinet.lib")

namespace rel {

// ------------------------------------------------------------------------------------------ strings ---
inline std::wstring W(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
inline std::string U(const std::wstring& w)
{
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
inline std::string Hex(const uint8_t* p, size_t n)
{
    static const char* d = "0123456789abcdef";
    std::string s(n * 2, 0);
    for (size_t i = 0; i < n; ++i) { s[2 * i] = d[p[i] >> 4]; s[2 * i + 1] = d[p[i] & 15]; }
    return s;
}
inline bool Unhex(const std::string& s, uint8_t* out, size_t n)
{
    if (s.size() != 2 * n) return false;
    auto v = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    for (size_t i = 0; i < n; ++i) {
        int a = v(s[2 * i]), b = v(s[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = (uint8_t)(a * 16 + b);
    }
    return true;
}
inline std::wstring Lower(std::wstring s) { for (auto& c : s) c = (wchar_t)towlower(c); return s; }
inline std::wstring JoinPath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    wchar_t last = a.back();
    return (last == L'\\' || last == L'/') ? a + b : a + L"\\" + b;
}

// --------------------------------------------------------------------------------------------- hash ---
class Hash {
public:
    explicit Hash(LPCWSTR alg)
    {
        if (BCryptOpenAlgorithmProvider(&a_, alg, nullptr, 0) == 0) {
            DWORD cb = 0;
            BCryptGetProperty(a_, BCRYPT_HASH_LENGTH, (PUCHAR)&len_, sizeof len_, &cb, 0);
            BCryptCreateHash(a_, &h_, nullptr, 0, nullptr, 0, 0);
        }
    }
    ~Hash() { if (h_) BCryptDestroyHash(h_); if (a_) BCryptCloseAlgorithmProvider(a_, 0); }
    Hash(const Hash&) = delete; Hash& operator=(const Hash&) = delete;
    void Add(const void* p, size_t n)
    {
        const uint8_t* q = (const uint8_t*)p;
        while (n) { ULONG k = (ULONG)std::min<size_t>(n, 1u << 30); BCryptHashData(h_, (PUCHAR)q, k, 0); q += k; n -= k; }
    }
    std::vector<uint8_t> Finish() { std::vector<uint8_t> o(len_); BCryptFinishHash(h_, o.data(), len_, 0); return o; }
private:
    BCRYPT_ALG_HANDLE a_ = nullptr; BCRYPT_HASH_HANDLE h_ = nullptr; DWORD len_ = 0;
};
inline void Md5(const void* p, size_t n, uint8_t out[16]) { Hash h(BCRYPT_MD5_ALGORITHM); h.Add(p, n); auto r = h.Finish(); memcpy(out, r.data(), 16); }
inline void Sha256(const void* p, size_t n, uint8_t out[32]) { Hash h(BCRYPT_SHA256_ALGORITHM); h.Add(p, n); auto r = h.Finish(); memcpy(out, r.data(), 32); }

// --------------------------------------------------------------------------------------------- file ---
class File {
public:
    File() = default;
    ~File() { Close(); }
    File(const File&) = delete; File& operator=(const File&) = delete;
    bool OpenRead(const std::wstring& p)
    {
        Close();
        h_ = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        return h_ != INVALID_HANDLE_VALUE;
    }
    bool OpenWrite(const std::wstring& p)   // create / truncate; readable too (the recipe re-reads earlier output)
    {
        Close();
        h_ = CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return h_ != INVALID_HANDLE_VALUE;
    }
    void Close() { if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
    bool IsOpen() const { return h_ != INVALID_HANDLE_VALUE; }
    uint64_t Size() const { LARGE_INTEGER s{}; GetFileSizeEx(h_, &s); return (uint64_t)s.QuadPart; }
    bool ReadAt(uint64_t off, void* buf, size_t n) const
    {
        uint8_t* p = (uint8_t*)buf;
        while (n) {
            OVERLAPPED o{}; o.Offset = (DWORD)off; o.OffsetHigh = (DWORD)(off >> 32);
            DWORD k = (DWORD)std::min<size_t>(n, 1u << 26), got = 0;
            if (!ReadFile(h_, p, k, &got, &o) || got != k) return false;
            p += k; off += k; n -= k;
        }
        return true;
    }
    bool WriteAt(uint64_t off, const void* buf, size_t n)
    {
        const uint8_t* p = (const uint8_t*)buf;
        while (n) {
            OVERLAPPED o{}; o.Offset = (DWORD)off; o.OffsetHigh = (DWORD)(off >> 32);
            DWORD k = (DWORD)std::min<size_t>(n, 1u << 26), put = 0;
            if (!WriteFile(h_, p, k, &put, &o) || put != k) return false;
            p += k; off += k; n -= k;
        }
        return true;
    }
    HANDLE Handle() const { return h_; }
private:
    HANDLE h_ = INVALID_HANDLE_VALUE;
};

inline bool ReadWhole(const std::wstring& p, std::vector<uint8_t>& out)
{
    File f; if (!f.OpenRead(p)) return false;
    out.resize((size_t)f.Size());
    return out.empty() || f.ReadAt(0, out.data(), out.size());
}

// SHA-256 of a whole file, streamed; progress(bytesDone) is optional.
inline bool Sha256File(const std::wstring& p, uint8_t out[32], uint64_t* size = nullptr,
                       const std::function<void(uint64_t)>& progress = nullptr)
{
    File f; if (!f.OpenRead(p)) return false;
    uint64_t n = f.Size(), done = 0;
    if (size) *size = n;
    std::vector<uint8_t> buf(8u << 20);
    Hash h(BCRYPT_SHA256_ALGORITHM);
    while (done < n) {
        size_t k = (size_t)std::min<uint64_t>(buf.size(), n - done);
        if (!f.ReadAt(done, buf.data(), k)) return false;
        h.Add(buf.data(), k); done += k;
        if (progress) progress(done);
    }
    auto r = h.Finish(); memcpy(out, r.data(), 32);
    return true;
}

// ------------------------------------------------------------------------------------- big-endian ---
inline uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
inline uint64_t Be64(const uint8_t* p) { return (uint64_t)Be32(p) << 32 | Be32(p + 4); }
inline uint32_t Le32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

// ---------------------------------------------------------------------------------------- DAT index ---
struct DatEntry { uint64_t off; uint32_t stored, raw; uint8_t pm; };

class Dat {
public:
    std::vector<DatEntry> entries;
    uint64_t indexOff = 0, indexLen = 0;
    File file;
    // Parse the archive's index (layout: Tools\patchdat.py parse_index).  The archive stays open for reads.
    bool Open(const std::wstring& path, std::string& err)
    {
        if (!file.OpenRead(path)) { err = "cannot open " + U(path); return false; }
        uint8_t h8[8];
        if (!file.ReadAt(0, h8, 8)) { err = "cannot read header"; return false; }
        int32_t first; uint32_t ilen; memcpy(&first, h8, 4); memcpy(&ilen, h8 + 4, 4);
        uint64_t io = first < 0 ? ((uint64_t)(uint32_t)(-first)) << 8 : (uint64_t)first;
        uint64_t size = file.Size();
        if (io + ilen != size) { err = "index does not reach the end of the file"; return false; }
        indexOff = io; indexLen = ilen;
        std::vector<uint8_t> d(ilen);
        if (!file.ReadAt(io, d.data(), ilen)) { err = "cannot read index"; return false; }
        return Parse(d, err);
    }
    // SHA-256 of the 8-byte header + the index: a cheap fingerprint of the archive's table of contents.
    bool IndexSha(uint8_t out[32])
    {
        std::vector<uint8_t> d((size_t)indexLen + 8);
        if (!file.ReadAt(0, d.data(), 8) || !file.ReadAt(indexOff, d.data() + 8, (size_t)indexLen)) return false;
        Sha256(d.data(), d.size(), out); return true;
    }
private:
    bool Parse(const std::vector<uint8_t>& d, std::string& err)
    {
        const uint8_t* p = d.data(); size_t n = d.size(), q = 0;
        auto need = [&](size_t k) { return q + k <= n; };
        if (!need(32) || Be32(p) + 4 != n || memcmp(p + 4, ".CC40TAD", 8) != 0) { err = "bad index signature"; return false; }
        q = 12;
        int32_t ver = (int32_t)Be32(p + q); uint32_t tver = Be32(p + q + 4), nf = Be32(p + q + 8), nn = Be32(p + q + 12), nl = Be32(p + q + 16);
        q += 20;
        if (ver != -11 || tver != 2) { err = "unsupported index version"; return false; }
        q += nl; q += 12ull * nn; q += 4;
        if (!need(8)) { err = "truncated index"; return false; }
        int32_t fver = (int32_t)Be32(p + q); uint32_t nfi = Be32(p + q + 4); q += 8;
        if (fver != -11 || nfi != nf || !need(16ull * nf)) { err = "bad file table"; return false; }
        entries.resize(nf);
        for (uint32_t i = 0; i < nf; ++i) {
            entries[i].off = Be64(p + q); entries[i].stored = Be32(p + q + 8); entries[i].raw = Be32(p + q + 12) & 0x7FFFFFFF;
            q += 16;
        }
        q += 4ull * nf;                                   // hashes
        if (!need(8)) { err = "truncated index"; return false; }
        uint32_t ds = Be32(p + q + 4); q += 8 + ds;       // duplicates
        while (q < n && p[q]) ++q;                        // common root path
        q += 1 + 16;
        const size_t esz[4] = { 16, 16, 1, 4 };
        for (int a = 0; a < 4; ++a) {
            if (!need(8) || Be32(p + q) != 0 || Be32(p + q + 4) != nf) { err = "unexpected array header"; return false; }
            q += 8;
            if (a == 2) for (uint32_t i = 0; i < nf; ++i) entries[i].pm = p[q + i];
            q += esz[a] * nf;
        }
        if (q != n) { err = "trailing bytes after the index"; return false; }
        return true;
    }
};

// ------------------------------------------------------------------------------- stock entry codec ---
// LZ2K: the game's own decompressor at RVA 0xB56C50 (0x140B56C50), decompress(srcWithHeader, dst, ctx).
// Its context is argument 3; with our own zeroed buffer it never touches the game's TLS (lz2k_harness).
class GameCodec {
public:
    ~GameCodec() { if (base_) VirtualFree(base_, 0, MEM_RELEASE); if (ctx_) VirtualFree(ctx_, 0, MEM_RELEASE); }
    bool Init(const std::wstring& exePath, std::string& err)
    {
        std::vector<uint8_t> file;
        if (!ReadWhole(exePath, file) || file.size() < 0x400) { err = "cannot read the game exe"; return false; }
        auto* dos = (IMAGE_DOS_HEADER*)file.data();
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || (size_t)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS64) > file.size()) { err = "not a PE file"; return false; }
        auto* nt = (IMAGE_NT_HEADERS64*)(file.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) { err = "not a 64-bit PE"; return false; }
        uint64_t want = nt->OptionalHeader.ImageBase;
        uint32_t imageSize = nt->OptionalHeader.SizeOfImage;
        base_ = (uint8_t*)VirtualAlloc((void*)want, imageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!base_) { err = "the game's code address is taken in this process (VirtualAlloc " + std::to_string(GetLastError()) + ")"; return false; }
        memcpy(base_, file.data(), nt->OptionalHeader.SizeOfHeaders);
        auto* sec = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (sec[i].SizeOfRawData && (uint64_t)sec[i].PointerToRawData + sec[i].SizeOfRawData <= file.size())
                memcpy(base_ + sec[i].VirtualAddress, file.data() + sec[i].PointerToRawData,
                       std::min<uint32_t>(sec[i].SizeOfRawData, imageSize - sec[i].VirtualAddress));
        // code sections executable, the rest read/write; nothing is ever both writable and executable
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            DWORD old = 0, prot = (sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) ? PAGE_EXECUTE_READ : PAGE_READWRITE;
            uint32_t vs = std::max(sec[i].Misc.VirtualSize, sec[i].SizeOfRawData);
            if (vs) VirtualProtect(base_ + sec[i].VirtualAddress, vs, prot, &old);
        }
        FlushInstructionCache(GetCurrentProcess(), base_, imageSize);
        ctx_ = (uint8_t*)VirtualAlloc(nullptr, kCtx + 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!ctx_) { err = "out of memory"; return false; }
        DWORD old = 0; VirtualProtect(ctx_ + kCtx, 0x1000, PAGE_NOACCESS, &old);
        fn_ = (Fn)(base_ + 0xB56C50);
        return true;
    }
    // Decode one stock entry (stored bytes as read from the archive) into raw bytes.
    bool Decode(uint8_t pm, const uint8_t* src, size_t n, uint32_t raw, std::vector<uint8_t>& out, std::string& err)
    {
        out.clear();
        if (pm == 0) { out.assign(src, src + n); }
        else if (pm == 6) {                                   // ZIPX: RC4 keyed by the chunk's LE rawSize
            size_t p = 0;
            while (n >= 12 && p < n - 11 && memcmp(src + p, "ZIPX", 4) == 0) {
                uint32_t a = Le32(src + p + 4), b = Le32(src + p + 8);
                if (p + 12 + (uint64_t)b > n) { err = "ZIPX chunk overruns"; return false; }
                const std::vector<uint8_t>& ks = Keystream(a);
                size_t m = std::min<size_t>(a, b);
                size_t o = out.size(); out.resize(o + m);
                for (size_t i = 0; i < m; ++i) out[o + i] = src[p + 12 + i] ^ ks[i];
                p += 12 + (size_t)b;
            }
        }
        else if (pm == 2) {
            if (!fn_) { err = "LZ2K decoder not initialised"; return false; }
            size_t p = 0;
            out.reserve(raw + 64);
            std::vector<uint8_t> dst;
            while (p + 12 <= n && memcmp(src + p, "LZ2K", 4) == 0) {
                uint32_t r = Le32(src + p + 4), c = Le32(src + p + 8);
                if (r == 0 || r > (1u << 20) || p + 12 + (uint64_t)c > n) { err = "bad LZ2K block"; return false; }
                if (c == r) {                                 // a stored block (compSize == rawSize): the bytes as they are
                    out.insert(out.end(), src + p + 12, src + p + 12 + r);
                    p += 12 + (size_t)c;
                    continue;
                }
                dst.assign(r + 64, 0);
                memset(ctx_, 0, kCtx);
                DWORD exc = 0;
                uint32_t got = SafeCall(src + p, dst.data(), &exc);
                if (exc || got != r) { err = "LZ2K block failed"; return false; }
                out.insert(out.end(), dst.begin(), dst.begin() + r);
                p += 12 + (size_t)c;
            }
        }
        else { err = "unknown pack mode " + std::to_string(pm); return false; }
        if (out.size() < raw) { err = "decoded entry too short"; return false; }
        out.resize(raw);
        return true;
    }
private:
    typedef uint32_t (*Fn)(const void*, void*, void*);
    static const size_t kCtx = 0x100000;
    uint8_t* base_ = nullptr; uint8_t* ctx_ = nullptr; Fn fn_ = nullptr;
    std::unordered_map<uint32_t, std::vector<uint8_t>> ks_;
    uint32_t SafeCall(const void* src, void* dst, DWORD* exc)
    {
        *exc = 0;
        __try { return fn_(src, dst, ctx_); }
        __except (EXCEPTION_EXECUTE_HANDLER) { *exc = GetExceptionCode(); return 0; }
    }
    const std::vector<uint8_t>& Keystream(uint32_t n)
    {
        auto it = ks_.find(n);
        if (it != ks_.end()) return it->second;
        uint8_t key[4]; memcpy(key, &n, 4);
        uint8_t S[256]; for (int i = 0; i < 256; ++i) S[i] = (uint8_t)i;
        for (int i = 0, j = 0; i < 256; ++i) { j = (j + S[i] + key[i % 4]) & 255; std::swap(S[i], S[j]); }
        std::vector<uint8_t> o(n);
        for (uint32_t k = 0, i = 0, j = 0; k < n; ++k) {
            i = (i + 1) & 255; j = (j + S[i]) & 255; std::swap(S[i], S[j]);
            o[k] = S[(S[i] + S[j]) & 255];
        }
        return ks_[n] = std::move(o);
    }
};

// ---------------------------------------------------------------------------------------- LZMS codec ---
inline bool LzmsCompress(const uint8_t* src, size_t n, std::vector<uint8_t>& out)
{
    COMPRESSOR_HANDLE c = nullptr;
    if (!CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &c)) return false;
    SIZE_T need = 0;
    Compress(c, src, n, nullptr, 0, &need);
    out.resize(need ? need : n + 4096);
    SIZE_T got = 0;
    BOOL ok = Compress(c, src, n, out.data(), out.size(), &got);
    CloseCompressor(c);
    if (!ok) return false;
    out.resize(got);
    return true;
}
inline bool LzmsDecompress(const uint8_t* src, size_t n, uint8_t* dst, size_t rawSize)
{
    DECOMPRESSOR_HANDLE d = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &d)) return false;
    SIZE_T got = 0;
    BOOL ok = Decompress(d, src, n, dst, rawSize, &got);
    CloseDecompressor(d);
    return ok && got == rawSize;
}

// ------------------------------------------------------------------------------------- payload pak ---
// <name>.lcupak (or <name>.lcupak.001, .002 ... read as one stream).  Solid: every file's bytes, back to back,
// form one stream cut into chunks of up to 8 MB, each stored or LZMS-compressed on its own.
//   "LCUPAK02" u64 dirOffset | chunks ... | directory:
//   u32 nChunks; per chunk: u64 fileOff, u32 stored, u32 raw, u8 method (0 stored, 1 LZMS)
//   u32 nFiles;  per file:  u16 pathLen, path (UTF-8, '\' separated, relative to the game folder),
//                           u64 streamOff, u64 size, u8 sha256[32], u64 mtime (FILETIME, 0 = none)
struct PakChunk { uint64_t off; uint32_t stored, raw; uint8_t method; uint64_t streamStart; };
struct PakFile { std::string path; uint64_t streamOff = 0, size = 0, mtime = 0; uint8_t sha[32] = {}; };

class MultiFile {   // one or more part files read as a single stream
public:
    bool Open(const std::wstring& base)
    {
        parts_.clear(); starts_.clear(); total_ = 0;
        if (GetFileAttributesW(base.c_str()) != INVALID_FILE_ATTRIBUTES) return Add(base);
        for (int i = 1; i < 1000; ++i) {
            wchar_t suf[8]; swprintf_s(suf, L".%03d", i);
            std::wstring p = base + suf;
            if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) break;
            if (!Add(p)) return false;
        }
        return !parts_.empty();
    }
    uint64_t Size() const { return total_; }
    bool ReadAt(uint64_t off, void* buf, size_t n) const
    {
        uint8_t* p = (uint8_t*)buf;
        while (n) {
            size_t i = std::upper_bound(starts_.begin(), starts_.end(), off) - starts_.begin() - 1;
            if (i >= parts_.size()) return false;
            uint64_t local = off - starts_[i], left = parts_[i]->Size() - local;
            size_t k = (size_t)std::min<uint64_t>(n, left);
            if (k == 0 || !parts_[i]->ReadAt(local, p, k)) return false;
            p += k; off += k; n -= k;
        }
        return true;
    }
    size_t PartCount() const { return parts_.size(); }
private:
    std::vector<std::unique_ptr<File>> parts_; std::vector<uint64_t> starts_; uint64_t total_ = 0;
    bool Add(const std::wstring& p)
    {
        auto f = std::make_unique<File>();
        if (!f->OpenRead(p)) return false;
        starts_.push_back(total_); total_ += f->Size(); parts_.push_back(std::move(f));
        return true;
    }
};

class Pak {
public:
    std::vector<PakFile> files;
    std::vector<PakChunk> chunks;
    uint64_t RawTotal() const { return chunks.empty() ? 0 : chunks.back().streamStart + chunks.back().raw; }
    size_t PartCount() const { return mf_.PartCount(); }
    bool Open(const std::wstring& base, std::string& err)
    {
        if (!mf_.Open(base)) { err = "payload not found: " + U(base); return false; }
        uint8_t h[16];
        if (!mf_.ReadAt(0, h, 16) || memcmp(h, "LCUPAK02", 8) != 0) { err = "not a payload file: " + U(base); return false; }
        uint64_t dir; memcpy(&dir, h + 8, 8);
        if (dir >= mf_.Size()) { err = "payload is incomplete (is a part missing?): " + U(base); return false; }
        std::vector<uint8_t> d((size_t)(mf_.Size() - dir));
        if (!mf_.ReadAt(dir, d.data(), d.size())) { err = "cannot read the payload directory"; return false; }
        size_t q = 0;
        auto rd = [&](void* o, size_t k) { if (q + k > d.size()) return false; memcpy(o, d.data() + q, k); q += k; return true; };
        uint32_t nc = 0, nf = 0;
        if (!rd(&nc, 4)) { err = "bad payload directory"; return false; }
        chunks.resize(nc);
        uint64_t s = 0;
        for (auto& c : chunks) {
            if (!rd(&c.off, 8) || !rd(&c.stored, 4) || !rd(&c.raw, 4) || !rd(&c.method, 1)) { err = "bad payload directory"; return false; }
            c.streamStart = s; s += c.raw;
        }
        if (!rd(&nf, 4)) { err = "bad payload directory"; return false; }
        files.resize(nf);
        for (auto& f : files) {
            uint16_t pl = 0;
            if (!rd(&pl, 2)) { err = "bad payload directory"; return false; }
            f.path.resize(pl);
            if ((pl && !rd(&f.path[0], pl)) || !rd(&f.streamOff, 8) || !rd(&f.size, 8) || !rd(f.sha, 32) || !rd(&f.mtime, 8)) { err = "bad payload directory"; return false; }
            if (f.streamOff + f.size > s) { err = "bad payload directory"; return false; }
        }
        if (q != d.size()) { err = "bad payload directory"; return false; }
        return true;
    }
    const PakFile* Find(const std::string& path) const
    {
        for (auto& f : files) if (_stricmp(f.path.c_str(), path.c_str()) == 0) return &f;
        return nullptr;
    }
    // Stream one file's bytes to sink(data, n); verifies its SHA-256.  Sequential calls in directory order
    // decompress every chunk once (one chunk is kept).
    bool Extract(const PakFile& f, const std::function<bool(const uint8_t*, size_t)>& sink, std::string& err)
    {
        Hash h(BCRYPT_SHA256_ALGORITHM);
        uint64_t pos = f.streamOff, end = f.streamOff + f.size;
        while (pos < end) {
            size_t ci = (size_t)(std::upper_bound(chunks.begin(), chunks.end(), pos,
                         [](uint64_t v, const PakChunk& c) { return v < c.streamStart; }) - chunks.begin()) - 1;
            if (ci >= chunks.size()) { err = "bad payload offsets"; return false; }
            if (!LoadChunk(ci, err)) return false;
            const PakChunk& c = chunks[ci];
            size_t a = (size_t)(pos - c.streamStart), k = (size_t)std::min<uint64_t>(end - pos, c.raw - a);
            h.Add(cur_.data() + a, k);
            if (!sink(cur_.data() + a, k)) { err = "cannot write " + f.path; return false; }
            pos += k;
        }
        auto sh = h.Finish();
        if (memcmp(sh.data(), f.sha, 32) != 0) { err = "payload checksum mismatch: " + f.path; return false; }
        return true;
    }
    bool ExtractToMemory(const PakFile& f, std::vector<uint8_t>& out, std::string& err)
    {
        out.clear(); out.reserve((size_t)f.size);
        return Extract(f, [&](const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); return true; }, err);
    }
    bool ExtractToFile(const PakFile& f, const std::wstring& dest, std::string& err)
    {
        File o; if (!o.OpenWrite(dest)) { err = "cannot create " + U(dest); return false; }
        uint64_t pos = 0;
        bool ok = Extract(f, [&](const uint8_t* p, size_t n) { bool r = o.WriteAt(pos, p, n); pos += n; return r; }, err);
        if (ok && f.mtime) { FILETIME ft; ft.dwLowDateTime = (DWORD)f.mtime; ft.dwHighDateTime = (DWORD)(f.mtime >> 32); SetFileTime(o.Handle(), nullptr, nullptr, &ft); }
        o.Close();
        if (!ok) DeleteFileW(dest.c_str());
        return ok;
    }
private:
    MultiFile mf_;
    size_t curIdx_ = (size_t)-1;
    std::vector<uint8_t> cur_, in_;
    bool LoadChunk(size_t ci, std::string& err)
    {
        if (ci == curIdx_) return true;
        const PakChunk& c = chunks[ci];
        in_.resize(c.stored);
        if (!mf_.ReadAt(c.off, in_.data(), c.stored)) { err = "cannot read the payload (damaged download?)"; return false; }
        if (c.method == 1) {
            cur_.resize(c.raw);
            if (!LzmsDecompress(in_.data(), c.stored, cur_.data(), c.raw)) { err = "payload data is damaged (re-download it)"; return false; }
        } else if (c.stored == c.raw) cur_.swap(in_);
        else { err = "bad payload chunk"; return false; }
        curIdx_ = ci;
        return true;
    }
};

// ------------------------------------------------------------------------------------------- recipe ---
// patch.recipe (all little-endian):
//   "LCURCP01"
//   u32 nArchives; per: u16 len, name ("GAME.DAT", "GAME0.DAT" ...)
//   u32 nSources;  per: u8 archive, u32 fileIndex, u32 rawSize, u8 md5[16]        (md5 of the decoded bytes)
//   u64 outSize; u8 outSha256[32]
//   u32 headLen; head bytes (written at 0)
//   u32 nTargets;  per: u64 dataOff, u64 size, u8 localHeader[24], u8 md5[16], u64 opsOff, u32 nOps, u64 litOff
//   u64 indexOff; u32 indexLen; index bytes
//   u64 opsLen; ops      (u8 type; 1 COPY: u32 src, u32 off, u32 len | 2 ADD: u32 len | 3 XDIFF: u32 src, u32 off, u32 len
//                         | 4 COPYOUT: u64 outOff, u32 len | 5 XDIFFOUT: u64 outOff, u32 len)
//   COPYOUT / XDIFFOUT read bytes this build already wrote (an earlier file of the same PATCH.DAT).
//   u64 litLen; literals (ADD bytes, XDIFF deltas; each target's from its litOff, in op order)
enum : uint8_t { OP_COPY = 1, OP_ADD = 2, OP_XDIFF = 3, OP_COPYOUT = 4, OP_XDIFFOUT = 5 };

struct RecipeSource { uint8_t arc; uint32_t fi, raw; uint8_t md5[16]; };
struct RecipeTarget { uint64_t dataOff, size; uint8_t local[24]; uint8_t md5[16]; uint64_t opsOff; uint32_t nOps; uint64_t litOff; };

struct Recipe {
    std::vector<std::string> archives;
    std::vector<RecipeSource> sources;
    uint64_t outSize = 0; uint8_t outSha[32] = {};
    std::vector<uint8_t> head, index, ops, lits;
    std::vector<RecipeTarget> targets;
    uint64_t indexOff = 0;

    bool Parse(const std::vector<uint8_t>& d, std::string& err)
    {
        size_t q = 0;
        auto rd = [&](void* o, size_t k) { if (q + k > d.size()) return false; memcpy(o, d.data() + q, k); q += k; return true; };
        auto blob = [&](std::vector<uint8_t>& v, uint64_t k) { if (q + k > d.size()) return false; v.assign(d.begin() + q, d.begin() + q + (size_t)k); q += (size_t)k; return true; };
        char magic[8];
        if (!rd(magic, 8) || memcmp(magic, "LCURCP01", 8) != 0) { err = "not a recipe"; return false; }
        uint32_t n = 0;
        if (!rd(&n, 4)) goto bad;
        archives.resize(n);
        for (auto& a : archives) { uint16_t l; if (!rd(&l, 2)) goto bad; a.resize(l); if (!rd(&a[0], l)) goto bad; }
        if (!rd(&n, 4)) goto bad;
        sources.resize(n);
        for (auto& s : sources) if (!rd(&s.arc, 1) || !rd(&s.fi, 4) || !rd(&s.raw, 4) || !rd(s.md5, 16) || s.arc >= archives.size()) goto bad;
        if (!rd(&outSize, 8) || !rd(outSha, 32)) goto bad;
        { uint32_t hl; if (!rd(&hl, 4) || !blob(head, hl)) goto bad; }
        if (!rd(&n, 4)) goto bad;
        targets.resize(n);
        for (auto& t : targets)
            if (!rd(&t.dataOff, 8) || !rd(&t.size, 8) || !rd(t.local, 24) || !rd(t.md5, 16) || !rd(&t.opsOff, 8) || !rd(&t.nOps, 4) || !rd(&t.litOff, 8)) goto bad;
        { uint32_t il; if (!rd(&indexOff, 8) || !rd(&il, 4) || !blob(index, il)) goto bad; }
        { uint64_t l; if (!rd(&l, 8) || !blob(ops, l)) goto bad; if (!rd(&l, 8) || !blob(lits, l)) goto bad; }
        if (q != d.size()) goto bad;
        return true;
    bad:
        err = "recipe is damaged"; return false;
    }
};

// Builds PATCH.DAT at outPath from the stock archives in gameDir.  progress(done, total) in output bytes.
// On any failure the output is deleted and err says why.
class RecipeBuilder {
public:
    std::function<void(uint64_t, uint64_t)> progress;
    std::function<bool()> cancelled;
    size_t cacheCap = (size_t)1536 << 20;
    uint64_t decodedBytes = 0, decodes = 0;

    bool Build(const Recipe& r, const std::wstring& gameDir, const std::wstring& outPath, std::string& err)
    {
        bool ok = Run(r, gameDir, outPath, err);
        out_.Close();
        if (!ok) DeleteFileW(outPath.c_str());
        return ok;
    }
private:
    std::vector<std::unique_ptr<Dat>> dats_;
    GameCodec codec_;
    File out_;
    std::list<uint32_t> lru_;
    std::unordered_map<uint32_t, std::pair<std::vector<uint8_t>, std::list<uint32_t>::iterator>> cache_;
    size_t cached_ = 0;

    const std::vector<uint8_t>* Source(const Recipe& r, uint32_t id, std::string& err)
    {
        auto it = cache_.find(id);
        if (it != cache_.end()) { lru_.splice(lru_.begin(), lru_, it->second.second); return &it->second.first; }
        const RecipeSource& s = r.sources[id];
        Dat& dat = *dats_[s.arc];
        if (s.fi >= dat.entries.size()) { err = "game archive " + r.archives[s.arc] + " has fewer files than expected"; return nullptr; }
        const DatEntry& e = dat.entries[s.fi];
        std::vector<uint8_t> stored(e.stored), raw;
        if (!dat.file.ReadAt(e.off, stored.data(), e.stored)) { err = "cannot read " + r.archives[s.arc]; return nullptr; }
        std::string derr;
        if (e.raw != s.raw || !codec_.Decode(e.pm, stored.data(), stored.size(), e.raw, raw, derr)) {
            err = "a file inside " + r.archives[s.arc] + " is not the expected one (" + (derr.empty() ? "size differs" : derr) + ")"; return nullptr;
        }
        uint8_t md5[16]; Md5(raw.data(), raw.size(), md5);
        if (memcmp(md5, s.md5, 16) != 0) { err = "a file inside " + r.archives[s.arc] + " differs from the stock game (modified or damaged archive)"; return nullptr; }
        decodedBytes += raw.size(); ++decodes;
        while (cached_ + raw.size() > cacheCap && !lru_.empty()) {
            uint32_t victim = lru_.back(); lru_.pop_back();
            cached_ -= cache_[victim].first.size(); cache_.erase(victim);
        }
        lru_.push_front(id);
        cached_ += raw.size();
        auto& slot = cache_[id];
        slot.first = std::move(raw); slot.second = lru_.begin();
        return &slot.first;
    }

    bool Run(const Recipe& r, const std::wstring& gameDir, const std::wstring& outPath, std::string& err)
    {
        for (auto& a : r.archives) {
            auto d = std::make_unique<Dat>();
            if (!d->Open(JoinPath(gameDir, W(a)), err)) { err = a + ": " + err; return false; }
            dats_.push_back(std::move(d));
        }
        if (!codec_.Init(JoinPath(gameDir, L"LEGOLCUR_DX11.exe"), err)) return false;
        if (!out_.OpenWrite(outPath)) { err = "cannot create " + U(outPath); return false; }
        Hash total(BCRYPT_SHA256_ALGORITHM);
        uint64_t pos = 0;
        auto emit = [&](const uint8_t* p, size_t n) -> bool {
            if (!out_.WriteAt(pos, p, n)) { err = "cannot write " + U(outPath) + " (disk full?)"; return false; }
            total.Add(p, n); pos += n; return true;
        };
        if (!emit(r.head.data(), r.head.size())) return false;
        std::vector<uint8_t> buf;
        for (size_t ti = 0; ti < r.targets.size(); ++ti) {
            const RecipeTarget& t = r.targets[ti];
            if (cancelled && cancelled()) { err = "cancelled"; return false; }
            if (t.dataOff < 24 || t.dataOff - 24 != pos) { err = "recipe layout error"; return false; }
            if (!emit(t.local, 24)) return false;
            buf.clear(); buf.reserve((size_t)t.size);
            size_t q = (size_t)t.opsOff; uint64_t lit = t.litOff;
            for (uint32_t k = 0; k < t.nOps; ++k) {
                if (q >= r.ops.size()) { err = "recipe ops overrun"; return false; }
                uint8_t ty = r.ops[q++];
                uint32_t src = 0, off = 0, len = 0; uint64_t oo = 0;
                auto u32 = [&](uint32_t& v) { memcpy(&v, &r.ops[q], 4); q += 4; };
                if (ty == OP_COPY || ty == OP_XDIFF) { u32(src); u32(off); u32(len); }
                else if (ty == OP_ADD) u32(len);
                else if (ty == OP_COPYOUT || ty == OP_XDIFFOUT) { memcpy(&oo, &r.ops[q], 8); q += 8; u32(len); }
                else { err = "bad recipe op"; return false; }
                size_t at = buf.size();
                if (ty == OP_ADD) {
                    if (lit + len > r.lits.size()) { err = "recipe literal overrun"; return false; }
                    buf.insert(buf.end(), r.lits.begin() + (size_t)lit, r.lits.begin() + (size_t)lit + len); lit += len;
                } else if (ty == OP_COPYOUT || ty == OP_XDIFFOUT) {
                    if (oo + len > t.dataOff) { err = "bad back reference"; return false; }
                    buf.resize(at + len);
                    if (!out_.ReadAt(oo, buf.data() + at, len)) { err = "cannot re-read the output"; return false; }
                    if (ty == OP_XDIFFOUT) {
                        if (lit + len > r.lits.size()) { err = "recipe literal overrun"; return false; }
                        const uint8_t* dl = r.lits.data() + lit;
                        for (uint32_t i = 0; i < len; ++i) buf[at + i] = (uint8_t)(buf[at + i] + dl[i]);
                        lit += len;
                    }
                } else {
                    if (src >= r.sources.size()) { err = "bad source id"; return false; }
                    const std::vector<uint8_t>* s = Source(r, src, err);
                    if (!s) return false;
                    if ((uint64_t)off + len > s->size()) { err = "bad source range"; return false; }
                    buf.insert(buf.end(), s->begin() + off, s->begin() + off + len);
                    if (ty == OP_XDIFF) {
                        if (lit + len > r.lits.size()) { err = "recipe literal overrun"; return false; }
                        const uint8_t* dl = r.lits.data() + lit;
                        for (uint32_t i = 0; i < len; ++i) buf[at + i] = (uint8_t)(buf[at + i] + dl[i]);
                        lit += len;
                    }
                }
            }
            if (buf.size() != t.size) { err = "rebuilt file has the wrong size"; return false; }
            uint8_t md5[16]; Md5(buf.data(), buf.size(), md5);
            if (memcmp(md5, t.md5, 16) != 0) { err = "rebuilt file " + std::to_string(ti) + " does not match (game files differ from the expected version?)"; return false; }
            if (!emit(buf.data(), buf.size())) return false;
            if (progress) progress(pos, r.outSize);
        }
        if (pos > r.indexOff) { err = "recipe layout error (index)"; return false; }
        std::vector<uint8_t> pad((size_t)(r.indexOff - pos), 0);
        if (!pad.empty() && !emit(pad.data(), pad.size())) return false;
        if (!emit(r.index.data(), r.index.size())) return false;
        auto sha = total.Finish();
        if (pos != r.outSize || memcmp(sha.data(), r.outSha, 32) != 0) { err = "the finished PATCH.DAT does not match the expected checksum"; return false; }
        if (progress) progress(pos, r.outSize);
        return true;
    }
};

} // namespace rel
