// Gow2HD-Windows launcher - strings, files, ini, PARAM.SFO, detection, SHA-256, JSON.
#include "common.h"
#include <bcrypt.h>
#include <stdarg.h>
#include <stdio.h>
#include <algorithm>

// ================================================================ strings
wstr Widen(const std::string& s) {
    if (s.empty()) return wstr();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    wstr w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
std::string Narrow(const wstr& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
wstr Fmt(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf(buf, 2047, fmt, ap);
    va_end(ap);
    buf[2047] = 0;
    return buf;
}
std::string ToLowerA(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
wstr ToLowerW(wstr s) { CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
wstr TrimW(const wstr& s) {
    size_t a = s.find_first_not_of(L" \t\r\n\""), b = s.find_last_not_of(L" \t\r\n\"");
    return a == wstr::npos ? wstr() : s.substr(a, b - a + 1);
}
bool IEquals(const wstr& a, const wstr& b) { return lstrcmpiW(a.c_str(), b.c_str()) == 0; }
bool EndsWithI(const wstr& s, const wstr& suf) {
    return s.size() >= suf.size() && IEquals(s.substr(s.size() - suf.size()), suf);
}
wstr FormatBytes(uint64_t n) {
    if (n >= (1ull << 30)) return Fmt(L"%.1f GB", n / 1073741824.0);
    if (n >= (1ull << 20)) return Fmt(L"%.1f MB", n / 1048576.0);
    if (n >= 1024) return Fmt(L"%.0f KB", n / 1024.0);
    return Fmt(L"%llu B", (unsigned long long)n);
}
// Quote one argument for CommandLineToArgvW / the MSVC CRT rules.
wstr ArgQuote(const wstr& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == wstr::npos) return arg;
    wstr r = L"\"";
    for (size_t i = 0;; ++i) {
        size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++i; ++bs; }
        if (i == arg.size()) { r.append(bs * 2, L'\\'); break; }
        if (arg[i] == L'"') { r.append(bs * 2 + 1, L'\\'); r.push_back(L'"'); }
        else { r.append(bs, L'\\'); r.push_back(arg[i]); }
    }
    r.push_back(L'"');
    return r;
}

// ================================================================ files
wstr JoinPath(const wstr& a, const wstr& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}
wstr ParentDir(const wstr& p) {
    wstr s = p;
    while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back();
    size_t k = s.find_last_of(L"\\/");
    if (k == wstr::npos) return wstr();
    if (k == 2 && s[1] == L':') return s.substr(0, 3);
    return s.substr(0, k);
}
wstr FileNameOf(const wstr& p) {
    size_t k = p.find_last_of(L"\\/");
    return k == wstr::npos ? p : p.substr(k + 1);
}
wstr FullPath(const wstr& p) {
    if (p.empty()) return p;
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetFullPathNameW(p.c_str(), MAX_PATH * 4, buf, nullptr);
    if (!n || n >= MAX_PATH * 4) return p;
    wstr r = buf;
    while (r.size() > 3 && (r.back() == L'\\' || r.back() == L'/')) r.pop_back();
    return r;
}
wstr ExePath() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    return wstr(buf, n);
}
wstr ExeDir() { return ParentDir(ExePath()); }
wstr KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; wstr r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}
wstr GetEnv(const wchar_t* name) {
    wchar_t buf[4096];
    DWORD n = GetEnvironmentVariableW(name, buf, 4096);
    return (n && n < 4096) ? wstr(buf, n) : wstr();
}
bool FileExists(const wstr& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const wstr& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool MakeDirs(const wstr& p) {
    if (p.empty() || DirExists(p)) return true;
    wstr parent = ParentDir(p);
    if (!parent.empty() && parent != p && !DirExists(parent)) MakeDirs(parent);
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}
uint64_t FileSize(const wstr& p) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return 0;
    return ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}
uint64_t FreeSpace(const wstr& p) {
    wstr d = p;
    while (!d.empty() && !DirExists(d)) d = ParentDir(d);
    if (d.empty()) return 0;
    ULARGE_INTEGER avail;
    if (!GetDiskFreeSpaceExW(d.c_str(), &avail, nullptr, nullptr)) return 0;
    return avail.QuadPart;
}
std::vector<wstr> ListDir(const wstr& dir, bool wantDirs, const wchar_t* pattern) {
    std::vector<wstr> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(JoinPath(dir, pattern).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (isDir == wantDirs) out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
bool ReadFileBytes(const wstr& p, std::string& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    if (sz.QuadPart > (64 << 20)) { CloseHandle(h); return false; }
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    bool ok = sz.QuadPart == 0 || (ReadFile(h, &out[0], (DWORD)sz.QuadPart, &got, nullptr) && got == sz.QuadPart);
    CloseHandle(h);
    return ok;
}
static bool WriteImpl(const wstr& p, const std::string& data, bool append) {
    HANDLE h = CreateFileW(p.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &put, nullptr) && put == data.size());
    CloseHandle(h);
    return ok;
}
bool WriteFileBytes(const wstr& p, const std::string& data) {
    // write to a temp file and move it over, so a crash never leaves a half-written settings file
    wstr tmp = p + L".tmp";
    if (!WriteImpl(tmp, data, false)) return false;
    return MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
bool AppendFileBytes(const wstr& p, const std::string& data) { return WriteImpl(p, data, true); }
bool DeleteTree(const wstr& dir) {
    if (dir.size() < 4 || !DirExists(dir)) return true;
    for (auto& f : ListDir(dir, false)) {
        wstr fp = JoinPath(dir, f);
        SetFileAttributesW(fp.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(fp.c_str());
    }
    for (auto& d : ListDir(dir, true)) {
        wstr dp = JoinPath(dir, d);
        DWORD a = GetFileAttributesW(dp.c_str());
        if (a & FILE_ATTRIBUTE_REPARSE_POINT) RemoveDirectoryW(dp.c_str());   // never follow junctions
        else DeleteTree(dp);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}
bool DirWritable(const wstr& dir) {
    wstr t = JoinPath(dir, L".gow2_write_test");
    HANDLE h = CreateFileW(t.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// ================================================================ KvFile
bool KvFile::Load(const wstr& path) {
    lines.clear();
    std::string data;
    if (!ReadFileBytes(path, data)) return false;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF) data = data.substr(3);   // UTF-8 BOM
    size_t pos = 0;
    while (pos < data.size()) {
        size_t e = data.find('\n', pos);
        std::string l = data.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return true;
}
bool KvFile::Save(const wstr& path) const {
    std::string out;
    for (auto& l : lines) { out += l; out += "\r\n"; }
    return WriteFileBytes(path, out);
}
static bool KeyOf(const std::string& line, std::string& key, std::string& val) {
    std::string t = Trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') return false;
    size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    key = ToLowerA(Trim(t.substr(0, eq)));
    val = Trim(t.substr(eq + 1));
    return true;
}
std::string KvFile::Get(const std::string& key, const std::string& def) const {
    std::string k, v, want = ToLowerA(key);
    for (auto& l : lines) if (KeyOf(l, k, v) && k == want) return v;
    return def;
}
int KvFile::GetInt(const std::string& key, int def) const {
    std::string v = Get(key);
    if (v.empty()) return def;
    char* end = nullptr;
    long r = strtol(v.c_str(), &end, 10);
    return (end && end != v.c_str()) ? (int)r : def;
}
void KvFile::Set(const std::string& key, const std::string& value) {
    std::string k, v, want = ToLowerA(key);
    for (auto& l : lines) if (KeyOf(l, k, v) && k == want) { l = key + "=" + value; return; }
    lines.push_back(key + "=" + value);
}

// ================================================================ PARAM.SFO
SfoInfo ReadSfo(const wstr& path) {
    std::string d;
    if (!ReadFileBytes(path, d)) return SfoInfo();
    return ReadSfoBytes(d);
}
SfoInfo ReadSfoBytes(const std::string& d) {
    SfoInfo r;
    if (d.size() < 20 || d.compare(0, 4, std::string("\0PSF", 4)) != 0) return r;
    auto u32 = [&](size_t o) -> uint32_t { return o + 4 <= d.size() ? *(const uint32_t*)&d[o] : 0; };
    auto u16 = [&](size_t o) -> uint16_t { return o + 2 <= d.size() ? *(const uint16_t*)&d[o] : 0; };
    uint32_t keyTab = u32(8), dataTab = u32(12), count = u32(16);
    if (count > 256) return r;
    for (uint32_t i = 0; i < count; ++i) {
        size_t e = 20 + 16 * i;
        if (e + 16 > d.size()) return r;
        uint16_t ko = u16(e); uint16_t fmt = u16(e + 2); uint32_t len = u32(e + 4); uint32_t doff = u32(e + 12);
        size_t kp = keyTab + ko, dp = dataTab + doff;
        if (kp >= d.size() || dp + len > d.size()) continue;
        std::string key(&d[kp]);
        if (fmt != 0x0204 && fmt != 0x0004) continue;   // utf-8 strings only
        std::string val(&d[dp], len);
        val = val.c_str();
        if (key == "TITLE_ID") r.titleId = val;
        else if (key == "APP_VER") r.appVer = val;
        else if (key == "VERSION") r.version = val;
        else if (key == "TITLE") r.title = val;
        else if (key == "CATEGORY") r.category = val;
    }
    r.ok = !r.titleId.empty();
    return r;
}

// Resolve whatever folder the player picked to the folder that holds PARAM.SFO + USRDIR.
static wstr ResolveGameRoot(const wstr& picked) {
    wstr p = FullPath(TrimW(picked));
    if (p.empty()) return p;
    if (FileExists(p)) p = ParentDir(p);                        // picked a file (EBOOT.BIN / PARAM.SFO)
    if (IEquals(FileNameOf(p), L"USRDIR")) p = ParentDir(p);
    if (FileExists(JoinPath(p, L"PARAM.SFO"))) return p;
    const wchar_t* subs[] = { L"NPUA80491", L"PS3_GAME", L"dev_hdd0\\game\\NPUA80491" };
    for (auto s : subs) if (FileExists(JoinPath(JoinPath(p, s), L"PARAM.SFO"))) return JoinPath(p, s);
    for (auto& d : ListDir(p, true)) {                              // one level down, e.g. "God of War II [NPUA80491]"
        wstr c = JoinPath(p, d);
        if (FileExists(JoinPath(c, L"PARAM.SFO"))) return c;
        if (FileExists(JoinPath(JoinPath(c, L"PS3_GAME"), L"PARAM.SFO"))) return JoinPath(c, L"PS3_GAME");
    }
    return p;
}

GameCheck CheckGameDir(const wstr& picked) {
    GameCheck g;
    g.dir = ResolveGameRoot(picked);
    if (g.dir.empty() || !DirExists(g.dir)) { g.status = GAME_MISSING; g.message = L"Folder not found."; return g; }
    SfoInfo s = ReadSfo(JoinPath(g.dir, L"PARAM.SFO"));
    if (!s.ok) {
        g.status = GAME_NOT_PS3;
        g.message = L"No PARAM.SFO here. Pick the NPUA80491 folder (it contains PARAM.SFO and USRDIR).";
        return g;
    }
    if (s.titleId != GAME_TITLE_ID) {
        g.status = GAME_WRONG_TITLE;
        g.message = Fmt(L"This is %s (%s). Only NPUA80491 v01.00 US is supported.",
                        Widen(s.title).c_str(), Widen(s.titleId).c_str());
        return g;
    }
    std::string ver = !s.appVer.empty() ? s.appVer : s.version;
    if (ver != GAME_APP_VER) {
        g.status = GAME_WRONG_VERSION;
        g.message = Fmt(L"Found NPUA80491 v%s. Only NPUA80491 v01.00 US is supported.", Widen(ver).c_str());
        return g;
    }
    if (!FileExists(JoinPath(g.dir, L"USRDIR\\EBOOT.BIN"))) {
        g.status = GAME_NO_EBOOT;
        g.message = L"USRDIR\\EBOOT.BIN is missing. Is the game fully installed?";
        return g;
    }
    g.status = GAME_OK;
    g.message = L"God of War II HD, NPUA80491 v01.00 (US).";
    return g;
}

// ================================================================ PSN package (.pkg)
// Layout and decryption as in RPCS3 (rpcs3/Crypto/unpkg.cpp, key_vault.h) and pkg2zip; the full description and the
// extractor are in winport/buildlib/pkg.py. Here only the header, the item table and PARAM.SFO are read (a few KB),
// so the launcher can tell the right package from a wrong one before a build. Retail packages: AES-128-CTR with the
// PS3 package key (a platform constant, the same for every PS3 package), counter = header IV + block index.
static const uint8_t kPs3PkgKey[16] = { 0x2E, 0x7B, 0x71, 0xD7, 0xC9, 0xC9, 0xA1, 0x4E,
                                        0xA3, 0x22, 0x1F, 0x18, 0x88, 0x28, 0xB8, 0xF8 };
static uint32_t Be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t Be64(const uint8_t* p) { return (uint64_t)Be32(p) << 32 | Be32(p + 4); }

struct PkgFile {
    HANDLE f = INVALID_HANDLE_VALUE;
    uint8_t hdr[0xC0] = {};
    uint64_t fileSize = 0, totalSize = 0, dataOff = 0;
    uint32_t itemCount = 0;
    uint16_t revision = 0, type = 0;
    std::string contentId;
    ~PkgFile() { if (f != INVALID_HANDLE_VALUE) CloseHandle(f); }
    bool ReadAt(uint64_t off, void* buf, DWORD n) {
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)off;
        DWORD got = 0;
        return SetFilePointerEx(f, li, nullptr, FILE_BEGIN) && ReadFile(f, buf, n, &got, nullptr) && got == n;
    }
    bool Open(const wstr& path) {
        f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz; GetFileSizeEx(f, &sz); fileSize = (uint64_t)sz.QuadPart;
        if (!ReadAt(0, hdr, sizeof(hdr)) || Be32(hdr) != 0x7F504B47) return false;
        revision = (uint16_t)(hdr[4] << 8 | hdr[5]); type = (uint16_t)(hdr[6] << 8 | hdr[7]);
        itemCount = Be32(hdr + 0x14); totalSize = Be64(hdr + 0x18); dataOff = Be64(hdr + 0x20);
        contentId.assign((const char*)hdr + 0x30, 0x30);
        contentId = contentId.c_str();
        return true;
    }
    // Decrypt size bytes at offset rel of the data area.
    bool ReadData(uint64_t rel, uint32_t size, std::string& out) {
        out.clear();
        if ((revision != 0x8000 && revision != 0x0000) || size > (1u << 20)) return false;
        uint64_t b0 = rel / 16; uint32_t skip = (uint32_t)(rel % 16);
        uint32_t nblk = (skip + size + 15) / 16;
        if (dataOff + b0 * 16 + (uint64_t)nblk * 16 > fileSize) return false;
        std::vector<uint8_t> enc((size_t)nblk * 16), ctr((size_t)nblk * 16);
        if (!ReadAt(dataOff + b0 * 16, enc.data(), (DWORD)enc.size())) return false;
        if (revision == 0x0000) {   // debug package: keystream block = SHA-1(digest[0:8] x2, digest[8:16] x2, 0 x24, u64 BE index)[0:16]
            BCRYPT_ALG_HANDLE sha = nullptr;
            if (BCryptOpenAlgorithmProvider(&sha, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return false;
            uint8_t in[64] = {}, dig[20];
            memcpy(in, hdr + 0x60, 8); memcpy(in + 8, hdr + 0x60, 8); memcpy(in + 16, hdr + 0x68, 8); memcpy(in + 24, hdr + 0x68, 8);
            bool ok = true;
            for (uint32_t i = 0; i < nblk && ok; ++i) {
                uint64_t idx = b0 + i;
                for (int k = 0; k < 8; ++k) in[56 + k] = (uint8_t)(idx >> (56 - 8 * k));
                BCRYPT_HASH_HANDLE h = nullptr;
                ok = BCryptCreateHash(sha, &h, nullptr, 0, nullptr, 0, 0) == 0 && BCryptHashData(h, in, 64, 0) == 0 &&
                     BCryptFinishHash(h, dig, 20, 0) == 0;
                if (h) BCryptDestroyHash(h);
                for (int k = 0; k < 16; ++k) enc[(size_t)i * 16 + k] ^= dig[k];
            }
            BCryptCloseAlgorithmProvider(sha, 0);
            if (!ok) return false;
            out.assign((const char*)enc.data() + skip, size);
            return true;
        }
        for (uint32_t i = 0; i < nblk; ++i) {           // IV + block index, 128-bit big-endian
            uint8_t* c = &ctr[(size_t)i * 16];
            memcpy(c, hdr + 0x70, 16);
            uint64_t add = b0 + i;
            for (int k = 15; k >= 0 && add; --k) { uint64_t s = (uint64_t)c[k] + (add & 0xFF); c[k] = (uint8_t)s; add = (add >> 8) + (s >> 8); }
        }
        BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_KEY_HANDLE key = nullptr;
        bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) == 0 &&
                  BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_ECB,
                                    (ULONG)sizeof(BCRYPT_CHAIN_MODE_ECB), 0) == 0 &&
                  BCryptGenerateSymmetricKey(alg, &key, nullptr, 0, (PUCHAR)kPs3PkgKey, 16, 0) == 0;
        ULONG got = 0;
        ok = ok && BCryptEncrypt(key, ctr.data(), (ULONG)ctr.size(), nullptr, nullptr, 0, ctr.data(), (ULONG)ctr.size(), &got, 0) == 0;
        if (key) BCryptDestroyKey(key);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
        if (!ok) return false;
        for (size_t i = 0; i < enc.size(); ++i) enc[i] ^= ctr[i];
        out.assign((const char*)enc.data() + skip, size);
        return true;
    }
};

bool IsPkgPath(const wstr& p) { return EndsWithI(TrimW(p), L".pkg"); }
wstr PkgGameDir(const wstr& installRoot) { return JoinPath(installRoot, L"game\\NPUA80491"); }

GameCheck CheckGamePkg(const wstr& path) {
    GameCheck g;
    g.isPkg = true;
    g.dir = FullPath(TrimW(path));
    if (!FileExists(g.dir)) { g.status = GAME_MISSING; g.message = L"File not found."; return g; }
    PkgFile p;
    if (!p.Open(g.dir) || p.type != 1) {
        g.status = GAME_NOT_PS3;
        g.message = L"This is not a PS3 game package (.pkg).";
        return g;
    }
    if (p.contentId != GAME_CONTENT_ID) {
        g.status = GAME_WRONG_TITLE;
        g.message = Fmt(L"This package is %s. Only God of War II HD US (%s) is supported.",
                        Widen(p.contentId.empty() ? std::string("another title") : p.contentId).c_str(), Widen(GAME_CONTENT_ID).c_str());
        return g;
    }
    if (p.fileSize < p.totalSize) {
        g.status = GAME_PKG_INCOMPLETE;
        g.message = Fmt(L"The package is incomplete (%s of %s): the download did not finish.",
                        FormatBytes(p.fileSize).c_str(), FormatBytes(p.totalSize).c_str());
        return g;
    }
    {                                           // look inside: item table, PARAM.SFO, the two big files
        std::string tab;
        if (p.itemCount == 0 || p.itemCount > 4096 || !p.ReadData(0, p.itemCount * 32, tab)) {
            g.status = GAME_NOT_PS3; g.message = L"The package's file table cannot be read (damaged file?)."; return g;
        }
        SfoInfo sfo; bool eboot = false; uint64_t psarc = 0;
        for (uint32_t i = 0; i < p.itemCount; ++i) {
            const uint8_t* e = (const uint8_t*)tab.data() + i * 32;
            uint32_t no = Be32(e), ns = Be32(e + 4), fl = Be32(e + 24);
            uint64_t dofs = Be64(e + 8), dsz = Be64(e + 16);
            if ((fl >> 24) == 0x90 || ns == 0 || ns > 1024) continue;
            std::string name;
            if (!p.ReadData(no, ns, name)) continue;
            name = name.c_str();
            if (name == "PARAM.SFO" && dsz < 65536) { std::string s; if (p.ReadData(dofs, (uint32_t)dsz, s)) sfo = ReadSfoBytes(s); }
            else if (name == "USRDIR/EBOOT.BIN") eboot = true;
            else if (name == "USRDIR/gow2.psarc") psarc = dsz;
        }
        if (!sfo.ok) { g.status = GAME_NOT_PS3; g.message = L"The package has no readable PARAM.SFO (damaged file?)."; return g; }
        std::string ver = !sfo.appVer.empty() ? sfo.appVer : sfo.version;
        if (sfo.category == "GD" || (eboot && !psarc)) {
            g.status = GAME_PKG_PATCH;
            g.message = L"This package holds only EBOOT.BIN (a game update or a modified EBOOT), not the full game. Pick the full game package.";
            return g;
        }
        if (sfo.titleId != GAME_TITLE_ID || ver != GAME_APP_VER) {
            g.status = GAME_WRONG_VERSION;
            g.message = Fmt(L"This package holds %s v%s. Only NPUA80491 v01.00 US is supported.", Widen(sfo.titleId).c_str(), Widen(ver).c_str());
            return g;
        }
        if (!eboot || psarc != GAME_PSARC_SIZE) {
            g.status = GAME_WRONG_VERSION;
            g.message = L"The package does not hold the v01.00 game files (USRDIR\\EBOOT.BIN, gow2.psarc).";
            return g;
        }
    }
    g.status = GAME_OK;
    g.message = L"Game package (.pkg) found: God of War II HD (NPUA80491)";
    return g;
}

GameCheck CheckGameInput(const wstr& picked) {
    wstr p = TrimW(picked);
    if (IsPkgPath(p) && !DirExists(p)) return CheckGamePkg(p);
    return CheckGameDir(p);
}

bool CheckRapFile(const wstr& path, wstr& msg) {
    if (path.empty()) { msg = L"Not found yet."; return false; }
    if (!FileExists(path)) { msg = L"File not found."; return false; }
    if (!EndsWithI(path, L".rap")) { msg = L"This is not a .rap file."; return false; }
    if (FileSize(path) != 16) { msg = L"A .rap file is exactly 16 bytes; this one is not."; return false; }
    if (ToLowerW(FileNameOf(path)).find(L"npua80491") == wstr::npos) {
        msg = L"The name doesn't mention NPUA80491. It may belong to another game.";
        return true;   // a warning, not a blocker: players sometimes rename files
    }
    msg = L"License file found.";
    return true;
}

// ================================================================ detection
static void AddUnique(std::vector<wstr>& v, const wstr& p) {
    if (p.empty()) return;
    wstr f = FullPath(p);
    for (auto& e : v) if (IEquals(e, f)) return;
    v.push_back(f);
}
static bool LooksLikeRpcs3(const wstr& d) {
    return DirExists(JoinPath(d, L"dev_hdd0")) || FileExists(JoinPath(d, L"rpcs3.exe"));
}
// RPCS3 can move dev_hdd0 (config\vfs.yml or vfs.yml: "/dev_hdd0/: <path>").
static void Rpcs3Hdd0FromVfs(const wstr& rpcs3, std::vector<wstr>& hdd0) {
    const wchar_t* names[] = { L"config\\vfs.yml", L"vfs.yml" };
    for (auto n : names) {
        std::string y;
        if (!ReadFileBytes(JoinPath(rpcs3, n), y)) continue;
        size_t k = y.find("/dev_hdd0/:");
        if (k == std::string::npos) continue;
        size_t e = y.find('\n', k);
        std::string v = Trim(y.substr(k + 11, e == std::string::npos ? std::string::npos : e - k - 11));
        if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'')) v = v.substr(1, v.size() - 2);
        const std::string emu = "$(EmulatorDir)";
        if (v.compare(0, emu.size(), emu) == 0) v = Narrow(rpcs3) + "\\" + v.substr(emu.size());
        for (auto& c : v) if (c == '/') c = '\\';
        if (!v.empty()) AddUnique(hdd0, Widen(v));
    }
}
static void FindRpcs3Dirs(std::vector<wstr>& out, std::vector<wstr>& log) {
    std::vector<wstr> cand;
    // 1) Uninstall entries (installer builds) and RPCS3's own registry key if a build wrote one.
    const wchar_t* roots[] = { L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
                               L"Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" };
    HKEY hives[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (HKEY hive : hives) for (auto r : roots) {
        HKEY k;
        if (RegOpenKeyExW(hive, r, 0, KEY_READ, &k) != ERROR_SUCCESS) continue;
        wchar_t name[256];
        for (DWORD i = 0;; ++i) {
            DWORD nl = 256;
            if (RegEnumKeyExW(k, i, name, &nl, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            if (!wcsstr(ToLowerW(name).c_str(), L"rpcs3")) continue;
            wchar_t val[1024]; DWORD vl = sizeof(val);
            if (RegGetValueW(k, name, L"InstallLocation", RRF_RT_REG_SZ, nullptr, val, &vl) == ERROR_SUCCESS) cand.push_back(val);
        }
        RegCloseKey(k);
    }
    // 2) Usual folders.
    wstr appdata = KnownFolder(FOLDERID_RoamingAppData), local = KnownFolder(FOLDERID_LocalAppData);
    wstr home = KnownFolder(FOLDERID_Profile), docs = KnownFolder(FOLDERID_Documents);
    wstr desk = KnownFolder(FOLDERID_Desktop), dl = KnownFolder(FOLDERID_Downloads);
    wstr pf = KnownFolder(FOLDERID_ProgramFiles), pfx = KnownFolder(FOLDERID_ProgramFilesX86);
    for (auto b : { appdata, local, home, docs, desk, dl, pf, pfx, local + L"\\Programs" })
        if (!b.empty()) { cand.push_back(JoinPath(b, L"rpcs3")); cand.push_back(JoinPath(b, L"RPCS3")); }
    cand.push_back(JoinPath(home, L"scoop\\apps\\rpcs3\\current"));
    cand.push_back(JoinPath(home, L"scoop\\persist\\rpcs3"));
    for (wstr e = ExeDir(); e.size() > 3; e = ParentDir(e)) cand.push_back(e);   // launcher dropped into RPCS3
    for (auto b : { dl, desk, docs, home }) {                 // "rpcs3-v0.0.xx-win64" style unzipped folders
        if (b.empty()) continue;
        for (auto& d : ListDir(b, true, L"rpcs3*")) cand.push_back(JoinPath(b, d));
    }
    // 3) Every fixed drive: \RPCS3, \Emulators\RPCS3, \Emulation\RPCS3, \Games\RPCS3, ...
    DWORD drives = GetLogicalDrives();
    for (int i = 2; i < 26; ++i) {
        if (!(drives & (1u << i))) continue;
        wchar_t root[4] = { (wchar_t)(L'A' + i), L':', L'\\', 0 };
        if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
        const wchar_t* subs[] = { L"RPCS3", L"Emulators\\RPCS3", L"Emulation\\RPCS3", L"Emulators\\PS3\\RPCS3",
                                  L"Emulation\\PS3\\RPCS3", L"Games\\RPCS3", L"Games\\Emulators\\RPCS3", L"Tools\\RPCS3" };
        for (auto s : subs) cand.push_back(wstr(root) + s);
    }
    for (auto& c : cand) {
        if (c.empty()) continue;
        if (LooksLikeRpcs3(c)) { AddUnique(out, c); }
        else if (DirExists(c)) {                               // a "RPCS3" folder holding versioned builds
            for (auto& d : ListDir(c, true)) if (LooksLikeRpcs3(JoinPath(c, d))) AddUnique(out, JoinPath(c, d));
        }
    }
    for (auto& r : out) log.push_back(L"RPCS3 folder: " + r);
}

static std::vector<wstr> Hdd0Dirs(const std::vector<wstr>& rpcs3) {
    std::vector<wstr> h;
    for (auto& r : rpcs3) { AddUnique(h, JoinPath(r, L"dev_hdd0")); Rpcs3Hdd0FromVfs(r, h); }
    return h;
}

wstr FindRap(const wstr& gameDir, const std::vector<wstr>& rpcs3Dirs, wstr* from) {
    auto tryDir = [&](const wstr& d, const wchar_t* what) -> wstr {
        if (d.empty() || !DirExists(d)) return wstr();
        wstr exact = JoinPath(d, RAP_FILE_NAME);
        if (FileExists(exact) && FileSize(exact) == 16) { if (from) *from = what; return exact; }
        for (auto& f : ListDir(d, false, L"*NPUA80491*.rap")) {
            wstr p = JoinPath(d, f);
            if (FileSize(p) == 16) { if (from) *from = what; return p; }
        }
        return wstr();
    };
    wstr r;
    std::vector<wstr> hdd0 = Hdd0Dirs(rpcs3Dirs);
    size_t k = ToLowerW(gameDir).find(L"\\dev_hdd0\\game\\");      // game inside some RPCS3: use that RPCS3's licenses
    if (k != wstr::npos) AddUnique(hdd0, gameDir.substr(0, k + 9));
    for (auto& h : hdd0) {
        wstr home = JoinPath(h, L"home");
        for (auto& u : ListDir(home, true))
            if (!(r = tryDir(JoinPath(JoinPath(home, u), L"exdata"), L"RPCS3 license folder")).empty()) return r;
    }
    if (!gameDir.empty()) {
        wstr g = gameDir;
        for (int up = 0; up < 3 && !g.empty(); ++up, g = ParentDir(g))
            if (!(r = tryDir(g, L"next to the game")).empty()) return r;
    }
    if (!(r = tryDir(ExeDir(), L"next to this launcher")).empty()) return r;
    if (!(r = tryDir(KnownFolder(FOLDERID_Downloads), L"Downloads")).empty()) return r;
    if (!(r = tryDir(KnownFolder(FOLDERID_Desktop), L"Desktop")).empty()) return r;
    return wstr();
}

wstr FindElf(const wstr& gameDir) {
    if (gameDir.empty()) return wstr();
    const wchar_t* c[] = { L"USRDIR\\EBOOT.ELF", L"EBOOT.ELF", L"..\\EBOOT.ELF" };
    for (auto s : c) { wstr p = FullPath(JoinPath(gameDir, s)); if (FileExists(p)) return p; }
    return wstr();
}

Detected DetectAll(const wstr& gameHint) {
    Detected d;
    FindRpcs3Dirs(d.rpcs3Dirs, d.log);
    std::vector<std::pair<wstr, wstr>> cand;   // (folder, where)
    if (!gameHint.empty()) cand.push_back({ gameHint, L"command line" });
    for (auto& h : Hdd0Dirs(d.rpcs3Dirs)) cand.push_back({ JoinPath(h, L"game\\NPUA80491"), L"RPCS3 (dev_hdd0)" });
    wstr e = ExeDir();
    for (int up = 0; up < 3 && !e.empty(); ++up, e = ParentDir(e)) cand.push_back({ e, L"next to this launcher" });
    wchar_t cwd[MAX_PATH]; GetCurrentDirectoryW(MAX_PATH, cwd);
    cand.push_back({ cwd, L"current folder" });
    for (auto b : { KnownFolder(FOLDERID_Downloads), KnownFolder(FOLDERID_Desktop), KnownFolder(FOLDERID_Documents) })
        if (!b.empty()) cand.push_back({ b, L"your user folders" });
    if (IsPkgPath(gameHint) && FileExists(gameHint)) {
        GameCheck g = CheckGamePkg(gameHint);
        d.log.push_back((g.status == GAME_OK ? L"Game package: " : L"Skipped package ") + g.dir + L": " + g.message);
        if (g.status == GAME_OK) { d.game = g.dir; d.gameFrom = L"command line"; }
    }
    for (auto& c : cand) {
        if (!d.game.empty()) break;
        if (!DirExists(c.first)) continue;
        GameCheck g = CheckGameDir(c.first);
        if (g.status == GAME_OK) {
            d.game = g.dir; d.gameFrom = c.second;
            d.log.push_back(L"Game: " + g.dir + L" (" + c.second + L")");
            break;
        }
        if (g.status == GAME_WRONG_TITLE || g.status == GAME_WRONG_VERSION) d.log.push_back(L"Skipped " + g.dir + L": " + g.message);
    }
    // No installed game: look for the PSN package (any *.pkg; the header and PARAM.SFO inside decide, not the name)
    // in Downloads, Desktop, Documents, next to the launcher, the current folder and the RPCS3 folders, each with one
    // level of subfolders.
    if (d.game.empty()) {
        std::vector<std::pair<wstr, wstr>> pc;
        e = ExeDir();
        for (int up = 0; up < 2 && !e.empty(); ++up, e = ParentDir(e)) pc.push_back({ e, L"next to this launcher" });
        pc.push_back({ cwd, L"current folder" });
        pc.push_back({ KnownFolder(FOLDERID_Downloads), L"Downloads" });
        pc.push_back({ KnownFolder(FOLDERID_Desktop), L"Desktop" });
        pc.push_back({ KnownFolder(FOLDERID_Documents), L"Documents" });
        for (auto& r : d.rpcs3Dirs) pc.push_back({ r, L"RPCS3 folder" });
        std::vector<wstr> seen;
        for (auto& c : pc) {
            if (c.first.empty() || !DirExists(c.first) || !d.game.empty()) continue;
            std::vector<wstr> dirs = { c.first };
            for (auto& s : ListDir(c.first, true)) dirs.push_back(JoinPath(c.first, s));
            for (auto& dir : dirs) {
                for (auto& f : ListDir(dir, false, L"*.pkg")) {
                    wstr p = FullPath(JoinPath(dir, f));
                    bool dup = false; for (auto& s : seen) dup |= IEquals(s, p);
                    if (dup) continue;
                    seen.push_back(p);
                    GameCheck g = CheckGamePkg(p);
                    if (g.status == GAME_OK) { d.game = p; d.gameFrom = c.second; d.log.push_back(L"Game package: " + p + L" (" + c.second + L")"); break; }
                    if (g.status != GAME_NOT_PS3) d.log.push_back(L"Skipped package " + p + L": " + g.message);
                }
                if (!d.game.empty()) break;
            }
        }
    }
    d.rap = FindRap(IsPkgPath(d.game) ? ParentDir(d.game) : d.game, d.rpcs3Dirs, &d.rapFrom);
    if (!d.rap.empty()) d.log.push_back(L"License: " + d.rap + L" (" + d.rapFrom + L")");
    if (!IsPkgPath(d.game)) d.elf = FindElf(d.game);
    if (!d.elf.empty()) d.log.push_back(L"EBOOT.ELF: " + d.elf);
    return d;
}

// ================================================================ SHA-256 (BCrypt)
bool Sha256File(const wstr& path, std::string& hexOut, std::atomic<bool>* cancel) {
    hexOut.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE h = nullptr;
    bool ok = false;
    std::vector<unsigned char> buf(1 << 20);
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0 &&
        BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
        ok = true;
        DWORD got;
        while (ReadFile(f, buf.data(), (DWORD)buf.size(), &got, nullptr) && got) {
            if (cancel && *cancel) { ok = false; break; }
            BCryptHashData(h, buf.data(), got, 0);
        }
        unsigned char dig[32];
        if (ok && BCryptFinishHash(h, dig, 32, 0) == 0) {
            static const char* hx = "0123456789abcdef";
            for (int i = 0; i < 32; ++i) { hexOut += hx[dig[i] >> 4]; hexOut += hx[dig[i] & 15]; }
        } else ok = false;
    }
    if (h) BCryptDestroyHash(h);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(f);
    return ok;
}

// ================================================================ JSON (small, strict enough for our inputs)
const JVal* JVal::get(const char* key) const {
    if (t != Obj) return nullptr;
    for (auto& kv : o) if (kv.first == key) return &kv.second;
    return nullptr;
}
std::string JVal::str(const char* key, const std::string& def) const {
    const JVal* v = get(key);
    if (!v) return def;
    if (v->t == Str) return v->s;
    if (v->t == Num) { char b[64]; snprintf(b, 64, "%.0f", v->n); return b; }
    return def;
}
double JVal::num(const char* key, double def) const {
    const JVal* v = get(key);
    if (!v) return def;
    if (v->t == Num) return v->n;
    if (v->t == Str) return atof(v->s.c_str());
    return def;
}
namespace {
struct JP {
    const char* p; const char* e; int depth = 0;
    void ws() { while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p; }
    bool lit(const char* s) { size_t n = strlen(s); if ((size_t)(e - p) < n || strncmp(p, s, n)) return false; p += n; return true; }
    static void utf8(std::string& o, unsigned c) {
        if (c < 0x80) o += (char)c;
        else if (c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xF0 | (c >> 18)); o += (char)(0x80 | ((c >> 12) & 63)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    }
    bool hex4(unsigned& c) {
        if (e - p < 4) return false;
        c = 0;
        for (int i = 0; i < 4; ++i) {
            char h = *p++; c <<= 4;
            if (h >= '0' && h <= '9') c |= h - '0'; else if (h >= 'a' && h <= 'f') c |= h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') c |= h - 'A' + 10; else return false;
        }
        return true;
    }
    bool str(std::string& o) {
        if (p >= e || *p != '"') return false;
        ++p;
        while (p < e && *p != '"') {
            if (*p == '\\') {
                ++p; if (p >= e) return false;
                char c = *p++;
                switch (c) {
                case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
                case 'b': o += '\b'; break; case 'f': o += '\f'; break;
                case 'u': {
                    unsigned u; if (!hex4(u)) return false;
                    if (u >= 0xD800 && u < 0xDC00 && e - p >= 6 && p[0] == '\\' && p[1] == 'u') {
                        p += 2; unsigned lo; if (!hex4(lo)) return false;
                        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, u); break;
                }
                default: o += c;
                }
            } else o += *p++;
        }
        if (p >= e) return false;
        ++p;
        return true;
    }
    bool val(JVal& v) {
        if (++depth > 64) return false;
        ws();
        if (p >= e) return false;
        bool ok = true;
        if (*p == '{') {
            v.t = JVal::Obj; ++p; ws();
            if (p < e && *p == '}') { ++p; }
            else for (;;) {
                ws(); std::string k;
                if (!str(k)) { ok = false; break; }
                ws(); if (p >= e || *p != ':') { ok = false; break; } ++p;
                v.o.push_back({ k, JVal() });
                if (!val(v.o.back().second)) { ok = false; break; }
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; break; }
                ok = false; break;
            }
        } else if (*p == '[') {
            v.t = JVal::Arr; ++p; ws();
            if (p < e && *p == ']') { ++p; }
            else for (;;) {
                v.a.push_back(JVal());
                if (!val(v.a.back())) { ok = false; break; }
                ws(); if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; break; }
                ok = false; break;
            }
        } else if (*p == '"') { v.t = JVal::Str; ok = str(v.s); }
        else if (lit("true")) { v.t = JVal::Bool; v.b = true; }
        else if (lit("false")) { v.t = JVal::Bool; v.b = false; }
        else if (lit("null")) { v.t = JVal::Null; }
        else {
            char* end = nullptr;
            std::string tmp(p, std::min<size_t>(64, e - p));
            v.n = strtod(tmp.c_str(), &end);
            if (end == tmp.c_str()) ok = false;
            else { v.t = JVal::Num; p += end - tmp.c_str(); }
        }
        --depth;
        return ok;
    }
};
}
bool JParse(const std::string& text, JVal& out) {
    JP jp{ text.data(), text.data() + text.size() };
    out = JVal();
    return jp.val(out);
}

// ================================================================ install paths
void InstallPaths::Init(const wstr& installDir) {
    root = FullPath(installDir);
    tools = JoinPath(root, L"tools");
    python = JoinPath(tools, L"python");
    pythonExe = JoinPath(python, L"python.exe");
    scripts = JoinPath(root, L"scripts");
    downloads = JoinPath(root, L"downloads");
    log = JoinPath(root, L"build.log");
    state = JoinPath(root, L"launcher_state.ini");
}
wstr DefaultInstallDir() { return JoinPath(KnownFolder(FOLDERID_LocalAppData), L"GoW2-Windows"); }
wstr LoadInstallDir() {
    wchar_t buf[MAX_PATH * 2]; DWORD n = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, REG_KEY, L"InstallDir", RRF_RT_REG_SZ, nullptr, buf, &n) == ERROR_SUCCESS && buf[0])
        return buf;
    return DefaultInstallDir();
}
void SaveInstallDir(const wstr& dir) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    RegSetValueExW(k, L"InstallDir", 0, REG_SZ, (const BYTE*)dir.c_str(), (DWORD)((dir.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}
