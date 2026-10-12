import Foundation
import CryptoKit

/// Reads just what the launcher needs from the player's EBOOT.ELF, streaming
/// the file instead of loading 10+ MB into memory.
struct ELFInfo {
    struct Segment { let type: UInt32; let flags: UInt32; let offset: UInt64; let vaddr: UInt64; let filesz: UInt64; let memsz: UInt64 }
    let segments: [Segment]

    /// PT_LOAD segments with PF_X: a patch there targets code that was
    /// recompiled ahead of time, so it cannot take effect without a re-lift.
    var executableRanges: [ClosedRange<UInt64>] {
        segments.filter { $0.type == 1 && $0.flags & 1 != 0 && $0.memsz > 0 }
            .map { $0.vaddr...($0.vaddr + $0.memsz - 1) }
    }

    enum Failure: Error, LocalizedError {
        case unreadable, notELF, notPPC64
        var errorDescription: String? {
            switch self {
            case .unreadable: return "Não foi possível ler o arquivo."
            case .notELF: return "Não é um ELF (o EBOOT.BIN criptografado precisa ser descriptografado)."
            case .notPPC64: return "Não é um executável PowerPC 64-bit big-endian de PS3."
            }
        }
    }

    private static func be<T: FixedWidthInteger>(_ d: Data, _ o: Int, _: T.Type = T.self) -> T {
        d[d.startIndex + o ..< d.startIndex + o + MemoryLayout<T>.size].reduce(T(0)) { ($0 << 8) | T($1) }
    }

    static func read(_ path: String) throws -> ELFInfo {
        guard let fh = FileHandle(forReadingAtPath: path) else { throw Failure.unreadable }
        defer { try? fh.close() }
        let hdr = fh.readData(ofLength: 64)
        guard hdr.count == 64, hdr.prefix(4) == Data([0x7F, 0x45, 0x4C, 0x46]) else { throw Failure.notELF }
        // ELFCLASS64, big-endian, e_machine = EM_PPC64 (21)
        guard hdr[4] == 2, hdr[5] == 2, be(hdr, 18, UInt16.self) == 21 else { throw Failure.notPPC64 }
        let phoff = be(hdr, 32, UInt64.self)
        let phentsize = Int(be(hdr, 54, UInt16.self))
        let phnum = Int(be(hdr, 56, UInt16.self))
        guard phentsize >= 56, phnum < 64 else { throw Failure.notPPC64 }
        try fh.seek(toOffset: phoff)
        let table = fh.readData(ofLength: phentsize * phnum)
        guard table.count == phentsize * phnum else { throw Failure.unreadable }
        let segs = (0 ..< phnum).map { i -> Segment in
            let o = i * phentsize
            return Segment(type: be(table, o), flags: be(table, o + 4), offset: be(table, o + 8),
                           vaddr: be(table, o + 16), filesz: be(table, o + 32), memsz: be(table, o + 40))
        }
        return ELFInfo(segments: segs)
    }

    /// The executable's identity as RPCS3 names it in patch.yml
    /// ("PPU-<sha1>"): per program header its big-endian p_type and p_flags,
    /// and for a non-empty PT_LOAD also p_vaddr, p_memsz and the file bytes.
    /// Verified against RPCS3's own patch.yml: GoW2 NPUA80491 01.00 hashes to
    /// PPU-31e32090ea333902dbf322c24487bab7e8c8d0d1.
    static func ppuHash(_ path: String, progress: ((Double) -> Void)? = nil) throws -> String {
        guard let fh = FileHandle(forReadingAtPath: path) else { throw Failure.unreadable }
        defer { try? fh.close() }
        let info = try read(path)
        let hdr = try { () throws -> Data in try fh.seek(toOffset: 0); return fh.readData(ofLength: 64) }()
        let phoff = be(hdr, 32, UInt64.self)
        let phentsize = Int(be(hdr, 54, UInt16.self))
        let total = Double(info.segments.filter { $0.type == 1 }.reduce(UInt64(0)) { $0 + $1.filesz })
        var done = 0.0
        var sha = Insecure.SHA1()
        for (i, seg) in info.segments.enumerated() {
            try fh.seek(toOffset: phoff + UInt64(i * phentsize))
            let ph = fh.readData(ofLength: phentsize)
            sha.update(data: ph.subdata(in: 0 ..< 4))   // p_type
            sha.update(data: ph.subdata(in: 4 ..< 8))   // p_flags
            guard seg.type == 1, seg.memsz > 0 else { continue }
            sha.update(data: ph.subdata(in: 16 ..< 24)) // p_vaddr
            sha.update(data: ph.subdata(in: 40 ..< 48)) // p_memsz
            try fh.seek(toOffset: seg.offset)
            var left = seg.filesz
            while left > 0 {
                let chunk = fh.readData(ofLength: Int(min(left, 1 << 20)))
                if chunk.isEmpty { throw Failure.unreadable }
                sha.update(data: chunk)
                left -= UInt64(chunk.count)
                done += Double(chunk.count)
                if total > 0 { progress?(done / total) }
            }
        }
        return "PPU-" + sha.finalize().map { String(format: "%02x", $0) }.joined()
    }
}
