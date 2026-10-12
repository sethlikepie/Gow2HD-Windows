# Quiver Launcher: how a build-on-first-run port fits

Researched 2026-10-09 from the sources at the end of this file (launcher commit `8db80d7`, catalog commit `8444ac6`).
Quiver (formerly GithubLauncher / N64RecompLauncher) is an open-source (MIT, .NET/Avalonia) launcher. It installs apps
from GitHub/GitLab releases, keeps them updated and starts them.

## How a catalog entry is defined

- **Where it lives:** the community catalog repo `tgeorgiadis/quiver-community-app-catalog`. There is one JSON list per
  brand: `community-app-catalog/PlayStation.json`, `Nintendo.json`, `Xbox.json` and `OtherPlatforms.json`. Each list holds
  `name`, `description`, `version` and an `apps` array. The launcher reads the catalog through the quiverlauncher.com API
  (`https://api.quiverlauncher.com/api/v1`). That service indexes these lists and records a SHA-256 for each release
  file it has verified.
- **How to submit:** open a pull request that adds the entry to the right list. CI rejects invalid JSON, tabs, odd
  indentation and trailing whitespace, and runs Prettier (`npx prettier@3.5.3`). App PRs leave the list `version` alone,
  because a bot batches the version bumps.
- **Fields:**

| Field | Required | Notes |
|---|---|---|
| `name` | yes | Game title only. No "(Recomp)" or "(Port)". |
| `project` | yes | Port or product name. Together with `name` it forms `folderName`. |
| `repository` | yes, unless the app is manual | `owner/repo` |
| `repositorySource` | no | `github` (default) or `gitlab` |
| `folderName` | yes | `PascalCase(name)-PascalCase(project)`, unique, letters and digits only |
| `releaseAssetFilter` | no | Case-insensitive substring. Only release files that match it are offered. |
| `appIconUrl` | recommended | URL of the icon, or `null` |
| `tags` | recommended | In this order: type, console, series, brand, full console name, long type |
| `mods.*` | no | Thunderstore/GameBanana mod browser (not relevant for this port) |

## How Quiver picks the download and the exe

- **Asset choice** (`DownloadAssetSelection`, `PlatformAssetMatcher`):
  - On Windows, an asset counts as a Windows build when its name contains `.exe`, `windows`, `win64`, `-win.` and
    similar, or when it is an unlabeled `.zip`, `.7z` or `.rar`.
  - Files whose names end in `-src.zip`, `-source.zip`, `.sha256` or `checksums` are auxiliary and are never offered.
  - If more than one asset qualifies, the player has to choose. `releaseAssetFilter` narrows the list first.
  - A catalog app installs only the release that quiverlauncher.com verified, and the download is refused if its SHA-256
    changed.
- **Install:** a single `.exe` asset is moved into `Apps/<folderName>/` as-is, and archives are extracted there. Quiver
  then writes `version.txt` in that folder. An update overwrites the files; other files in the folder are not wiped.
- **Which exe runs** (`GameInstallationService.FindExecutableCandidates`): Quiver searches the app folder
  **recursively** for `*.exe`, `*.bat` and `*.cmd`. With exactly one candidate, it runs that one. With more, the player
  picks one and the choice is saved in `selected_executable.txt`.
- **Launch:** `ProcessStartInfo { FileName = exe, WorkingDirectory = exe folder, UseShellExecute = true }` with **no
  arguments**. Quiver has no per-app launch arguments; its only documented CLI flag is `--kiosk`, which is for Quiver
  itself.
- **First-time setup:** Quiver has no setup hooks, install scripts or prompts of its own. Anything interactive has to
  happen inside the app.

## How well Gow2HD-Windows fits

It fits well, without changes on Quiver's side:

1. The release ships one asset, `Gow2HD-Windows.exe`. That name carries the `.exe` Windows marker, so Quiver installs it
   into `Apps/GodOfWarIIHD-GoW2Windows/` and runs it with no arguments.
2. With no arguments the launcher decides what to show. The first time it shows the setup wizard: it finds the game and
   the `.rap`, then downloads and builds. After that it shows the launcher window. Quiver does not need to know about the
   build.
3. The build goes to `%LOCALAPPDATA%\GoW2-Windows` by default, **outside** Quiver's app folder. This matters because
   Quiver searches its folder recursively for executables. If the port's exe, `python.exe` or the toolchain sat inside
   it, the player would get an "pick the executable" prompt. The launcher warns when someone picks an install folder
   inside the Quiver folder.
4. Since Quiver cannot pass `--play`, the launcher has the setting **"Start the game directly"**. With it on, starting
   the app from Quiver goes straight into the game, and holding Shift shows the launcher again. `--play` stays for
   launchers that can pass arguments (Steam shortcuts, Playnite, scripts).
5. Updates: Quiver updates the exe itself. The launcher detects a Quiver install (`version.txt` next to the exe) and then
   does not self-update. "Check for updates" points the player to Quiver instead.
6. Checksums: Quiver checks the exe's SHA-256. The launcher then checks every file it downloads (Python, the tools, and
   the scripts zip in download mode) against SHA-256 values pinned in the exe or in `build.py`. The chain is verified
   end to end.

Limits and suggestions:

- **Kiosk/gamepad:** the first-time setup needs a mouse and keyboard (file pickers). In Quiver's kiosk mode on a
  cabinet, the player has to finish setup once on the desktop. Adding gamepad navigation for the home menu is easy; the
  wizard would stay mouse-driven.
- **Uninstall:** removing the app in Quiver deletes only `Apps/GodOfWarIIHD-GoW2Windows/`. The ~6 GB build in
  `%LOCALAPPDATA%\GoW2-Windows` stays. A "Remove build" button in the launcher would solve this.
- **Asset naming convention for our releases:**
  - Exactly one runnable asset: `Gow2HD-Windows.exe`.
  - Everything else must be ignorable by Quiver: the scripts zip (download mode) as `Gow2HD-Windows-scripts-src.zip` and
    checksums as `Gow2HD-Windows.exe.sha256`. Both are auxiliary by Quiver's rules.
  - Still set `releaseAssetFilter` so a future extra asset cannot cause a choice prompt.
- **Console tag:** the catalog's tag table has no PS3 yet. The list already uses `psp` / `playstation portable` outside
  that table, so `ps3` / `playstation 3` follows the same pattern. Say so in the PR.

## Proposed entry (for `community-app-catalog/PlayStation.json`)

```json
{
  "name": "God of War II HD",
  "project": "Gow2HD-Windows",
  "repository": "sethlikepie/Gow2HD-Windows",
  "folderName": "GodOfWarIIHD-GoW2Windows",
  "releaseAssetFilter": "Gow2HD-Windows.exe",
  "appIconUrl": "https://raw.githubusercontent.com/sethlikepie/Gow2HD-Windows/main/launcher/res/icon-256.png",
  "tags": [
    "recomp",
    "ps3",
    "god of war",
    "playstation",
    "playstation 3",
    "recompilation"
  ]
}
```

The repository name (`gow2-windows`) and the icon path are placeholders until the main repo exists. They must match
`GITHUB_REPO` in `src/config.h`. The icon must be our own (`res/app.ico`, drawn by `tools/make_icon.py`), never game
or box art. The catalog often uses SteamGridDB box art; we should not.

## Sources

- Launcher repo: https://github.com/tgeorgiadis/quiver (README; `lib/QuiverLauncher.Core/Services/GameInstallationService.cs`,
  `PlatformAssetMatcher.cs`, `DownloadAssetSelection.cs`; `Services/GameLaunchService.cs`, `Services/QuiverCatalogClient.cs`;
  `Models/GameInfo.cs`; `apps.json.example`), commit 8db80d79fa1fe2241b9269d0ec10e47063fb38ae
- Catalog repo: https://github.com/tgeorgiadis/quiver-community-app-catalog (README: fields, naming, tag order,
  release batches; `community-app-catalog/PlayStation.json` for existing recomp entries), commit
  8444ac61c4ef2b0f337fcdf9772120d69ef05b06
- Catalog website: https://quiverlauncher.com
- Release history (catalog moved to a separate repo, external lists): https://newreleases.io/project/github/tgeorgiadis/quiver-launcher/release/v2.0.0
