// Gow2HD-Windows launcher - WinHTTP downloads (resumable, verified), child processes, archive extraction, updates.
#include "common.h"
#include <winhttp.h>
#include <mutex>

// ================================================================ HTTP
std::function<void(const wstr&)> g_netLog;
struct UrlParts { wstr host, path; INTERNET_PORT port = 443; bool https = true; };
static bool Crack(const wstr& url, UrlParts& u) {
    URL_COMPONENTS c = {}; c.dwStructSize = sizeof(c);
    wchar_t host[256], path[4096];
    c.lpszHostName = host; c.dwHostNameLength = 256;
    c.lpszUrlPath = path; c.dwUrlPathLength = 4096;
    wchar_t extra[2048]; c.lpszExtraInfo = extra; c.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &c)) return false;
    u.host.assign(host, c.dwHostNameLength);
    u.path.assign(path, c.dwUrlPathLength);
    u.path.append(extra, c.dwExtraInfoLength);
    u.port = c.nPort;
    u.https = c.nScheme == INTERNET_SCHEME_HTTPS;
    return true;
}
static wstr LastErr(const wchar_t* what) {
    DWORD e = GetLastError();
    if (e == ERROR_WINHTTP_TIMEOUT) return Fmt(L"%s: the server stopped responding (timeout)", what);
    if (e == ERROR_WINHTTP_NAME_NOT_RESOLVED) return Fmt(L"%s: no internet connection (name not resolved)", what);
    if (e == ERROR_WINHTTP_CANNOT_CONNECT) return Fmt(L"%s: could not connect", what);
    if (e == ERROR_WINHTTP_SECURE_FAILURE) return Fmt(L"%s: secure connection failed", what);
    return Fmt(L"%s: error %lu", what, e);
}

struct Http {
    HINTERNET ses = nullptr, con = nullptr, req = nullptr;
    ~Http() { if (req) WinHttpCloseHandle(req); if (con) WinHttpCloseHandle(con); if (ses) WinHttpCloseHandle(ses); }
    bool Open(const wstr& url, const wstr& headers, wstr& err) {
        UrlParts u;
        if (!Crack(url, u)) { err = L"Bad URL: " + url; return false; }
        ses = WinHttpOpen(L"Gow2HD-Windows-Launcher/" LAUNCHER_VERSION_W, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) ses = WinHttpOpen(L"Gow2HD-Windows-Launcher/" LAUNCHER_VERSION_W, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) { err = LastErr(L"WinHttpOpen"); return false; }
        DWORD protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | 0x2000 /* TLS1_3 */;
        if (!WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos))) {
            protos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protos, sizeof(protos));
        }
        WinHttpSetTimeouts(ses, 15000, 20000, 30000, 60000);
        con = WinHttpConnect(ses, u.host.c_str(), u.port, 0);
        if (!con) { err = LastErr(L"Connect"); return false; }
        req = WinHttpOpenRequest(con, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 u.https ? WINHTTP_FLAG_SECURE : 0);
        if (!req) { err = LastErr(L"Request"); return false; }
        if (!WinHttpSendRequest(req, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                headers.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(req, nullptr)) { err = LastErr(L"Download"); return false; }
        return true;
    }
    DWORD Status() {
        DWORD s = 0, n = sizeof(s);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &s, &n, nullptr);
        return s;
    }
    uint64_t Length() {
        wchar_t b[64]; DWORD n = sizeof(b);
        if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, b, &n, nullptr)) return 0;
        return _wcstoui64(b, nullptr, 10);
    }
};

bool HttpGet(const wstr& url, std::string& body, DWORD& status, wstr& err, const wchar_t* extraHeaders) {
    Http h; body.clear(); status = 0;
    if (!h.Open(url, extraHeaders ? extraHeaders : L"", err)) return false;
    status = h.Status();
    char buf[16384]; DWORD got;
    while (WinHttpReadData(h.req, buf, sizeof(buf), &got) && got) {
        body.append(buf, got);
        if (body.size() > (8u << 20)) { err = L"Response too large"; return false; }
    }
    return true;
}

static bool DownloadOnce(const wstr& url, const wstr& part, uint64_t expectSize, const ProgressFn& progress,
                         std::atomic<bool>* cancel, wstr& err) {
    uint64_t have = FileSize(part);
    if (expectSize && have > expectSize) { DeleteFileW(part.c_str()); have = 0; }
    if (expectSize && have == expectSize) return true;
    wstr headers;
    if (have) headers = Fmt(L"Range: bytes=%llu-\r\n", (unsigned long long)have);
    Http h;
    if (!h.Open(url, headers, err)) return false;
    DWORD st = h.Status();
    bool append = false;
    if (st == 206 && have) {
        append = true;
        if (g_netLog) g_netLog(Fmt(L"Resuming %s at %s (HTTP 206)", FileNameOf(part).c_str(), FormatBytes(have).c_str()));
    }
    else if (st == 416 && have) return true;            // already complete; the hash check decides
    else if (st != 200) { err = Fmt(L"Server answered HTTP %lu for %s", st, url.c_str()); return false; }
    uint64_t total = h.Length();
    if (append) total += have; else have = 0;
    if (!total) total = expectSize;
    HANDLE f = CreateFileW(part.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { err = L"Cannot write " + part; return false; }
    std::vector<char> buf(256 * 1024);
    DWORD got = 0, lastTick = 0;
    bool ok = true;
    for (;;) {
        if (cancel && *cancel) { err = L"Cancelled"; ok = false; break; }
        if (!WinHttpReadData(h.req, buf.data(), (DWORD)buf.size(), &got)) { err = LastErr(L"Download interrupted"); ok = false; break; }
        if (!got) break;
        DWORD put;
        if (!WriteFile(f, buf.data(), got, &put, nullptr) || put != got) { err = L"Disk write failed (disk full?)"; ok = false; break; }
        have += got;
        DWORD now = GetTickCount();
        if (progress && now - lastTick > 100) { lastTick = now; progress(have, total); }
    }
    CloseHandle(f);
    if (progress) progress(have, total);
    if (ok && total && have != total) { err = L"Download ended early"; ok = false; }
    return ok;
}

bool DownloadVerified(const wstr& url, const wstr& dest, const std::string& sha256, uint64_t size,
                      const ProgressFn& progress, std::atomic<bool>* cancel, wstr& err) {
    std::string want = ToLowerA(Trim(sha256)), got;
    if (want.compare(0, 7, "sha256:") == 0) want = want.substr(7);
    if (FileExists(dest)) {                              // reuse a finished, verified download
        if ((!size || FileSize(dest) == size) && Sha256File(dest, got, cancel) && (want.empty() || got == want)) return true;
        DeleteFileW(dest.c_str());
    }
    MakeDirs(ParentDir(dest));
    wstr part = dest + L".part";
    bool restarted = false;
    for (int attempt = 1; attempt <= 5; ++attempt) {
        if (cancel && *cancel) { err = L"Cancelled"; return false; }
        if (!DownloadOnce(url, part, size, progress, cancel, err)) {
            if (cancel && *cancel) return false;
            if (g_netLog) g_netLog(Fmt(L"Attempt %d failed: %s", attempt, err.c_str()));
            Sleep(1000 * attempt);                      // keep the .part, resume next time
            continue;
        }
        if (size && FileSize(part) != size) {
            err = Fmt(L"Size mismatch: got %llu bytes, expected %llu", (unsigned long long)FileSize(part), (unsigned long long)size);
        } else if (!Sha256File(part, got, cancel)) {
            err = L"Could not hash " + part;
        } else if (!want.empty() && got != want) {
            err = Fmt(L"Checksum mismatch for %s\nexpected %s\ngot      %s", FileNameOf(dest).c_str(),
                      Widen(want).c_str(), Widen(got).c_str());
        } else {
            DeleteFileW(dest.c_str());
            if (!MoveFileW(part.c_str(), dest.c_str())) { err = L"Cannot rename " + part; return false; }
            return true;
        }
        if (g_netLog) g_netLog(err + L" -- deleting the download and starting over");
        DeleteFileW(part.c_str());                       // bad bytes: start over once, then give up
        if (restarted) return false;
        restarted = true;
    }
    return false;
}

// ================================================================ processes
static std::mutex g_jobMx;
static HANDLE g_job = nullptr;

void CancelRunningProcess() {
    std::lock_guard<std::mutex> lk(g_jobMx);
    if (g_job) TerminateJobObject(g_job, 1);
}

bool RunProcess(const wstr& cmdline, const wstr& cwd, const LineFn& onLine, DWORD& exitCode, std::atomic<bool>* cancel) {
    exitCode = (DWORD)-1;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 1 << 16)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul; si.hStdOutput = wr; si.hStdError = wr;
    PROCESS_INFORMATION pi = {};
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
    li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end()); cmd.push_back(0);
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                             nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); CloseHandle(job); return false; }
    AssignProcessToJobObject(job, pi.hProcess);
    { std::lock_guard<std::mutex> lk(g_jobMx); g_job = job; }
    if (cancel && *cancel) TerminateJobObject(job, 1);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    std::string acc;
    char buf[8192]; DWORD got;
    while (ReadFile(rd, buf, sizeof(buf), &got, nullptr) && got) {
        acc.append(buf, got);
        size_t nl;
        while ((nl = acc.find('\n')) != std::string::npos) {
            std::string line = acc.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            acc.erase(0, nl + 1);
            if (onLine) onLine(line);
        }
    }
    if (!acc.empty() && onLine) onLine(acc);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    { std::lock_guard<std::mutex> lk(g_jobMx); g_job = nullptr; }
    CloseHandle(job);   // kills anything the child left behind
    return true;
}

bool IsArchiveName(const wstr& n) {
    for (auto s : { L".zip", L".tar", L".tar.gz", L".tgz", L".tar.xz", L".txz", L".tar.bz2", L".tbz2", L".7z" })
        if (EndsWithI(n, s)) return true;
    return false;
}

// Windows 10 1803+ ships bsdtar as System32\tar.exe; it reads zip, tar.* and 7z. PowerShell is the fallback for zip.
bool ExtractArchive(const wstr& archive, const wstr& destDir, std::atomic<bool>* cancel, wstr& err) {
    MakeDirs(destDir);
    wstr sys = GetEnv(L"SystemRoot");
    if (sys.empty()) sys = L"C:\\Windows";
    wstr tar = JoinPath(sys, L"System32\\tar.exe");
    std::string tail;
    LineFn keep = [&](const std::string& l) { if (tail.size() < 4000) tail += l + "\n"; };
    DWORD code = 1;
    if (FileExists(tar)) {
        wstr cmd = ArgQuote(tar) + L" -xf " + ArgQuote(archive) + L" -C " + ArgQuote(destDir);
        if (RunProcess(cmd, destDir, keep, code, cancel) && code == 0) return true;
        if (cancel && *cancel) { err = L"Cancelled"; return false; }
    }
    if (EndsWithI(archive, L".zip")) {
        wstr ps = JoinPath(sys, L"System32\\WindowsPowerShell\\v1.0\\powershell.exe");
        auto esc = [](wstr s) { wstr r; for (auto c : s) { if (c == L'\'') r += L"''"; else r += c; } return r; };
        wstr cmd = ArgQuote(ps) + L" -NoProfile -NonInteractive -Command \"Expand-Archive -LiteralPath '" + esc(archive) +
                   L"' -DestinationPath '" + esc(destDir) + L"' -Force\"";
        tail.clear();
        if (RunProcess(cmd, destDir, keep, code, cancel) && code == 0) return true;
    }
    err = L"Could not extract " + FileNameOf(archive) + L": " + Widen(Trim(tail));
    return false;
}

// ================================================================ updates
int CompareVersions(const std::string& a, const std::string& b) {
    auto parts = [](const std::string& v) {
        std::vector<long> r; std::string s = v;
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s = s.substr(1);
        size_t i = 0;
        while (i < s.size() && r.size() < 4) {
            if (!isdigit((unsigned char)s[i])) break;
            r.push_back(strtol(s.c_str() + i, nullptr, 10));
            while (i < s.size() && isdigit((unsigned char)s[i])) ++i;
            if (i < s.size() && s[i] == '.') ++i; else break;
        }
        while (r.size() < 4) r.push_back(0);
        return r;
    };
    auto x = parts(a), y = parts(b);
    for (int i = 0; i < 4; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

UpdateInfo CheckForUpdate() {
    UpdateInfo u;
    std::string body; DWORD st;
    wstr repo = GetEnv(L"GOW2_UPDATE_REPO");                       // test override: owner/repo
    if (repo.empty()) repo = GITHUB_REPO_W;
    wstr url = L"https://api.github.com/repos/" + repo + L"/releases/latest";
    if (!HttpGet(url, body, st, u.err, L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n")) return u;
    // 404 = the repository has no published release yet (or is not public yet): not an error, this copy is current.
    if (st == 404) { u.ok = true; u.noRelease = true; u.pageUrl = "https://github.com/" + Narrow(repo); return u; }
    if (st == 403 || st == 429) { u.err = L"GitHub rate limit reached. Try again in an hour."; return u; }
    if (st != 200) { u.err = Fmt(L"GitHub answered HTTP %lu.", st); return u; }
    JVal j;
    if (!JParse(body, j) || j.t != JVal::Obj) { u.err = L"Unexpected answer from GitHub."; return u; }
    u.tag = j.str("tag_name");
    u.pageUrl = j.str("html_url");
    if (const JVal* as = j.get("assets")) for (auto& a : as->a) {
        if (_stricmp(a.str("name").c_str(), "Gow2HD-Windows.exe") == 0) {
            u.assetUrl = a.str("browser_download_url");
            u.sha256 = a.str("digest");   // "sha256:..." on current GitHub
        }
    }
    u.ok = !u.tag.empty();
    u.newer = u.ok && CompareVersions(u.tag, LAUNCHER_VERSION) > 0;
    if (!u.ok) u.err = L"The latest release has no version tag.";
    return u;
}

// Quiver writes version.txt next to the exe it installed and updates the exe itself.
bool ManagedByQuiver() {
    return FileExists(JoinPath(ExeDir(), L"version.txt")) || ToLowerW(ExeDir()).find(L"\\quiver") != wstr::npos;
}
