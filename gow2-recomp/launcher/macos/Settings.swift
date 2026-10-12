import Foundation
import SwiftUI

/// Player-facing options. Every one maps to something the runtime already
/// reads; nothing here invents behaviour. Fullscreen and VSync live in the
/// runtime overlay settings file -- the same file the in-game menu edits.
@MainActor
final class GameSettings: ObservableObject {
    @Published var fullscreen: Bool { didSet { if !isLoadingFromFile && fullscreen != oldValue { write("fullscreen", fullscreen) } } }
    @Published var vsync: Bool { didSet { if !isLoadingFromFile && vsync != oldValue { write("vsync", vsync) } } }
    @AppStorage("metalFX") var metalFX = true
    @AppStorage("hdr") var hdr = false
    @AppStorage("frameFence") var frameFence = true
    @AppStorage("soundOn") var soundOn = true
    @AppStorage("movieSkip") var movieSkip = true
    @AppStorage("autostart") var autostart = false
    @AppStorage("extraEnv") var extraEnv = ""

    /// Set when the last write to the overlay settings file failed
    /// (permissions, disk full, sandbox); cleared on the next successful one.
    /// Shown to the player so a stuck toggle never fails silently.
    @Published var saveError: String?

    let overlayFile: OverlaySettingsFile

    /// Guards fullscreen/vsync's didSet while a value is being loaded FROM
    /// the file (init, reloadFromFile), so reading the file never turns into
    /// a write -- no wasted I/O and no read-modify-write race with whatever
    /// last wrote it (the in-game menu).
    private var isLoadingFromFile = false

    init(overlayFile: OverlaySettingsFile = OverlaySettingsFile(
            url: LauncherCore.overlaySettingsURL(repo: LauncherCore.repoURL())),
         legacy: UserDefaults = .standard) {
        self.overlayFile = overlayFile
        let saved = overlayFile.read()
        // First run of this launcher: carry over the old toggles (default on).
        let fs = OverlaySettingsFile.flag(saved["fullscreen"]) ?? (legacy.object(forKey: "fullscreen") as? Bool ?? true)
        let vs = OverlaySettingsFile.flag(saved["vsync"]) ?? (legacy.object(forKey: "vsync") as? Bool ?? true)
        isLoadingFromFile = true
        fullscreen = fs
        vsync = vs
        isLoadingFromFile = false
        var seed: [String: String] = [:]
        if saved["fullscreen"] == nil { seed["fullscreen"] = fs ? "1" : "0" }
        if saved["vsync"] == nil { seed["vsync"] = vs ? "1" : "0" }
        if !seed.isEmpty { persist(seed) }
    }

    /// The in-game menu may have changed the file since the last read. Reads
    /// only -- never writes back (see isLoadingFromFile).
    func reloadFromFile() {
        let saved = overlayFile.read()
        isLoadingFromFile = true
        if let f = OverlaySettingsFile.flag(saved["fullscreen"]), f != fullscreen { fullscreen = f }
        if let v = OverlaySettingsFile.flag(saved["vsync"]), v != vsync { vsync = v }
        isLoadingFromFile = false
    }

    private func write(_ key: String, _ on: Bool) { persist([key: on ? "1" : "0"]) }
    private func flag(_ on: Bool) -> String { on ? "1" : "0" }

    /// Writes to the overlay settings file and surfaces any failure instead
    /// of swallowing it -- a silently failed write would leave the toggle
    /// shown on screen diverged from what's actually on disk.
    private func persist(_ changes: [String: String]) {
        do {
            try overlayFile.update(changes)
            saveError = nil
        } catch {
            NSLog("[GoW2 launcher] falha ao salvar configurações do overlay em %@: %@",
                  overlayFile.url.path, String(describing: error))
            saveError = "Não foi possível salvar as configurações: \(error.localizedDescription)"
        }
    }

    var environment: [String: String] {
        var env: [String: String] = [
            "PS3_OVERLAY_SETTINGS": overlayFile.url.path, // fullscreen/VSync + in-game menu
            "PS3_METALFX": flag(metalFX),            // MetalFX spatial upscale
            "PS3_METAL_HDR": flag(hdr),
            "PS3_METAL_FRAME_FENCE": flag(frameFence), // no 1-frame giant triangle
            "PS3_MUTE": flag(!soundOn),              // audio_mute.c
            "PS3_MOVIE_SKIP": flag(movieSkip),       // cellPad.c: START/CROSS skips
            "PS3_PAD_AUTOSTART": flag(autostart),    // presses Start through the intro
        ]
        // Advanced: KEY=VALUE per line, for testers (PS3_FULLSCREEN=... still wins).
        for line in extraEnv.split(separator: "\n") {
            let parts = line.split(separator: "=", maxSplits: 1).map {
                $0.trimmingCharacters(in: .whitespaces)
            }
            if parts.count == 2, parts[0].hasPrefix("PS3_") { env[parts[0]] = parts[1] }
        }
        return env
    }
}
