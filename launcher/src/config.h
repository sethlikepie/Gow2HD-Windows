// Gow2HD-Windows launcher - constants. Change these in one place.
#pragma once

#define LAUNCHER_VERSION      "0.1.0"
#define LAUNCHER_VERSION_W    L"0.1.0"
#define LAUNCHER_EXE_NAME     L"Gow2HD-Windows.exe"

// GitHub repository (owner/name) that publishes the launcher releases: the update check, the scripts download
// (download mode) and the project link all derive from this one constant.
#define GITHUB_REPO           "sethlikepie/Gow2HD-Windows"
#define WIDEN_(s)             L##s
#define WIDEN(s)              WIDEN_(s)
#define GITHUB_REPO_W         WIDEN(GITHUB_REPO)

// The one game version the port supports.
#define GAME_TITLE_ID         "NPUA80491"
#define GAME_APP_VER          "01.00"
#define RAP_FILE_NAME         L"UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap"
#define GAME_CONTENT_ID       "UP9000-NPUA80491_00-GODOFWARIIHDUS00"   // also the PSN package's content id
#define GAME_PSARC_SIZE       6987947360ull                              // USRDIR/gow2.psarc of v01.00

// Disk needed for a first build (measured): build output 5.8 GB + tools 0.94 GB + downloaded zips 0.27 GB.
#define REQUIRED_BYTES        (15ull * 512 * 1024 * 1024)   // 7.5 GB
// From a PSN package the game files are extracted into the install folder (gow2.psarc 6.99 GB + EBOOT.BIN +
// PARAM.SFO + ICON0.PNG) and stay there: the game reads gow2.psarc while it runs.
#define PKG_EXTRA_BYTES       (6680ull * 1024 * 1024)       // 7.0 GB (6.5 GiB)

// Embeddable CPython that runs build.py (official python.org file; MD5 cross-checked with the python.org API).
#define PYTHON_URL            L"https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip"
#define PYTHON_SHA256         "4acbed6dd1c744b0376e3b1cf57ce906f9dc9e95e68824584c8099a63025a3c3"
#define PYTHON_SIZE           11133606ull
#define PYTHON_PTH            L"python312._pth"

// Where the build scripts come from. 1 = compressed resource inside the exe (default: offline-capable, version-locked).
// 0 = download SCRIPTS_URL (the matching release asset) and check SCRIPTS_SHA256. Set by build.cmd.
#ifndef SCRIPTS_BUNDLED
#define SCRIPTS_BUNDLED       1
#endif
#ifndef SCRIPTS_URL
#define SCRIPTS_URL           L"https://github.com/" GITHUB_REPO_W L"/releases/download/v" LAUNCHER_VERSION_W L"/Gow2HD-Windows-scripts-src.zip"
#endif
#ifndef SCRIPTS_SHA256
#define SCRIPTS_SHA256        ""
#endif

#define REG_KEY               L"Software\\GoW2-Windows"
