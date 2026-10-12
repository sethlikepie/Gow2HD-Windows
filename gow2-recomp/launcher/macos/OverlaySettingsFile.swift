import Foundation

/// The runtime overlay settings file (libs/video/rsx_overlay_settings.c):
/// `key=value` lines. The launcher and the in-game menu edit the same file.
struct OverlaySettingsFile {
    let url: URL

    static func defaultURL(home: URL = FileManager.default.homeDirectoryForCurrentUser) -> URL {
        home.appendingPathComponent("Library/Application Support/ps3recomp/gow2/runtime-overlay.settings")
    }

    static func flag(_ value: String?) -> Bool? {
        switch value { case "1": return true; case "0": return false; default: return nil }
    }

    func read() -> [String: String] {
        guard let text = try? String(contentsOf: url, encoding: .utf8) else { return [:] }
        var out: [String: String] = [:]
        for line in text.split(separator: "\n") {
            let parts = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            if parts.count == 2 { out[String(parts[0])] = String(parts[1]) }
        }
        return out
    }

    /// Replaces the given keys in place, appends missing ones, keeps every
    /// other line; creates the file (with `version=2`) when absent.
    func update(_ changes: [String: String]) throws {
        var lines = (try? String(contentsOf: url, encoding: .utf8))?
            .components(separatedBy: "\n") ?? []
        if lines.last == "" { lines.removeLast() }
        var pending = changes
        lines = lines.map { line in
            let parts = line.split(separator: "=", maxSplits: 1, omittingEmptySubsequences: false)
            guard parts.count == 2, let value = pending.removeValue(forKey: String(parts[0])) else { return line }
            return "\(parts[0])=\(value)"
        }
        if !lines.contains(where: { $0.hasPrefix("version=") }) { lines.insert("version=2", at: 0) }
        for key in pending.keys.sorted() { lines.append("\(key)=\(pending[key]!)") }
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(),
                                                withIntermediateDirectories: true)
        try (lines.joined(separator: "\n") + "\n").write(to: url, atomically: true, encoding: .utf8)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }
}
