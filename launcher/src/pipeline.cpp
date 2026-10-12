// Gow2HD-Windows launcher - the build pipeline: scripts -> Python -> tools -> build.py, and launching the result.
#include "common.h"
#include <time.h>
#include <mutex>

static std::mutex g_logMx;
static wstr g_logPath;
// set only while build.py runs; removed again so the game never inherits them
static const wchar_t* const kLauncherOnlyEnv[] = { L"PYTHONUNBUFFERED", L"PYTHONIOENCODING", L"PYTHONNOUSERSITE",
                                                    L"GOW2_LAUNCHER_VERSION" };

static std::string Stamp() {
    SYSTEMTIME t; GetLocalTime(&t);
    char b[32]; snprintf(b, 32, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
    return b;
}
static void LogRaw(const std::string& line) {
    std::lock_guard<std::mutex> lk(g_logMx);
    if (!g_logPath.empty()) AppendFileBytes(g_logPath, line + "\r\n");
}

struct Ctx {
    const BuildEvents& ev;
    std::atomic<bool>* cancel;
    InstallPaths ip;
    int pct = 0;
    void Say(const wstr& msg) { LogRaw(Stamp() + "[launcher] " + Narrow(msg)); if (ev.logLine) ev.logLine(msg); }
    void Step(int p, const wstr& step) { pct = p; if (ev.progress) ev.progress(p, step); }
    bool Cancelled() { return cancel && *cancel; }
};

// ---------------------------------------------------------------- build scripts
static bool PrepareScripts(Ctx& c, wstr& scriptsDir, wstr& err) {
    wstr dev = GetEnv(L"GOW2_SCRIPTS_DIR");               // developer override: use a folder as-is
    if (!dev.empty()) {
        scriptsDir = FullPath(dev);
        c.Say(L"Using build scripts from GOW2_SCRIPTS_DIR: " + scriptsDir);
        return FileExists(JoinPath(scriptsDir, L"build.py")) || (err = L"No build.py in GOW2_SCRIPTS_DIR", false);
    }
    wstr zip = JoinPath(c.ip.downloads, L"build-scripts.zip");
    MakeDirs(c.ip.downloads);
#if SCRIPTS_BUNDLED
    HRSRC r = FindResourceW(nullptr, L"SCRIPTS", (LPCWSTR)RT_RCDATA);
    HGLOBAL g = r ? LoadResource(nullptr, r) : nullptr;
    DWORD n = r ? SizeofResource(nullptr, r) : 0;
    const char* data = g ? (const char*)LockResource(g) : nullptr;
    if (!data || !n) { err = L"This launcher has no build scripts inside (bad build)."; return false; }
    if (!WriteFileBytes(zip, std::string(data, n))) { err = L"Cannot write " + zip; return false; }
#else
    c.Step(c.pct, L"Downloading build scripts");
    if (!DownloadVerified(SCRIPTS_URL, zip, SCRIPTS_SHA256, 0, nullptr, c.cancel, err)) return false;
#endif
    std::string sha;
    Sha256File(zip, sha);
    std::string old;
    ReadFileBytes(JoinPath(c.ip.scripts, L".bundle.sha256"), old);
    scriptsDir = c.ip.scripts;
    if (Trim(old) == sha && FileExists(JoinPath(scriptsDir, L"build.py"))) { DeleteFileW(zip.c_str()); return true; }
    c.Say(L"Unpacking build scripts (" + Widen(sha.substr(0, 12)) + L")");
    DeleteTree(c.ip.scripts);                              // our own folder; replaced as a whole
    if (!ExtractArchive(zip, c.ip.scripts, c.cancel, err)) return false;
    DeleteFileW(zip.c_str());
    if (!FileExists(JoinPath(scriptsDir, L"build.py"))) {   // zip with one top folder
        for (auto& d : ListDir(c.ip.scripts, true))
            if (FileExists(JoinPath(JoinPath(c.ip.scripts, d), L"build.py"))) { scriptsDir = JoinPath(c.ip.scripts, d); break; }
    }
    if (!FileExists(JoinPath(scriptsDir, L"build.py"))) { err = L"build.py missing from the build scripts."; return false; }
    WriteFileBytes(JoinPath(c.ip.scripts, L".bundle.sha256"), sha);
    return true;
}

// ---------------------------------------------------------------- python
static wstr StampPath(const InstallPaths& ip, const wstr& name) {
    wstr safe;
    for (auto ch : name) safe += (iswalnum(ch) || ch == L'-' || ch == L'_' || ch == L'.') ? ch : L'_';
    return JoinPath(JoinPath(ip.tools, L".stamps"), safe + L".txt");
}
static bool StampOk(const InstallPaths& ip, const wstr& name, const std::string& value) {
    std::string s;
    return ReadFileBytes(StampPath(ip, name), s) && Trim(s) == value;
}
static void SetStamp(const InstallPaths& ip, const wstr& name, const std::string& value) {
    MakeDirs(JoinPath(ip.tools, L".stamps"));
    WriteFileBytes(StampPath(ip, name), value);
}

static ProgressFn BytesTo(Ctx& c, int from, int to, const wstr& label) {
    return [&c, from, to, label](uint64_t done, uint64_t total) {
        int p = total ? from + (int)((to - from) * (double)done / (double)total) : from;
        wstr s = label + L"  " + FormatBytes(done) + (total ? L" / " + FormatBytes(total) : L"");
        if (c.ev.progress) c.ev.progress(p, s);
    };
}

static bool PreparePython(Ctx& c, const wstr& scriptsDir, wstr& err) {
    if (!(FileExists(c.ip.pythonExe) && StampOk(c.ip, L"python", PYTHON_SHA256))) {
        wstr zip = JoinPath(c.ip.downloads, L"python-3.12.10-embed-amd64.zip");
        c.Say(L"Downloading Python 3.12.10 (embeddable, python.org)");
        if (!DownloadVerified(PYTHON_URL, zip, PYTHON_SHA256, PYTHON_SIZE, BytesTo(c, 2, 5, L"Downloading Python"), c.cancel, err))
            return false;
        c.Say(L"Python verified: sha256 " + Widen(PYTHON_SHA256));
        DeleteTree(c.ip.python);
        c.Step(5, L"Unpacking Python");
        if (!ExtractArchive(zip, c.ip.python, c.cancel, err)) return false;
        if (!FileExists(c.ip.pythonExe)) { err = L"python.exe missing after unpacking."; return false; }
        SetStamp(c.ip, L"python", PYTHON_SHA256);
        DeleteFileW(zip.c_str());
    }
    // The embeddable Python ignores the script's folder unless it is listed in its ._pth file.
    std::string pth = "python312.zip\r\n.\r\n" + Narrow(scriptsDir) + "\r\n";
    return WriteFileBytes(JoinPath(c.ip.python, PYTHON_PTH), pth) || (err = L"Cannot write the Python path file.", false);
}

static wstr PyCmd(const InstallPaths& ip, const wstr& scriptsDir) {
    return ArgQuote(ip.pythonExe) + L" -u -X utf8 " + ArgQuote(JoinPath(scriptsDir, L"build.py"));
}

// ---------------------------------------------------------------- tools
struct Tool { wstr name, url, extractTo; std::string sha256; uint64_t size = 0; };

static bool ReadToolList(Ctx& c, const wstr& scriptsDir, std::vector<Tool>& tools, wstr& err) {
    std::string out;
    DWORD code;
    c.Say(L"Asking build.py which tools it needs (--print-tools)");
    if (!RunProcess(PyCmd(c.ip, scriptsDir) + L" --print-tools", scriptsDir,
                    [&](const std::string& l) { out += l + "\n"; LogRaw("  " + l); }, code, c.cancel)) {
        err = L"Could not start Python."; return false;
    }
    if (c.Cancelled()) { err = L"Cancelled"; return false; }
    if (code != 0) { err = Fmt(L"build.py --print-tools failed (exit code %lu). See the log.", code); return false; }
    size_t a = out.find_first_of("[{");
    JVal j;
    if (a == std::string::npos || !JParse(out.substr(a), j)) { err = L"build.py --print-tools did not print valid JSON."; return false; }
    const JVal* arr = j.t == JVal::Arr ? &j : j.get("tools");
    if (!arr || arr->t != JVal::Arr) { err = L"build.py --print-tools: expected a JSON list of tools."; return false; }
    for (auto& t : arr->a) {
        Tool x;
        x.name = Widen(t.str("name"));
        x.url = Widen(t.str("url"));
        x.sha256 = ToLowerA(t.str("sha256"));
        x.size = (uint64_t)t.num("size");
        x.extractTo = Widen(t.str("extract-to", t.str("extract_to", t.str("dest"))));
        for (auto& ch : x.extractTo) if (ch == L'/') ch = L'\\';
        if (x.name.empty() || x.url.empty()) { err = L"build.py --print-tools: a tool has no name or url."; return false; }
        if (x.sha256.size() != 64) { err = L"build.py --print-tools: tool " + x.name + L" has no sha256; refusing an unverified download."; return false; }
        if (x.extractTo.find(L"..") != wstr::npos || (x.extractTo.size() > 1 && x.extractTo[1] == L':')) {
            err = L"build.py --print-tools: bad extract-to for " + x.name; return false;
        }
        tools.push_back(x);
    }
    return true;
}

static bool FetchTools(Ctx& c, const std::vector<Tool>& tools, wstr& err) {
    uint64_t totalBytes = 0, doneBytes = 0;
    std::vector<bool> need(tools.size());
    for (size_t i = 0; i < tools.size(); ++i) {
        const Tool& t = tools[i];
        wstr target = t.extractTo.empty() ? c.ip.tools : JoinPath(c.ip.tools, t.extractTo);
        need[i] = !(StampOk(c.ip, t.name, t.sha256 + "|" + Narrow(t.extractTo)) && DirExists(target));
        if (need[i]) totalBytes += t.size ? t.size : 1;
        else c.Say(L"Tool already present: " + t.name);
    }
    for (size_t i = 0; i < tools.size(); ++i) {
        if (!need[i]) continue;
        const Tool& t = tools[i];
        if (c.Cancelled()) { err = L"Cancelled"; return false; }
        wstr file = FileNameOf(t.url);
        size_t q = file.find_first_of(L"?#");
        if (q != wstr::npos) file = file.substr(0, q);
        if (file.empty()) file = t.name;
        wstr dl = JoinPath(c.ip.downloads, file);
        int from = 5 + (int)(25.0 * doneBytes / (double)(totalBytes ? totalBytes : 1));
        int to = 5 + (int)(25.0 * (doneBytes + (t.size ? t.size : 1)) / (double)(totalBytes ? totalBytes : 1));
        wstr label = Fmt(L"Downloading %s", t.name.c_str());
        c.Say(label + L" from " + t.url);
        if (!DownloadVerified(t.url, dl, t.sha256, t.size, BytesTo(c, from, to, label), c.cancel, err)) {
            err = L"Download of " + t.name + L" failed: " + err; return false;
        }
        c.Say(L"Verified " + t.name + L": sha256 " + Widen(t.sha256));
        wstr target = t.extractTo.empty() ? c.ip.tools : JoinPath(c.ip.tools, t.extractTo);
        if (IsArchiveName(file)) {
            if (!t.extractTo.empty()) DeleteTree(target);   // stale copy of this tool (never the tools root)
            c.Step(to, L"Unpacking " + t.name);
            if (!ExtractArchive(dl, target, c.cancel, err)) return false;
            DeleteFileW(dl.c_str());
        } else {
            MakeDirs(target);
            wstr dst = JoinPath(target, file);
            DeleteFileW(dst.c_str());
            if (!MoveFileW(dl.c_str(), dst.c_str())) { err = L"Cannot move " + file; return false; }
        }
        SetStamp(c.ip, t.name, t.sha256 + "|" + Narrow(t.extractTo));
        doneBytes += t.size ? t.size : 1;
    }
    return true;
}

// ---------------------------------------------------------------- whole pipeline
int RunBuildPipeline(const BuildRequest& req, const BuildEvents& ev, std::atomic<bool>* cancel, wstr& err) {
    Ctx c{ ev, cancel };
    c.ip.Init(req.install);
    g_netLog = [&c](const wstr& m) { c.Say(m); };
    struct ResetNetLog { ~ResetNetLog() { g_netLog = nullptr; } } resetNetLog;
    if (!MakeDirs(c.ip.root) || !DirWritable(c.ip.root)) { err = L"Cannot write to the install folder " + c.ip.root; return 2; }
    MakeDirs(c.ip.tools); MakeDirs(c.ip.downloads);
    {
        std::lock_guard<std::mutex> lk(g_logMx);
        g_logPath = c.ip.log;
        wstr prev = JoinPath(c.ip.root, L"build.prev.log");
        DeleteFileW(prev.c_str());
        MoveFileW(c.ip.log.c_str(), prev.c_str());
    }
    time_t now = time(nullptr);
    char tbuf[64]; strftime(tbuf, 64, "%Y-%m-%d %H:%M:%S", localtime(&now));
    LogRaw(std::string("Gow2HD-Windows launcher " LAUNCHER_VERSION " build log, ") + tbuf);
    c.Say(L"Game:    " + req.game);
    c.Say(L"License: " + req.rap);
    if (!req.elf.empty()) c.Say(L"ELF:     " + req.elf);
    c.Say(L"Install: " + c.ip.root);

    KvFile st; st.Load(c.ip.state);
    st.Set("built", "0");
    st.SetW("game", req.game); st.SetW("rap", req.rap); st.SetW("elf", req.elf);
    st.Save(c.ip.state);

    GameCheck g = CheckGameInput(req.game);
    if (g.status != GAME_OK) { err = g.message; c.Say(L"ERROR " + err); return 3; }
    wstr rmsg;
    if (!CheckRapFile(req.rap, rmsg)) { err = L"License file: " + rmsg; c.Say(L"ERROR " + err); return 3; }
    uint64_t freeB = FreeSpace(c.ip.root);
    c.Say(L"Free space: " + FormatBytes(freeB));
    if (g.isPkg) c.Say(L"Game package: the game files are extracted into " + PkgGameDir(c.ip.root) +
                       L" (about " + FormatBytes(PKG_EXTRA_BYTES) + L"; they stay, the game reads them while it runs)");

    auto fail = [&](int code) { c.Say(L"ERROR " + err); return code; };
    c.Step(1, L"Preparing build scripts");
    wstr scriptsDir;
    if (!PrepareScripts(c, scriptsDir, err)) return fail(4);
    c.Step(2, L"Preparing Python");
    if (!PreparePython(c, scriptsDir, err)) return fail(c.Cancelled() ? 9 : 5);
    std::vector<Tool> tools;
    c.Step(5, L"Reading the tool list");
    if (!ReadToolList(c, scriptsDir, tools, err)) return fail(c.Cancelled() ? 9 : 6);
    if (!FetchTools(c, tools, err)) return fail(c.Cancelled() ? 9 : 7);

    // build.py itself
    c.Step(30, L"Building the port");
    wstr cmd = PyCmd(c.ip, scriptsDir) + (g.isPkg ? L" --pkg " : L" --game ") + ArgQuote(g.dir) + L" --rap " + ArgQuote(req.rap);
    if (!req.elf.empty()) cmd += L" --elf " + ArgQuote(req.elf);
    cmd += L" --tools " + ArgQuote(c.ip.tools) + L" --out " + ArgQuote(c.ip.root);
    if (req.jobs > 0) cmd += Fmt(L" --jobs %d", req.jobs);
    c.Say(L"Running: " + cmd);
    SetEnvironmentVariableW(L"PYTHONUNBUFFERED", L"1");
    SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
    SetEnvironmentVariableW(L"PYTHONNOUSERSITE", L"1");
    SetEnvironmentVariableW(L"GOW2_LAUNCHER_VERSION", LAUNCHER_VERSION_W);
    wstr lastError, builtExe;
    DWORD code = 1;
    bool started = RunProcess(cmd, scriptsDir, [&](const std::string& line) {
        LogRaw(line);
        if (line.compare(0, 9, "PROGRESS ") == 0) {
            const char* p = line.c_str() + 9;
            char* end;
            long v = strtol(p, &end, 10);
            if (end != p) {
                if (v < 0) v = 0; if (v > 100) v = 100;
                std::string msg = Trim(end);
                c.Step(30 + (int)(v * 70 / 100), Widen(msg));
            }
        } else if (line.compare(0, 6, "ERROR ") == 0) {
            lastError = Widen(Trim(line.substr(6)));
        } else if (line.compare(0, 4, "EXE ") == 0) {
            builtExe = Widen(Trim(line.substr(4)));
        } else if (ev.logLine) ev.logLine(Widen(line));
    }, code, cancel);
    for (auto k : kLauncherOnlyEnv) SetEnvironmentVariableW(k, nullptr);   // build only: never inherited by the game
    if (!started) { err = L"Could not start the build (python.exe)."; return fail(8); }
    if (c.Cancelled()) { err = L"Build cancelled."; return fail(9); }
    if (code != 0) {
        err = lastError.empty() ? Fmt(L"The build failed (exit code %lu). Open the log for details.", code) : lastError;
        return fail(10);
    }
    if (!builtExe.empty() && !(builtExe.size() > 1 && builtExe[1] == L':')) builtExe = JoinPath(c.ip.root, builtExe);
    st.Load(c.ip.state);
    if (!builtExe.empty()) st.SetW("exe", FullPath(builtExe));
    wstr exe = FindBuiltExe(c.ip, st);
    if (exe.empty()) { err = L"The build finished but no game exe was found in " + c.ip.root; return fail(11); }
    now = time(nullptr);
    strftime(tbuf, 64, "%Y-%m-%d %H:%M", localtime(&now));
    st.Set("built", "1");
    st.SetW("exe", exe);
    st.Set("built_at", tbuf);
    st.Set("launcher_version", LAUNCHER_VERSION);
    st.Save(c.ip.state);
    c.Step(100, L"Done");
    c.Say(L"Build finished: " + exe);
    return 0;
}

static bool IsOurLauncher(const wstr& name) {
    wstr n = ToLowerW(name);
    return n == L"gow2-windows.exe" || n.find(L"unins") == 0 || n == L"python.exe" || n == L"pythonw.exe";
}

wstr FindBuiltExe(const InstallPaths& ip, const KvFile& state) {
    wstr e = state.GetW("exe");
    if (!e.empty() && FileExists(e)) return e;
    wstr best; uint64_t bestSize = 0;
    for (auto sub : { L"", L"bin", L"game", L"port" }) {
        wstr d = *sub ? JoinPath(ip.root, sub) : ip.root;
        for (auto& f : ListDir(d, false, L"*.exe")) {
            if (IsOurLauncher(f)) continue;
            wstr p = JoinPath(d, f);
            uint64_t s = FileSize(p);
            if (s > bestSize) { bestSize = s; best = p; }
        }
        if (!best.empty()) break;
    }
    return best;
}

bool IsBuilt(const InstallPaths& ip) {
    KvFile st;
    if (!st.Load(ip.state) || st.Get("built") != "1") return false;
    return !FindBuiltExe(ip, st).empty();
}

// The game exe configures itself (winport/host/wp_release.c): release defaults compiled in, the game folder from
// gow2_port.ini (written by build.py), port_settings.ini next to it, its own folder as working directory. The launcher
// therefore passes NO arguments and NO environment of its own: the game sees the player's plain environment.
bool LaunchGame(const InstallPaths& ip, wstr& err) {
    KvFile st; st.Load(ip.state);
    wstr exe = FindBuiltExe(ip, st);
    if (exe.empty()) { err = L"The port is not built yet."; return false; }
    wstr exeDir = ParentDir(exe);
    wstr game = st.GetW("game");
    if (IsPkgPath(game)) game = PkgGameDir(ip.root);   // built from a package: the game runs from the extracted files
    GameCheck g = CheckGameDir(game);
    if (g.status != GAME_OK) { err = L"The game folder is gone or changed:\n" + game + L"\n\n" + g.message; return false; }
    for (auto k : kLauncherOnlyEnv) SetEnvironmentVariableW(k, nullptr);
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    wstr cmd = ArgQuote(exe);
    std::vector<wchar_t> cl(cmd.begin(), cmd.end()); cl.push_back(0);
    if (!CreateProcessW(exe.c_str(), cl.data(), nullptr, nullptr, FALSE, 0, nullptr, exeDir.c_str(), &si, &pi)) {
        err = Fmt(L"Could not start %s (error %lu).", exe.c_str(), GetLastError());
        return false;
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return true;
}

// ---------------------------------------------------------------- remove build
static wstr NormDir(const wstr& p) {          // lower-case full path with a trailing backslash, for containment tests
    wstr f = ToLowerW(FullPath(p));
    for (auto& ch : f) if (ch == L'/') ch = L'\\';
    if (f.empty() || f.back() != L'\\') f += L'\\';
    return f;
}
static bool Within(const wstr& inner, const wstr& outer) {   // inner == outer or inner below outer
    wstr a = NormDir(inner), b = NormDir(outer);
    return a.compare(0, b.size(), b) == 0;
}
static uint64_t TreeBytes(const wstr& dir) {
    uint64_t n = 0;
    for (auto& f : ListDir(dir, false)) n += FileSize(JoinPath(dir, f));
    for (auto& d : ListDir(dir, true)) {
        wstr dp = JoinPath(dir, d);
        if (!(GetFileAttributesW(dp.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) n += TreeBytes(dp);
    }
    return n;
}
// gow2_port.ini: "[env]" KEY=VALUE lines (the game applies them as defaults). Returns the value of key, and whether
// the [env] section has any entry at all.
static std::string PortIniEnv(const wstr& ini, const char* key, bool* anyEnv) {
    KvFile k; std::string val; bool inEnv = false;
    if (anyEnv) *anyEnv = false;
    if (!k.Load(ini)) return val;
    for (auto& l : k.lines) {
        std::string t = Trim(l);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[') { inEnv = _strnicmp(t.c_str(), "[env]", 5) == 0; continue; }
        size_t eq = t.find('=');
        if (!inEnv || eq == std::string::npos) continue;
        if (anyEnv) *anyEnv = true;
        if (_stricmp(Trim(t.substr(0, eq)).c_str(), key) == 0) val = Trim(t.substr(eq + 1));
    }
    return val;
}
// Where the game keeps its saves (ps3recomp/libs/system/cellSaveData.c): PS3_SAVEDATA_ROOT if set (environment, or
// gow2_port.ini [env], relative to the exe folder), else %APPDATA%\ps3recomp\dev_hdd0\home\00000001\savedata.
// All candidates are protected; the default one is listed even when it does not exist yet.
static std::vector<wstr> SaveRoots(const wstr& exeDir) {
    std::vector<wstr> r;
    auto add = [&](wstr p) {
        if (p.empty()) return;
        if (!(p.size() > 1 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\')))) p = JoinPath(exeDir, p);
        p = FullPath(p);
        for (auto& x : r) if (IEquals(x, p)) return;
        r.push_back(p);
    };
    add(GetEnv(L"PS3_SAVEDATA_ROOT"));
    add(Widen(PortIniEnv(JoinPath(exeDir, L"gow2_port.ini"), "PS3_SAVEDATA_ROOT", nullptr)));
    add(JoinPath(KnownFolder(FOLDERID_RoamingAppData), L"ps3recomp"));
    return r;
}

RemovePlan PlanRemoveBuild(const InstallPaths& ip) {
    RemovePlan p;
    KvFile st; st.Load(ip.state);
    wstr exe = FindBuiltExe(ip, st);
    wstr exeDir = exe.empty() ? ip.root : ParentDir(exe);
    p.saveRoots = SaveRoots(exeDir);
    // the game's checkpoints (F6 / Port Settings "Save Checkpoint": checkpoints\ next to the game exe) are the player's,
    // like the saves: never deleted
    const wstr ckpt = JoinPath(exeDir, L"checkpoints");
    auto safe = [&](const wstr& path) {      // never anything that holds saves or lies inside a save folder
        for (auto& s : p.saveRoots) if (Within(s, path) || Within(path, s)) return false;
        if (Within(ckpt, path) || Within(path, ckpt)) return false;   // nor the checkpoints
        return !Within(ExePath(), path);     // nor the running launcher (or a folder holding it)
    };
    // folders: the launcher's own (tools, scripts, downloads) and build.py's (.build, movie_cache, licenses)
    for (auto base : { ip.root, exeDir }) {
        for (auto sub : { L"tools", L"scripts", L"downloads", L".build", L"movie_cache", L"licenses", L"shader_cache" }) {
            wstr d = JoinPath(base, sub);
            if (!DirExists(d) || (GetFileAttributesW(d.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            bool dup = false; for (auto& x : p.dirs) dup |= IEquals(x, d);
            if (dup) continue;
            if (!safe(d)) { p.kept.push_back(d + L"  (holds or is inside a save folder)"); continue; }
            p.dirs.push_back(d);
            p.bytes += TreeBytes(d);
        }
        if (IEquals(base, exeDir)) break;
    }
    // files: build.py's install list (build_info.json "installed"), else the names it is known to install
    std::vector<wstr> names;
    std::string js;
    JVal j;
    if (ReadFileBytes(JoinPath(exeDir, L"build_info.json"), js) && JParse(js, j)) {
        if (const JVal* a = j.get("installed")) for (auto& v : a->a) if (v.t == JVal::Str) names.push_back(Widen(v.s));
        // the game files build.py extracted from a PSN package (listed only when it made them; the .pkg itself stays)
        if (const JVal* a = j.get("installed_dirs")) for (auto& v : a->a) {
            if (v.t != JVal::Str || v.s != "game") continue;
            wstr d = JoinPath(exeDir, L"game");
            if (!DirExists(d) || (GetFileAttributesW(d.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            if (!safe(d)) { p.kept.push_back(d + L"  (holds or is inside a save folder)"); continue; }
            p.dirs.push_back(d);
            p.bytes += TreeBytes(d);
        }
    }
    if (names.empty()) {
        names = { L"gow2hd.exe", L"EBOOT.ELF", L"ffmpeg.exe", L"ffprobe.exe" };
        for (auto pat : { L"avcodec-*.dll", L"avdevice-*.dll", L"avfilter-*.dll", L"avformat-*.dll", L"avutil-*.dll",
                          L"postproc-*.dll", L"swresample-*.dll", L"swscale-*.dll" })
            for (auto& f : ListDir(exeDir, false, pat)) names.push_back(f);
    }
    if (!exe.empty()) names.push_back(FileNameOf(exe));
    for (auto n : { L"build_info.json", L"gow2.log", L"gow2.prev.log" }) names.push_back(n);
    bool iniEnv = false;
    PortIniEnv(JoinPath(exeDir, L"gow2_port.ini"), "", &iniEnv);
    if (iniEnv) p.kept.push_back(JoinPath(exeDir, L"gow2_port.ini") + L"  (has your [env] settings)");
    else names.push_back(L"gow2_port.ini");
    for (auto& n : names) {
        if (n.empty() || n.find_first_of(L"\\/:") != wstr::npos || n.find(L"..") != wstr::npos) continue;
        if (IEquals(n, L"port_settings.ini") || IEquals(n, L"launcher_state.ini") || IEquals(n, LAUNCHER_EXE_NAME)) continue;
        if (iniEnv && IEquals(n, L"gow2_port.ini")) continue;
        wstr f = JoinPath(exeDir, n);
        if (!FileExists(f) || !safe(f)) continue;
        bool dup = false; for (auto& x : p.files) dup |= IEquals(x, f);
        if (dup) continue;
        p.files.push_back(f);
        p.bytes += FileSize(f);
    }
    for (auto n : { L"build.log", L"build.prev.log" }) {     // the launcher's logs of the removed build
        wstr f = JoinPath(ip.root, n);
        if (FileExists(f) && safe(f)) { p.files.push_back(f); p.bytes += FileSize(f); }
    }
    wstr ps = JoinPath(exeDir, L"port_settings.ini");
    if (FileExists(ps)) p.kept.insert(p.kept.begin(), ps);
    if (DirExists(ckpt)) p.kept.push_back(ckpt + L"  (your checkpoints)");
    p.kept.push_back(ip.state + L"  (your game and licence choices)");
    return p;
}

int RemoveBuild(const InstallPaths& ip, const RemovePlan& plan, wstr& err) {
    int bad = 0;
    for (auto& f : plan.files) {
        SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileW(f.c_str()) && FileExists(f)) { ++bad; err += f + L"\n"; }
    }
    for (auto& d : plan.dirs)
        if (!DeleteTree(d) && DirExists(d)) { ++bad; err += d + L"\n"; }
    KvFile st; st.Load(ip.state);
    st.Set("built", "0"); st.Set("exe", ""); st.Set("built_at", "");
    st.Save(ip.state);
    return bad;
}
