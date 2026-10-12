import Foundation
import AppKit
import CryptoKit

/// What the Play screen shows. Computed natively (no Python): same rules as
/// the legacy gow2_launcher.py, and the same user_config.json on disk, so both
/// launchers stay interchangeable.
struct LauncherStatus {
    struct Mod: Identifiable, Hashable {
        let name: String
        var enabled: Bool
        var id: String { name }
    }
    var setup_ok: Bool
    var message: String
    var elf: String
    var elf_ok: Bool
    var vfs_root: String
    var vfs_ok: Bool
    var movie_cache: String
    var movie_cache_ok: Bool
    var binary: String
    var autosave: Bool
    var savedata_root: String
    var mods_dir: String
    var mods: [Mod]
}

/// user_config.json -- written by this app and by gow2_launcher.py.
struct LauncherConfig: Codable {
    var elf: String = ""
    var vfs_root: String = ""
    var movie_cache: String = ""
    var mods_dir: String = ""
    var mods_enabled: [String] = []
    var savedata_root: String? = nil
    var overlay_settings: String? = nil
}

enum LauncherCore {
    static let fm = FileManager.default

    static func isFile(_ p: String) -> Bool {
        var dir: ObjCBool = false
        return !p.isEmpty && fm.fileExists(atPath: p, isDirectory: &dir) && !dir.boolValue
    }
    static func isDir(_ p: String) -> Bool {
        var dir: ObjCBool = false
        return !p.isEmpty && fm.fileExists(atPath: p, isDirectory: &dir) && dir.boolValue
    }

    /// ELF magic: the decrypted EBOOT, not the encrypted EBOOT.BIN (SCE\0).
    static func isELF(_ p: String) -> Bool {
        guard isFile(p), let h = FileHandle(forReadingAtPath: p) else { return false }
        defer { try? h.close() }
        return h.readData(ofLength: 4) == Data([0x7F, 0x45, 0x4C, 0x46])
    }

    static func isUSRDIR(_ p: String) -> Bool {
        guard isDir(p), let names = try? fm.contentsOfDirectory(atPath: p) else { return false }
        let lower = Set(names.map { $0.lowercased() })
        return lower.contains("gow2.psarc") || lower.contains("usrdir") || !names.isEmpty
    }

    static func defaultConfig(repo: URL) -> LauncherConfig {
        var c = LauncherConfig()
        let elf = repo.appendingPathComponent("EBOOT.ELF").path
        let usr = repo.appendingPathComponent("extracted/USRDIR").path
        c.elf = isFile(elf) ? elf : ""
        c.vfs_root = isDir(usr) ? usr : ""
        c.movie_cache = repo.appendingPathComponent("movie_cache").path
        c.mods_dir = repo.appendingPathComponent("mods").path
        return c
    }

    static func load(repo: URL) -> LauncherConfig {
        var c = defaultConfig(repo: repo)
        let url = repo.appendingPathComponent("user_config.json")
        if let data = try? Data(contentsOf: url),
           let obj = try? JSONSerialization.jsonObject(with: data) as? [String: Any] {
            if let v = obj["elf"] as? String { c.elf = v }
            if let v = obj["vfs_root"] as? String { c.vfs_root = v }
            if let v = obj["movie_cache"] as? String { c.movie_cache = v }
            if let v = obj["mods_dir"] as? String, !v.isEmpty { c.mods_dir = v }
            if let v = obj["mods_enabled"] as? [String] { c.mods_enabled = v }
            if let v = obj["savedata_root"] as? String, !v.isEmpty { c.savedata_root = v }
            if let v = obj["overlay_settings"] as? String, !v.isEmpty { c.overlay_settings = v }
        }
        return c
    }

    static func save(_ c: LauncherConfig, repo: URL) throws {
        let enc = JSONEncoder()
        enc.outputFormatting = [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
        try enc.encode(c).write(to: repo.appendingPathComponent("user_config.json"), options: .atomic)
    }

    static func findBinary(repo: URL) -> String {
        for name in ["boot_gow2_mods", "g2play", "boot_gow2"] {
            let p = repo.appendingPathComponent(name).path
            if fm.isExecutableFile(atPath: p) { return p }
        }
        return ""
    }

    static func savedataRoot(_ c: LauncherConfig) -> URL {
        if let r = c.savedata_root { return URL(fileURLWithPath: r) }
        if let r = ProcessInfo.processInfo.environment["PS3_SAVEDATA_ROOT"], !r.isEmpty {
            return URL(fileURLWithPath: r)
        }
        return fm.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/ps3recomp/dev_hdd0/home/00000001/savedata")
    }

    static let safeName = try! NSRegularExpression(pattern: "^[A-Za-z0-9._-]{1,64}$")
    static func isSafeName(_ s: String) -> Bool {
        safeName.firstMatch(in: s, range: NSRange(s.startIndex..., in: s)) != nil
    }

    static func modNames(_ dir: String) -> [String] {
        guard let names = try? fm.contentsOfDirectory(atPath: dir) else { return [] }
        return names.filter { isSafeName($0) && !$0.hasPrefix(".") && isDir(dir + "/" + $0) }.sorted()
    }

    /// A slot is valid when its manifest lists relative files whose size and
    /// SHA-256 match (the autosave store never restores a partial copy).
    static func autosaveAvailable(_ c: LauncherConfig) -> Bool {
        let base = savedataRoot(c).deletingLastPathComponent().appendingPathComponent("ps3recomp-autosave")
        guard let titles = try? fm.contentsOfDirectory(atPath: base.path) else { return false }
        for t in titles where !t.hasPrefix(".") {
            let slot = base.appendingPathComponent(t).appendingPathComponent("current")
            if slotValid(slot) { return true }
        }
        return false
    }

    static func slotValid(_ slot: URL) -> Bool {
        guard let data = try? Data(contentsOf: slot.appendingPathComponent("manifest.json")),
              let m = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              (m["version"] as? Int) == 1,
              let files = m["files"] as? [[String: Any]], !files.isEmpty else { return false }
        for f in files {
            guard let rel = f["path"] as? String, !rel.hasPrefix("/"), !rel.contains("\\"),
                  !rel.split(separator: "/").contains(where: { $0 == "." || $0 == ".." }),
                  let size = f["size"] as? Int, let sha = f["sha256"] as? String,
                  let bytes = try? Data(contentsOf: slot.appendingPathComponent("data").appendingPathComponent(rel)),
                  bytes.count == size else { return false }
            let digest = SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined()
            if digest != sha { return false }
        }
        return true
    }

    static func status(repo: URL) -> LauncherStatus {
        let c = load(repo: repo)
        let elfOK = isELF(c.elf), vfsOK = isUSRDIR(c.vfs_root)
        let msg = !elfOK ? "Falta o EBOOT.ELF (dump seu do GoW2 HD)."
                : !vfsOK ? "Falta a pasta USRDIR." : "Pronto para jogar."
        let mods = modNames(c.mods_dir).map { LauncherStatus.Mod(name: $0, enabled: c.mods_enabled.contains($0)) }
        return LauncherStatus(setup_ok: elfOK && vfsOK, message: msg,
                              elf: c.elf, elf_ok: elfOK, vfs_root: c.vfs_root, vfs_ok: vfsOK,
                              movie_cache: c.movie_cache, movie_cache_ok: isDir(c.movie_cache),
                              binary: findBinary(repo: repo), autosave: autosaveAvailable(c),
                              savedata_root: savedataRoot(c).path, mods_dir: c.mods_dir, mods: mods)
    }

    /// Imports a mod .zip with the system tools (no zip-slip: every entry is
    /// checked before extraction), flattening a lone wrapper folder.
    static func importZip(_ zip: URL, modsDir: String) throws -> String {
        let list = try run("/usr/bin/zipinfo", ["-1", zip.path])
        for entry in list.split(separator: "\n") {
            if entry.hasPrefix("/") || entry.split(separator: "/").contains("..") {
                throw NSError(domain: "mods", code: 1, userInfo: [NSLocalizedDescriptionKey: "zip com caminho inseguro: \(entry)"])
            }
        }
        var base = zip.deletingPathExtension().lastPathComponent
            .replacingOccurrences(of: "[^A-Za-z0-9._-]", with: "_", options: .regularExpression)
        if base.isEmpty { base = "mod" }
        base = String(base.prefix(64))
        try fm.createDirectory(atPath: modsDir, withIntermediateDirectories: true)
        var name = base, n = 2
        while fm.fileExists(atPath: modsDir + "/" + name) { name = "\(base)_\(n)"; n += 1 }
        let tmp = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        defer { try? fm.removeItem(at: tmp) }
        _ = try run("/usr/bin/ditto", ["-x", "-k", zip.path, tmp.path])
        var root = tmp
        if let kids = try? fm.contentsOfDirectory(atPath: tmp.path).filter({ !$0.hasPrefix(".") && $0 != "__MACOSX" }),
           kids.count == 1, isDir(tmp.appendingPathComponent(kids[0]).path),
           kids[0].uppercased() != "USRDIR" {
            root = tmp.appendingPathComponent(kids[0])
        }
        try fm.moveItem(at: root, to: URL(fileURLWithPath: modsDir + "/" + name))
        return name
    }

    @discardableResult
    static func run(_ tool: String, _ args: [String]) throws -> String {
        let p = Process()
        p.executableURL = URL(fileURLWithPath: tool)
        p.arguments = args
        let out = Pipe()
        p.standardOutput = out
        p.standardError = out
        try p.run()
        let data = out.fileHandleForReading.readDataToEndOfFile()
        p.waitUntilExit()
        let text = String(decoding: data, as: UTF8.self)
        if p.terminationStatus != 0 {
            throw NSError(domain: tool, code: Int(p.terminationStatus), userInfo: [NSLocalizedDescriptionKey: text])
        }
        return text
    }

    static func shq(_ s: String) -> String { "'" + s.replacingOccurrences(of: "'", with: "'\\''") + "'" }

    /// The checkout the launcher drives: GOW2_REPO, else the path stamped by build_app.sh.
    static func repoURL() -> URL {
        let env = ProcessInfo.processInfo.environment["GOW2_REPO"]
        let stamped = Bundle.main.object(forInfoDictionaryKey: "GoW2RepoPath") as? String
        return URL(fileURLWithPath: env ?? stamped ?? FileManager.default.currentDirectoryPath)
    }

    /// Same order as gow2_launcher.py: caller env, user_config.json, per-user default.
    static func overlaySettingsURL(repo: URL) -> URL {
        if let e = ProcessInfo.processInfo.environment["PS3_OVERLAY_SETTINGS"], !e.isEmpty {
            return URL(fileURLWithPath: e)
        }
        if let c = load(repo: repo).overlay_settings {
            return URL(fileURLWithPath: (c as NSString).expandingTildeInPath)
        }
        return OverlaySettingsFile.defaultURL()
    }

    /// Same launch contract as gow2_launcher.py build_launch_script: the player's
    /// settings are in the environment, env_gow2.sh fills the rest, then paths/mods.
    static func launchScript(repo: URL, c: LauncherConfig, binary: String, resume: Bool) -> String {
        let enabled = c.mods_enabled.filter { modNames(c.mods_dir).contains($0) }
        let resumeEnv = resume
            ? "export PS3_AUTOSAVE_RESUME=1\nexport PS3_AUTOSAVE_MENU=1\nexport PS3_PAD_AUTOSTART=0\n"
            : "unset PS3_AUTOSAVE_RESUME\n"
        return """
        set -euo pipefail
        cd \(shq(repo.path))
        G2_FULLSCREEN="${PS3_FULLSCREEN-}"
        G2_VSYNC="${PS3_METAL_VSYNC-}"
        set -a
        . ./env_gow2.sh   # ${VAR:=default}: the player's settings, exported before, win
        set +a
        export PS3_VFS_ROOT=\(shq(c.vfs_root))
        export PS3_MOVIE_CACHE=\(shq(c.movie_cache))
        export PS3_MODS_DIR=\(shq(c.mods_dir))
        export PS3_MODS_ENABLED=\(shq(enabled.joined(separator: ",")))
        export PS3_MOVIE_DONE_MS="${GOW2_MOVIE_DONE_MS:-auto}"
        # Fullscreen and VSync come from the overlay settings file; only an
        # explicit caller value overrides it (env_gow2.sh's default must not).
        if [ -n "$G2_FULLSCREEN" ]; then export PS3_FULLSCREEN="$G2_FULLSCREEN"; else unset PS3_FULLSCREEN; fi
        if [ -n "$G2_VSYNC" ]; then export PS3_METAL_VSYNC="$G2_VSYNC"; else unset PS3_METAL_VSYNC; fi
        \(resumeEnv)unset PS3_NO_RSX
        exec \(shq(binary)) \(shq(c.elf))
        """
    }
}

/// Hides the launcher while the game runs and brings it back when the game
/// exits. A protocol so the handoff is testable without AppKit windows.
@MainActor
protocol LauncherPresenter: AnyObject {
    func hideForGame()
    func showAfterGame()
}

@MainActor
final class AppKitLauncherPresenter: LauncherPresenter {
    func hideForGame() { NSApp.hide(nil) }

    func showAfterGame() {
        NSApp.unhide(nil)
        NSApp.activate()
        let main = NSApp.windows.first { $0.canBecomeMain && !($0 is NSPanel) }
        main?.makeKeyAndOrderFront(nil)
    }
}

/// Starts the game process and ties the launcher's visibility to it: hidden
/// only after a successful run(), shown again when the process ends (exit,
/// window closed or crash), then `onExit` runs. A failed start never hides.
@MainActor
enum GameSession {
    static func start(_ p: Process, presenter: LauncherPresenter,
                      onExit: @escaping @MainActor () -> Void) throws {
        p.terminationHandler = { _ in
            Task { @MainActor in
                presenter.showAfterGame()
                onExit()
            }
        }
        try p.run()
        presenter.hideForGame()
    }
}

@MainActor
final class Backend: ObservableObject {
    @Published var status: LauncherStatus?
    @Published var error: String?
    @Published var running = false
    @Published var logTail: [String] = []
    /// Set when the status query is slow -- in practice macOS waiting for the
    /// user to allow access to the folder that holds the game.
    @Published var waitingHint: String?

    let repo: URL
    let logURL: URL
    private var game: Process?
    private var logTimer: Timer?
    var presenter: LauncherPresenter = AppKitLauncherPresenter()

    init() {
        // The build script stamps the checkout path into Info.plist; GOW2_REPO
        // overrides it (useful when the app is moved).
        repo = LauncherCore.repoURL()
        let logs = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Logs/GoW2Recomp", isDirectory: true)
        try? FileManager.default.createDirectory(at: logs, withIntermediateDirectories: true)
        logURL = logs.appendingPathComponent("game.log")
    }

    func refresh() async {
        let hint = Task { @MainActor in
            try? await Task.sleep(nanoseconds: 3_000_000_000)
            if !Task.isCancelled {
                waitingHint = "Aguardando o macOS liberar o acesso à pasta do jogo. Se aparecer o pedido de permissão (Documentos), clique em Permitir."
            }
        }
        defer { hint.cancel(); waitingHint = nil }
        let repo = self.repo
        status = await Task.detached { LauncherCore.status(repo: repo) }.value
    }

    private func mutate(_ change: (inout LauncherConfig) -> Void) async {
        var c = LauncherCore.load(repo: repo)
        change(&c)
        do { try LauncherCore.save(c, repo: repo); error = nil }
        catch { self.error = "Não foi possível salvar a configuração: \(error.localizedDescription)" }
        await refresh()
    }

    func setPaths(elf: String? = nil, usrdir: String? = nil, movieCache: String? = nil) async {
        await mutate { c in
            if let elf { c.elf = elf }
            if let usrdir { c.vfs_root = usrdir }
            if let movieCache { c.movie_cache = movieCache }
        }
    }

    func setMods(enabled: [String]) async {
        await mutate { c in
            let names = Set(LauncherCore.modNames(c.mods_dir))
            c.mods_enabled = enabled.filter { names.contains($0) }
        }
    }

    func importMod(zip: URL) async {
        let dir = LauncherCore.load(repo: repo).mods_dir
        do {
            let name = try await Task.detached { try LauncherCore.importZip(zip, modsDir: dir) }.value
            await mutate { c in if !c.mods_enabled.contains(name) { c.mods_enabled.append(name) } }
        } catch {
            self.error = "Falha ao importar o mod: \(error.localizedDescription)"
        }
    }

    // MARK: game process

    func play(resume: Bool, settings: GameSettings, patchFile: String? = nil) {
        guard game == nil else { return }
        let c = LauncherCore.load(repo: repo)
        let binary = LauncherCore.findBinary(repo: repo)
        guard LauncherCore.isELF(c.elf), LauncherCore.isUSRDIR(c.vfs_root), !binary.isEmpty else {
            error = "Configure os arquivos do jogo e compile o executável antes de jogar."
            return
        }
        if resume && !LauncherCore.autosaveAvailable(c) {
            error = "Nenhum autosave válido para continuar."
            return
        }
        try? FileManager.default.createDirectory(atPath: c.mods_dir, withIntermediateDirectories: true)
        FileManager.default.createFile(atPath: logURL.path, contents: nil)
        guard let handle = try? FileHandle(forWritingTo: logURL) else {
            error = "Não foi possível criar o log em \(logURL.path)"
            return
        }
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/bin/bash")
        p.arguments = ["-c", LauncherCore.launchScript(repo: repo, c: c, binary: binary, resume: resume)]
        p.currentDirectoryURL = repo
        var env = ProcessInfo.processInfo.environment
        for (k, v) in settings.environment { env[k] = v }
        if let patchFile { env["PS3_PATCH_FILE"] = patchFile } else { env.removeValue(forKey: "PS3_PATCH_FILE") }
        p.environment = env
        p.standardOutput = handle
        p.standardError = handle
        do {
            try GameSession.start(p, presenter: presenter) { [weak self] in
                self?.game = nil
                self?.running = false
                self?.logTimer?.invalidate()
                self?.readLogTail()
            }
            game = p
            running = true
            error = nil
            logTimer = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
                Task { @MainActor in self?.readLogTail() }
            }
        } catch {
            self.error = "Não foi possível iniciar o jogo: \(error.localizedDescription)"
        }
    }

    func stop() {
        game?.terminate()
    }

    /// Last lines of the game log, without the per-job SPU chatter.
    func readLogTail(maxLines: Int = 400, hideNoise: Bool = true) {
        guard let h = try? FileHandle(forReadingFrom: logURL) else { return }
        defer { try? h.close() }
        let size = (try? h.seekToEnd()) ?? 0
        let window: UInt64 = 512 * 1024
        try? h.seek(toOffset: size > window ? size - window : 0)
        let text = String(decoding: h.readDataToEndOfFile(), as: UTF8.self)
        var lines = text.split(separator: "\n", omittingEmptySubsequences: true).map(String.init)
        if hideNoise {
            let noisy = ["[SPUJOB]", "[SPUABORT]", "[LFQ", "[FIFOKICK]", "[RSX DRAW", "[HOSTINFL]", "[MOVIEFSM]"]
            lines = lines.filter { l in !noisy.contains { l.hasPrefix($0) } }
        }
        logTail = Array(lines.suffix(maxLines))
    }

    func reveal(_ path: String) {
        guard !path.isEmpty else { return }
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: path)])
    }
}
