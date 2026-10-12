import Foundation
// Check harness: parses the real RPCS3 patch.yml and the real EBOOT.
let yml = try! String(contentsOfFile: CommandLine.arguments[1], encoding: .utf8)
let eboot = CommandLine.arguments[2]
var fails = 0
func check(_ c: Bool, _ m: String) { if !c { fails += 1; print("FAIL:", m) } }
let t0 = Date()
let hash = try! ELFInfo.ppuHash(eboot)
print("hash", hash, String(format: "%.2fs", Date().timeIntervalSince(t0)))
check(hash == "PPU-31e32090ea333902dbf322c24487bab7e8c8d0d1", "PPU hash of NPUA80491 01.00")
let info = try! ELFInfo.read(eboot)
print("exec ranges", info.executableRanges.map { String(format: "0x%llx-0x%llx", $0.lowerBound, $0.upperBound) })
let t1 = Date()
let mine = PatchCatalog.patches(in: yml, ppuHash: hash)
print("parse", String(format: "%.2fs", Date().timeIntervalSince(t1)), "patches:", mine.map(\.name))
check(mine.count == 1 && mine[0].name == "4:3 Aspect Ratio", "one patch for 01.00")
if let p = mine.first {
    check(p.writes == [.init(type: "bef32", address: 0x53ee4c, value: "Aspect Ratio")], "writes \(p.writes)")
    check(p.configurables.first?.options.count == 7, "7 aspect options \(p.configurables.first?.options.count ?? -1)")
    check(p.configurables.first?.defaultValue == "3.555555555555556", "default via anchor \(p.configurables.first?.defaultValue ?? "")")
    check(!p.notes.isEmpty, "notes resolved from alias")
    print(PatchCatalog.render([p], values: [p.id + "/Aspect Ratio": "2.4"]))
}
let v101 = PatchCatalog.patches(in: yml, ppuHash: "PPU-9cd8d28963ebc29b59c79819cb56f3d92e857a37")
print("01.01:", v101.map { "\($0.name)=\($0.writes.count)" })
check(v101.contains { $0.name == "True widescreen" && $0.writes.count == 3 }, "01.01 true widescreen")
print(fails == 0 ? "PASS" : "FAIL \(fails)")
exit(fails == 0 ? 0 : 1)
