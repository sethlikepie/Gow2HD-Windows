// Gow2HD-Windows launcher - shared declarations.
// Everything here is our own code; the exe contains no game data.
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include <atomic>

#include "config.h"

typedef std::wstring wstr;

// ---------------------------------------------------------------- strings
wstr Widen(const std::string& s);
std::string Narrow(const wstr& s);
wstr Fmt(const wchar_t* fmt, ...);
std::string ToLowerA(std::string s);
wstr ToLowerW(wstr s);
std::string Trim(const std::string& s);
wstr TrimW(const wstr& s);
bool IEquals(const wstr& a, const wstr& b);
bool EndsWithI(const wstr& s, const wstr& suffix);
wstr FormatBytes(uint64_t n);
wstr ArgQuote(const wstr& arg);

// ---------------------------------------------------------------- files / paths
wstr JoinPath(const wstr& a, const wstr& b);
wstr ParentDir(const wstr& p);
wstr FileNameOf(const wstr& p);
wstr FullPath(const wstr& p);
wstr ExePath();
wstr ExeDir();
wstr KnownFolder(REFKNOWNFOLDERID id);
wstr GetEnv(const wchar_t* name);
bool FileExists(const wstr& p);
bool DirExists(const wstr& p);
bool MakeDirs(const wstr& p);
uint64_t FileSize(const wstr& p);
uint64_t FreeSpace(const wstr& p);   // free bytes on the volume of p (or its nearest existing parent)
std::vector<wstr> ListDir(const wstr& dir, bool wantDirs, const wchar_t* pattern = L"*");
bool ReadFileBytes(const wstr& p, std::string& out);
bool WriteFileBytes(const wstr& p, const std::string& data);
bool AppendFileBytes(const wstr& p, const std::string& data);
bool DeleteTree(const wstr& dir);    // only used on folders the launcher itself created (tools, scripts)
bool DirWritable(const wstr& dir);

// key=value text file without sections (port_settings.ini, launcher_state.ini). Keeps comments and unknown keys.
struct KvFile {
    std::vector<std::string> lines;
    bool Load(const wstr& path);
    bool Save(const wstr& path) const;
    std::string Get(const std::string& key, const std::string& def = "") const;
    int GetInt(const std::string& key, int def) const;
    void Set(const std::string& key, const std::string& value);
    wstr GetW(const std::string& key) const { return Widen(Get(key)); }
    void SetW(const std::string& key, const wstr& v) { Set(key, Narrow(v)); }
};

// ---------------------------------------------------------------- game / license detection
struct SfoInfo { bool ok = false; std::string titleId, appVer, version, title, category; };
SfoInfo ReadSfo(const wstr& path);
SfoInfo ReadSfoBytes(const std::string& d);

enum GameStatus { GAME_OK = 0, GAME_MISSING, GAME_NOT_PS3, GAME_WRONG_TITLE, GAME_WRONG_VERSION, GAME_NO_EBOOT,
                  GAME_PKG_INCOMPLETE, GAME_PKG_PATCH };
// dir: the resolved game folder, or (isPkg) the full path of the PSN package
struct GameCheck { GameStatus status = GAME_MISSING; wstr dir; wstr message; bool isPkg = false; };
GameCheck CheckGameDir(const wstr& picked);   // accepts NPUA80491, PS3_GAME, USRDIR or a parent folder
GameCheck CheckGamePkg(const wstr& path);     // a PSN package: header, content id, size, PARAM.SFO inside, files
GameCheck CheckGameInput(const wstr& picked); // a .pkg file -> CheckGamePkg, anything else -> CheckGameDir
bool IsPkgPath(const wstr& p);
wstr PkgGameDir(const wstr& installRoot);     // where build.py extracts a package: <install>\game\NPUA80491
bool CheckRapFile(const wstr& path, wstr& msg);

struct Detected {
    std::vector<wstr> rpcs3Dirs;   // RPCS3 folders found (each has dev_hdd0 or rpcs3.exe)
    wstr game, gameFrom;           // folder (or a PSN .pkg when no installed game was found) + where it was found
    wstr rap, rapFrom;
    wstr elf;
    std::vector<wstr> log;         // what was searched (for --detect and the log)
};
Detected DetectAll(const wstr& gameHint = L"");
wstr FindRap(const wstr& gameDir, const std::vector<wstr>& rpcs3Dirs, wstr* from);
wstr FindElf(const wstr& gameDir);

// ---------------------------------------------------------------- hashing / json
bool Sha256File(const wstr& path, std::string& hexOut, std::atomic<bool>* cancel = nullptr);

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } t = Null;
    bool b = false; double n = 0; std::string s;
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;
    const JVal* get(const char* key) const;
    std::string str(const char* key, const std::string& def = "") const;
    double num(const char* key, double def = 0) const;
};
bool JParse(const std::string& text, JVal& out);

// ---------------------------------------------------------------- network
extern std::function<void(const wstr&)> g_netLog;   // optional: notes like "resuming at N bytes"
typedef std::function<void(uint64_t done, uint64_t total)> ProgressFn;
bool HttpGet(const wstr& url, std::string& body, DWORD& status, wstr& err, const wchar_t* extraHeaders = nullptr);
// Downloads url to dest (via dest+".part", resumable with HTTP Range), then checks size and sha256 (if given).
// Returns true when dest exists and matches. Existing dest with the right hash is reused without a download.
bool DownloadVerified(const wstr& url, const wstr& dest, const std::string& sha256, uint64_t size,
                      const ProgressFn& progress, std::atomic<bool>* cancel, wstr& err);

// ---------------------------------------------------------------- processes
typedef std::function<void(const std::string& line)> LineFn;
// Runs a hidden child in a kill-on-close job. Lines of stdout+stderr go to onLine. Returns false if it could not start.
bool RunProcess(const wstr& cmdline, const wstr& cwd, const LineFn& onLine, DWORD& exitCode, std::atomic<bool>* cancel);
void CancelRunningProcess();          // kills the current job's whole process tree
bool ExtractArchive(const wstr& archive, const wstr& destDir, std::atomic<bool>* cancel, wstr& err);
bool IsArchiveName(const wstr& name);

// ---------------------------------------------------------------- paths of an install
struct InstallPaths {
    wstr root, tools, python, pythonExe, scripts, downloads, log, state;
    void Init(const wstr& installDir);
};
wstr DefaultInstallDir();
wstr LoadInstallDir();               // HKCU, else default
void SaveInstallDir(const wstr& dir);

// ---------------------------------------------------------------- build pipeline (worker thread)
struct BuildRequest {
    wstr game, rap, elf, install;
    int jobs = 0;
};
struct BuildEvents {
    std::function<void(int pct, const wstr& step)> progress;   // overall 0..100
    std::function<void(const wstr& line)> logLine;               // also written to build.log
};
// Runs the whole pipeline. Returns 0 on success, else a non-zero code; err gets a player-facing message.
int RunBuildPipeline(const BuildRequest& req, const BuildEvents& ev, std::atomic<bool>* cancel, wstr& err);
wstr FindBuiltExe(const InstallPaths& ip, const KvFile& state);
bool LaunchGame(const InstallPaths& ip, wstr& err);
bool IsBuilt(const InstallPaths& ip);

// "Remove build": deletes the build output, tools, scripts and downloads of an install; keeps port_settings.ini,
// launcher_state.ini (game/licence choices) and never touches the save folder (see SaveRoots).
struct RemovePlan {
    std::vector<wstr> dirs, files;   // what will be deleted (absolute)
    std::vector<wstr> kept;          // notable things that stay (absolute), for the confirmation text
    std::vector<wstr> saveRoots;     // where the game keeps saves (protected)
    uint64_t bytes = 0;
};
RemovePlan PlanRemoveBuild(const InstallPaths& ip);
int RemoveBuild(const InstallPaths& ip, const RemovePlan& plan, wstr& err);   // returns the number of failures

// ---------------------------------------------------------------- updates
struct UpdateInfo { bool ok = false, newer = false, noRelease = false; std::string tag, assetUrl, sha256, pageUrl; wstr err; };
UpdateInfo CheckForUpdate();
int CompareVersions(const std::string& a, const std::string& b);
bool ManagedByQuiver();
