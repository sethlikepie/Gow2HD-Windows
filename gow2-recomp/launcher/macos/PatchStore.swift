import Foundation
import SwiftUI

/// Patches available for the player's EBOOT, from RPCS3's patch.yml and any
/// imported .yml, matched by the executable's PPU hash exactly like RPCS3.
@MainActor
final class PatchStore: ObservableObject {
    @Published var ppuHash: String?
    @Published var progress: Double?
    @Published var patches: [GamePatch] = []
    @Published var error: String?
    @Published var enabled: Set<String> = [] { didSet { persist() } }
    @Published var values: [String: String] = [:] { didSet { persist() } }
    private var execRanges: [ClosedRange<UInt64>] = []

    static let rpcs3PatchFile = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/rpcs3/patches/patch.yml")
    static let supportDir = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Application Support/GoW2Recomp", isDirectory: true)
    static var importedDir: URL { supportDir.appendingPathComponent("patches", isDirectory: true) }

    init() {
        let d = UserDefaults.standard
        enabled = Set(d.stringArray(forKey: "patchEnabled") ?? [])
        values = d.dictionary(forKey: "patchValues") as? [String: String] ?? [:]
    }

    private func persist() {
        UserDefaults.standard.set(Array(enabled), forKey: "patchEnabled")
        UserDefaults.standard.set(values, forKey: "patchValues")
    }

    var sources: [URL] {
        var urls: [URL] = []
        if FileManager.default.fileExists(atPath: Self.rpcs3PatchFile.path) { urls.append(Self.rpcs3PatchFile) }
        let imported = (try? FileManager.default.contentsOfDirectory(at: Self.importedDir, includingPropertiesForKeys: nil)) ?? []
        urls += imported.filter { ["yml", "yaml"].contains($0.pathExtension.lowercased()) }.sorted { $0.path < $1.path }
        return urls
    }

    func isCode(_ p: GamePatch) -> Bool {
        p.writes.contains { w in execRanges.contains { $0.contains(w.address) } }
    }

    /// Hashes the EBOOT (streamed, with progress) and resolves the catalog.
    func load(elf: String) async {
        guard LauncherCore.isELF(elf) else { ppuHash = nil; patches = []; return }
        error = nil
        progress = 0
        let sources = self.sources
        do {
            let (hash, ranges, found) = try await Task.detached { () throws -> (String, [ClosedRange<UInt64>], [GamePatch]) in
                let hash = try ELFInfo.ppuHash(elf) { p in Task { @MainActor [weak self] in self?.progress = p } }
                let ranges = try ELFInfo.read(elf).executableRanges
                var seen = Set<String>(), all: [GamePatch] = []
                for url in sources {
                    guard let text = try? String(contentsOf: url, encoding: .utf8) else { continue }
                    for p in PatchCatalog.patches(in: text, ppuHash: hash) where seen.insert(p.id).inserted { all.append(p) }
                }
                return (hash, ranges, all)
            }.value
            ppuHash = hash
            execRanges = ranges
            patches = found
        } catch {
            self.error = error.localizedDescription
        }
        progress = nil
    }

    func importFile(_ url: URL, elf: String) async {
        do {
            try FileManager.default.createDirectory(at: Self.importedDir, withIntermediateDirectories: true)
            let dest = Self.importedDir.appendingPathComponent(url.lastPathComponent)
            if FileManager.default.fileExists(atPath: dest.path) { try FileManager.default.removeItem(at: dest) }
            try FileManager.default.copyItem(at: url, to: dest)
            await load(elf: elf)
        } catch {
            self.error = "Não foi possível importar: \(error.localizedDescription)"
        }
    }

    /// Writes the enabled patches for the runtime; nil when none is on.
    func writePatchFile() -> String? {
        let on = patches.filter { enabled.contains($0.id) }
        guard !on.isEmpty else { return nil }
        let url = Self.supportDir.appendingPathComponent("active_patches.txt")
        try? FileManager.default.createDirectory(at: Self.supportDir, withIntermediateDirectories: true)
        do {
            try PatchCatalog.render(on, values: values).write(to: url, atomically: true, encoding: .utf8)
            return url.path
        } catch {
            self.error = "Não foi possível gravar os patches: \(error.localizedDescription)"
            return nil
        }
    }
}
