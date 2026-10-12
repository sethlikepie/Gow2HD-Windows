import Foundation

/// A small YAML reader for RPCS3's patch.yml: block maps and lists by
/// indentation, flow lists ("[ be32, 0x10, 0x1 ]"), quoted scalars, comments,
/// anchors (&name) and aliases (*name). It is not a general YAML parser; it
/// covers what patch.yml actually uses, and fails soft on anything else.
indirect enum YNode {
    case scalar(String)
    case list([YNode])
    case map([(String, YNode)])

    subscript(key: String) -> YNode? {
        if case .map(let kv) = self { return kv.first { $0.0 == key }?.1 }
        return nil
    }
    var string: String? { if case .scalar(let s) = self { return s }; return nil }
    var list: [YNode]? { if case .list(let l) = self { return l }; return nil }
    var pairs: [(String, YNode)]? { if case .map(let kv) = self { return kv }; return nil }
}

struct YAMLLite {
    private struct Line { let indent: Int; let text: String }
    private var lines: [Line] = []
    private var pos = 0
    private(set) var anchors: [String: YNode] = [:]

    static func parse(_ source: String) -> (root: YNode, anchors: [String: YNode]) {
        var p = YAMLLite()
        for raw in source.split(separator: "\n", omittingEmptySubsequences: false) {
            let stripped = YAMLLite.stripComment(String(raw))
            let trimmed = stripped.trimmingCharacters(in: .whitespaces)
            if trimmed.isEmpty { continue }
            let indent = stripped.prefix { $0 == " " }.count
            p.lines.append(Line(indent: indent, text: trimmed))
        }
        let root = p.block(indent: 0)
        return (root, p.anchors)
    }

    /// Drops a trailing "# comment" that is not inside quotes.
    private static func stripComment(_ s: String) -> String {
        var inQuote: Character? = nil
        var prev: Character = " "
        var escaped = false
        for (i, c) in s.enumerated() {
            if escaped { escaped = false; prev = c; continue }
            if c == "\\", inQuote == "\"" { escaped = true; prev = c; continue }
            if c == "\"" || c == "'" { inQuote = inQuote == nil ? c : (inQuote == c ? nil : inQuote) }
            if c == "#", inQuote == nil, prev == " " || i == 0 { return String(s.prefix(i)) }
            prev = c
        }
        return s
    }

    private mutating func block(indent: Int) -> YNode {
        guard pos < lines.count else { return .scalar("") }
        if lines[pos].text.hasPrefix("- ") || lines[pos].text == "-" { return seq(indent: lines[pos].indent) }
        return mapping(indent: lines[pos].indent)
    }

    private mutating func seq(indent: Int) -> YNode {
        var items: [YNode] = []
        while pos < lines.count, lines[pos].indent == indent, lines[pos].text.hasPrefix("-") {
            let rest = String(lines[pos].text.dropFirst()).trimmingCharacters(in: .whitespaces)
            pos += 1
            if rest.isEmpty { items.append(block(indent: indent + 1)) }
            else { items.append(value(rest, childIndent: indent)) }
        }
        return .list(items)
    }

    private mutating func mapping(indent: Int) -> YNode {
        var kv: [(String, YNode)] = []
        while pos < lines.count, lines[pos].indent == indent, !lines[pos].text.hasPrefix("- ") {
            let t = lines[pos].text
            guard let colon = YAMLLite.keyColon(t) else { pos += 1; continue }
            let key = YAMLLite.unquote(String(t[..<colon]).trimmingCharacters(in: .whitespaces))
            let rest = String(t[t.index(after: colon)...]).trimmingCharacters(in: .whitespaces)
            pos += 1
            kv.append((key, value(rest, childIndent: indent)))
        }
        // Sequences may sit at the same indent as their key (RPCS3 style).
        return .map(kv)
    }

    /// Value after "key:" or "- ": inline scalar/flow list/alias, or a nested block.
    private mutating func value(_ rest: String, childIndent: Int) -> YNode {
        var rest = rest
        var anchor: String? = nil
        if rest.hasPrefix("&") {
            let name = rest.dropFirst().prefix { !$0.isWhitespace }
            anchor = String(name)
            rest = String(rest.dropFirst(name.count + 1)).trimmingCharacters(in: .whitespaces)
        }
        let node: YNode
        if rest.isEmpty, pos < lines.count, lines[pos].indent > childIndent,
           !lines[pos].text.hasPrefix("- "), YAMLLite.keyColon(lines[pos].text) == nil {
            // "key:" followed by a scalar on the next, deeper line(s)
            var parts: [String] = []
            while pos < lines.count, lines[pos].indent > childIndent { parts.append(lines[pos].text); pos += 1 }
            node = .scalar(YAMLLite.unquote(parts.joined(separator: " ")))
        } else if rest.isEmpty {
            if pos < lines.count, lines[pos].indent > childIndent
                || (lines[pos].indent == childIndent && lines[pos].text.hasPrefix("- ")) {
                node = block(indent: lines[pos].indent)
            } else { node = .scalar("") }
        } else if rest.hasPrefix("*") {
            node = anchors[String(rest.dropFirst())] ?? .scalar("")
        } else if rest.hasPrefix("[") {
            node = .list(YAMLLite.flowList(rest).map { item in
                item.hasPrefix("*") ? (anchors[String(item.dropFirst())] ?? .scalar(item)) : .scalar(item)
            })
        } else {
            node = .scalar(YAMLLite.unquote(rest))
        }
        if let anchor { anchors[anchor] = node }
        return node
    }

    /// Position of the key/value colon (": " or line-ending ":"), skipping quotes.
    private static func keyColon(_ s: String) -> String.Index? {
        var inQuote = false
        var i = s.startIndex
        while i < s.endIndex {
            let c = s[i]
            if c == "\\", inQuote {   // skip the escaped character
                i = s.index(after: i)
                if i < s.endIndex { i = s.index(after: i) }
                continue
            }
            if c == "\"" { inQuote.toggle() }
            if c == ":", !inQuote {
                let next = s.index(after: i)
                if next == s.endIndex || s[next] == " " { return i }
            }
            i = s.index(after: i)
        }
        return nil
    }

    private static func unquote(_ s: String) -> String {
        if s.count >= 2, s.hasPrefix("\""), s.hasSuffix("\"") {
            return String(s.dropFirst().dropLast())
                .replacingOccurrences(of: "\\\"", with: "\"")
                .replacingOccurrences(of: "\\n", with: "\n")
        }
        if s.count >= 2, s.hasPrefix("'"), s.hasSuffix("'") { return String(s.dropFirst().dropLast()) }
        return s
    }

    /// "[ be32, 0x10, "Aspect Ratio" ]" -> ["be32", "0x10", "Aspect Ratio"]
    static func flowList(_ s: String) -> [String] {
        var body = s.trimmingCharacters(in: .whitespaces)
        if body.hasPrefix("[") { body.removeFirst() }
        if body.hasSuffix("]") { body.removeLast() }
        var out: [String] = [], cur = "", inQuote = false, escaped = false
        for c in body {
            if escaped { cur.append(c); escaped = false; continue }
            if c == "\\", inQuote { escaped = true; continue }
            if c == "\"" { inQuote.toggle(); continue }
            if c == ",", !inQuote { out.append(cur.trimmingCharacters(in: .whitespaces)); cur = ""; continue }
            cur.append(c)
        }
        let last = cur.trimmingCharacters(in: .whitespaces)
        if !last.isEmpty { out.append(last) }
        return out
    }
}

/// One patch from patch.yml, resolved for a given PPU hash.
struct GamePatch: Identifiable, Hashable {
    struct Write: Hashable { let type: String; let address: UInt64; let value: String }
    struct Configurable: Hashable {
        let name: String
        let type: String
        let defaultValue: String
        let options: [(String, String)]   // (label, value)
        static func == (a: Self, b: Self) -> Bool { a.name == b.name && a.defaultValue == b.defaultValue }
        func hash(into h: inout Hasher) { h.combine(name) }
    }
    let hash: String
    let name: String
    let author: String
    let notes: String
    let writes: [Write]
    let configurables: [Configurable]
    var id: String { hash + "/" + name }
}

enum PatchCatalog {
    /// Every patch in `source` for the executable `ppuHash`.
    static func patches(in source: String, ppuHash: String) -> [GamePatch] {
        let (root, anchors) = YAMLLite.parse(source)
        guard let block = root[ppuHash]?.pairs else { return [] }
        return block.compactMap { name, node -> GamePatch? in
            guard let lines = node["Patch"]?.list else { return nil }
            var writes: [GamePatch.Write] = []
            func add(_ entry: YNode) {
                guard let f = entry.list?.compactMap(\.string) else {
                    // an alias to a list of entries (load/anchor blocks)
                    entry.list?.forEach(add)
                    return
                }
                if f.count >= 2, f[0] == "load", let target = anchors[f[1]]?.list {
                    target.forEach(add)
                } else if f.count >= 3, let addr = UInt64(f[1].replacingOccurrences(of: "0x", with: ""), radix: 16) {
                    writes.append(.init(type: f[0], address: addr, value: f[2]))
                }
            }
            lines.forEach(add)
            let configs = (node["Configurable Values"]?.pairs ?? []).map { cname, c -> GamePatch.Configurable in
                let opts = (c["Allowed Values"]?.pairs ?? []).compactMap { label, v in v.string.map { (label, $0) } }
                return .init(name: cname, type: c["Type"]?.string ?? "", defaultValue: c["Value"]?.string ?? "", options: opts)
            }
            return GamePatch(hash: ppuHash, name: name, author: node["Author"]?.string ?? "",
                             notes: node["Notes"]?.string ?? "", writes: writes, configurables: configs)
        }
    }

    /// Flattens the enabled patches into the runtime's PS3_PATCH_FILE format,
    /// substituting configurable values by name.
    static func render(_ patches: [GamePatch], values: [String: String]) -> String {
        var out = "# generated by GoW2 Recomp launcher\n"
        for p in patches {
            out += "# patch: \(p.name)\n"
            for w in p.writes {
                var v = w.value
                if let cfg = p.configurables.first(where: { $0.name == w.value }) {
                    v = values[p.id + "/" + cfg.name] ?? cfg.defaultValue
                }
                let type = w.type == "utf8" || w.type == "c_utf8" ? w.type : w.type
                let value = type.hasSuffix("utf8") ? "\"\(v)\"" : v
                out += "\(type) 0x\(String(w.address, radix: 16)) \(value)\n"
            }
        }
        return out
    }
}
