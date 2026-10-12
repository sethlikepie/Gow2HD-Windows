import Foundation

// Launcher -> runtime handoff: one settings file, no forced env overrides.
var fails = 0
func check(_ c: Bool, _ m: String) { if !c { fails += 1; print("FAIL:", m) } }

let tmp = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("launch_check-\(getpid())")
try? FileManager.default.removeItem(at: tmp)
try! FileManager.default.createDirectory(at: tmp, withIntermediateDirectories: true)

// 1. update() keeps unrelated lines and order, replaces known keys, appends new ones.
let file = OverlaySettingsFile(url: tmp.appendingPathComponent("s/runtime-overlay.settings"))
try! file.update(["fullscreen": "0"])
check(file.read()["version"] == "2" && file.read()["fullscreen"] == "0", "created with version")
try! "version=1\nvsync=0\nanalog_deadzone=12\n".write(to: file.url, atomically: true, encoding: .utf8)
try! file.update(["vsync": "1", "fullscreen": "1"])
let text = try! String(contentsOf: file.url, encoding: .utf8)
check(text == "version=1\nvsync=1\nanalog_deadzone=12\nfullscreen=1\n", "preserve+replace+append: \(text)")
let mode = (try! FileManager.default.attributesOfItem(atPath: file.url.path)[.posixPermissions] as! NSNumber).intValue
check(mode == 0o600, "mode 0600, got \(String(mode, radix: 8))")

MainActor.assumeIsolated {
    // 2. Seeding from the old AppStorage values when the file lacks the keys.
    let defaults = UserDefaults(suiteName: "launch_check-\(getpid())")!
    defaults.set(false, forKey: "fullscreen")
    let seeded = OverlaySettingsFile(url: tmp.appendingPathComponent("seed.settings"))
    let s = GameSettings(overlayFile: seeded, legacy: defaults)
    check(s.fullscreen == false && s.vsync == true, "seeded from legacy")
    check(seeded.read()["fullscreen"] == "0" && seeded.read()["vsync"] == "1", "seed written")
    // 3. Toggling writes the file; the environment carries the path, never the flags.
    s.fullscreen = true
    check(seeded.read()["fullscreen"] == "1", "toggle persisted")
    let env = s.environment
    check(env["PS3_FULLSCREEN"] == nil && env["PS3_METAL_VSYNC"] == nil, "no forced overrides: \(env)")
    check(env["PS3_OVERLAY_SETTINGS"] == seeded.url.path, "settings path handed over")
    // 4. A file edited by the in-game menu wins on reload.
    try! seeded.update(["fullscreen": "0"])
    s.reloadFromFile()
    check(s.fullscreen == false, "reload from file")
    defaults.removePersistentDomain(forName: "launch_check-\(getpid())")

    // 5. reloadFromFile() must not echo the value back to disk through didSet:
    //    no wasted write, no read-modify-write race with the in-game writer.
    //    Pin the mtime to a known-past date and confirm reload never touches it.
    let rlDefaults = UserDefaults(suiteName: "launch_check-reload-\(getpid())")!
    let rl = OverlaySettingsFile(url: tmp.appendingPathComponent("reload.settings"))
    try! rl.update(["fullscreen": "1", "vsync": "1"])
    let rlGS = GameSettings(overlayFile: rl, legacy: rlDefaults)
    check(rlGS.fullscreen == true, "reload fixture starts true")
    try! rl.update(["fullscreen": "0"]) // the "in-game menu" changes the file
    let past = Date(timeIntervalSince1970: 0)
    try! FileManager.default.setAttributes([.modificationDate: past], ofItemAtPath: rl.url.path)
    rlGS.reloadFromFile()
    check(rlGS.fullscreen == false, "reload picked up the new value")
    let mtimeAfterReload = try! FileManager.default.attributesOfItem(atPath: rl.url.path)[.modificationDate] as! Date
    check(mtimeAfterReload == past, "reload did not write back to the file (mtime unchanged)")
    rlDefaults.removePersistentDomain(forName: "launch_check-reload-\(getpid())")

    // 6. A write failure (read-only directory) surfaces saveError and never
    //    crashes -- both for the seed write in init and for a later toggle.
    let roDir = tmp.appendingPathComponent("readonly", isDirectory: true)
    try! FileManager.default.createDirectory(at: roDir, withIntermediateDirectories: true)
    try! FileManager.default.setAttributes([.posixPermissions: 0o500], ofItemAtPath: roDir.path)
    let roDefaults = UserDefaults(suiteName: "launch_check-ro-\(getpid())")!
    let roFile = OverlaySettingsFile(url: roDir.appendingPathComponent("sub/runtime-overlay.settings"))
    let roGS = GameSettings(overlayFile: roFile, legacy: roDefaults)
    check(roGS.saveError != nil, "seed write failure surfaced in init: \(roGS.saveError ?? "nil")")
    roGS.fullscreen.toggle()
    check(roGS.saveError != nil, "toggle write failure surfaced: \(roGS.saveError ?? "nil")")
    roDefaults.removePersistentDomain(forName: "launch_check-ro-\(getpid())")
    try? FileManager.default.setAttributes([.posixPermissions: 0o700], ofItemAtPath: roDir.path)
}

// 7. End to end: generated launch script -> env -> the runtime's own
//    precedence (overlay_env_probe links rsx_overlay_settings.c and prints what
//    rsx_metal_backend_init would pick). Three cases: caller env > saved file >
//    backend default, with env_gow2.sh's own VSync default in the way.
let probe = CommandLine.arguments[1]
let repo = tmp.appendingPathComponent("repo")
try! FileManager.default.createDirectory(at: repo, withIntermediateDirectories: true)
try! ": \"${PS3_METAL_VSYNC:=1}\"; export PS3_METAL_VSYNC\n".write(
    to: repo.appendingPathComponent("env_gow2.sh"), atomically: true, encoding: .utf8)
var cfg = LauncherConfig()
cfg.mods_dir = tmp.appendingPathComponent("mods").path
let script = LauncherCore.launchScript(repo: repo, c: cfg, binary: probe, resume: false)
func run(settings: URL, _ extra: [String: String]) -> String {
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/bin/bash")
    p.arguments = ["-c", script]
    var env = ProcessInfo.processInfo.environment
    env.removeValue(forKey: "PS3_FULLSCREEN"); env.removeValue(forKey: "PS3_METAL_VSYNC")
    env["PS3_OVERLAY_SETTINGS"] = settings.path
    for (k, v) in extra { env[k] = v }
    p.environment = env
    let pipe = Pipe(); p.standardOutput = pipe
    try! p.run(); p.waitUntilExit()
    return String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
        .trimmingCharacters(in: .whitespacesAndNewlines)
}
MainActor.assumeIsolated {
    // A. Caller env wins over the saved file.
    let a = OverlaySettingsFile(url: tmp.appendingPathComponent("a.settings"))
    try! a.update(["fullscreen": "0", "vsync": "1"])
    check(run(settings: a.url, ["PS3_FULLSCREEN": "1", "PS3_METAL_VSYNC": "0"]) == "fullscreen=1 vsync=0",
          "A: caller env wins")
    // B. Saved file (written by the launcher's own toggles) wins over script defaults.
    let bDefaults = UserDefaults(suiteName: "launch_check-b-\(getpid())")!
    let b = OverlaySettingsFile(url: tmp.appendingPathComponent("b.settings"))
    let gs = GameSettings(overlayFile: b, legacy: bDefaults)
    gs.fullscreen = true
    gs.vsync = false
    check(run(settings: b.url, [:]) == "fullscreen=1 vsync=0", "B: saved settings win over env_gow2.sh default")
    bDefaults.removePersistentDomain(forName: "launch_check-b-\(getpid())")
    // C. Nothing saved, nothing in env: backend defaults.
    let c = tmp.appendingPathComponent("absent.settings")
    check(run(settings: c, [:]) == "fullscreen=0 vsync=1", "C: backend defaults")
}

// 8. The launcher hides while the game runs and comes back when it exits;
//    a start failure never hides it. The presenter is the injectable hook.
@MainActor final class FakePresenter: LauncherPresenter {
    var hides = 0, shows = 0, exits = 0
    func hideForGame() { hides += 1 }
    func showAfterGame() { shows += 1 }
}
MainActor.assumeIsolated {
    let ok = FakePresenter()
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/bin/true")
    var threw = false
    do { try GameSession.start(p, presenter: ok) { ok.exits += 1 } } catch { threw = true }
    check(!threw && ok.hides == 1 && ok.shows == 0, "hidden once right after a successful start (\(ok.hides)/\(ok.shows))")
    let deadline = Date().addingTimeInterval(10)
    while ok.exits == 0 && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.02)) }
    check(ok.hides == 1 && ok.shows == 1 && ok.exits == 1, "shown once after the game exits (\(ok.hides)/\(ok.shows)/\(ok.exits))")

    let bad = FakePresenter()
    let q = Process()
    q.executableURL = tmp.appendingPathComponent("no-such-binary")
    threw = false
    do { try GameSession.start(q, presenter: bad) { bad.exits += 1 } } catch { threw = true }
    RunLoop.main.run(until: Date().addingTimeInterval(0.2))
    check(threw && bad.hides == 0 && bad.shows == 0 && bad.exits == 0, "start failure never hides (\(bad.hides)/\(bad.shows))")
}

try? FileManager.default.removeItem(at: tmp)
print(fails == 0 ? "PASS" : "FAIL \(fails)")
exit(fails == 0 ? 0 : 1)
