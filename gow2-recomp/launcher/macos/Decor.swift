import SwiftUI

/// Greek key (meander) band: a running square spiral along the width, the
/// border motif of God of War's menus. `h` is the band height.
struct MeanderBand: Shape {
    func path(in rect: CGRect) -> Path {
        var p = Path()
        let h = rect.height
        guard h > 0 else { return p }
        let u = h / 4                      // grid unit: the spiral is 4x4 units
        let period = 5 * u                 // one motif + one unit of spacing
        var x = rect.minX
        let top = rect.minY, bottom = rect.maxY
        while x + 4 * u <= rect.maxX + 0.5 {
            // One motif, drawn as a single polyline from the baseline.
            p.move(to: CGPoint(x: x, y: bottom))
            p.addLine(to: CGPoint(x: x, y: top))
            p.addLine(to: CGPoint(x: x + 4 * u, y: top))
            p.addLine(to: CGPoint(x: x + 4 * u, y: bottom - u))
            p.addLine(to: CGPoint(x: x + u, y: bottom - u))
            p.addLine(to: CGPoint(x: x + u, y: top + u))
            p.addLine(to: CGPoint(x: x + 3 * u, y: top + u))
            p.addLine(to: CGPoint(x: x + 3 * u, y: top + 2 * u))
            p.addLine(to: CGPoint(x: x + 2 * u, y: top + 2 * u))
            // baseline to the next motif
            p.move(to: CGPoint(x: x, y: bottom))
            p.addLine(to: CGPoint(x: min(x + period, rect.maxX), y: bottom))
            x += period
        }
        return p
    }
}

/// A gold meander divider with hairlines above and below.
struct MeanderDivider: View {
    var opacity: Double = 0.8
    var body: some View {
        VStack(spacing: 2) {
            Rectangle().fill(Theme.C.gold.opacity(opacity * 0.6)).frame(height: 1)
            MeanderBand()
                .stroke(Theme.C.gold.opacity(opacity), style: StrokeStyle(lineWidth: 1, lineCap: .square))
                .frame(height: Theme.R.meander)
            Rectangle().fill(Theme.C.gold.opacity(opacity * 0.6)).frame(height: 1)
        }
        .accessibilityHidden(true)
    }
}

/// Procedural stone: deterministic speckles and a few veins, drawn once.
struct StoneTexture: View {
    var body: some View {
        Canvas(rendersAsynchronously: true) { ctx, size in
            var rng = SplitMix(seed: 0x60D2)
            let n = Int(size.width * size.height / 900)
            for _ in 0 ..< n {
                let x = rng.next01() * size.width, y = rng.next01() * size.height
                let r = 0.5 + rng.next01() * 1.6
                let light = rng.next01() > 0.5
                ctx.fill(Path(ellipseIn: CGRect(x: x, y: y, width: r, height: r)),
                         with: .color((light ? Theme.C.parchment : Color.black).opacity(0.035 + rng.next01() * 0.04)))
            }
            for _ in 0 ..< 6 {
                var p = Path()
                var pt = CGPoint(x: rng.next01() * size.width, y: rng.next01() * size.height)
                p.move(to: pt)
                for _ in 0 ..< 8 {
                    pt.x += (rng.next01() - 0.3) * 90
                    pt.y += (rng.next01() - 0.5) * 60
                    p.addLine(to: pt)
                }
                ctx.stroke(p, with: .color(Color.black.opacity(0.18)), lineWidth: 0.7)
            }
        }
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

/// Embers rising from the bottom, computed from time (no per-frame state):
/// each particle has a seeded speed, sway and size; they fade near the top.
struct EmberField: View {
    let count: Int
    let running: Bool
    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 60, paused: !running)) { tl in
            Canvas { ctx, size in
                let t = tl.date.timeIntervalSinceReferenceDate
                var rng = SplitMix(seed: 0xE3B)
                ctx.blendMode = .plusLighter
                for _ in 0 ..< count {
                    let x0 = rng.next01() * size.width
                    let rise = 9 + rng.next01() * 13             // seconds per screen height
                    let phase = rng.next01()
                    let sway = 6 + rng.next01() * 8
                    let r = 1.0 + rng.next01() * 2.2
                    let hot = rng.next01() > 0.6
                    let f = (t / rise + phase).truncatingRemainder(dividingBy: 1)
                    let y = size.height * (1 - f)
                    let x = x0 + sin(t * 0.8 + phase * 12) * sway
                    let fade = min(1, (1 - f) / 0.35) * min(1, f / 0.08)
                    let color = hot ? Theme.C.goldHi : Theme.C.ember
                    ctx.fill(Path(ellipseIn: CGRect(x: x - r * 2.5, y: y - r * 2.5, width: r * 5, height: r * 5)),
                             with: .color(color.opacity(0.10 * fade)))
                    ctx.fill(Path(ellipseIn: CGRect(x: x - r / 2, y: y - r / 2, width: r, height: r)),
                             with: .color(color.opacity(0.85 * fade)))
                }
            }
        }
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

/// Serif caps title with a gold core and a slowly breathing fire glow.
final class Breath: ObservableObject { @Published var up = false }

struct FireTitle: View {
    let text: String
    let font: Font
    let tracking: CGFloat
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @StateObject private var breath = Breath()

    var body: some View {
        let glow = breath.up && !reduceMotion ? Theme.M.glowMax : Theme.M.glowMin
        Text(text)
            .font(font)
            .tracking(tracking)
            .foregroundStyle(LinearGradient(colors: [Theme.C.parchment, Theme.C.goldHi],
                                            startPoint: .top, endPoint: .bottom))
            .shadow(color: Theme.C.ember.opacity(0.55), radius: glow)
            .shadow(color: Theme.C.gold.opacity(0.35), radius: glow / 3)
            .animation(reduceMotion ? nil : Theme.M.glowBreath, value: breath.up)
            .onAppear {
                guard !reduceMotion else { return }
                DispatchQueue.main.async { breath.up = true }
            }
            .accessibilityAddTraits(.isHeader)
    }
}

/// Small deterministic PRNG so textures and particles are stable per launch.
struct SplitMix {
    private var state: UInt64
    init(seed: UInt64) { state = seed }
    mutating func next() -> UInt64 {
        state &+= 0x9E3779B97F4A7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58476D1CE4E5B9
        z = (z ^ (z >> 27)) &* 0x94D049BB133111EB
        return z ^ (z >> 31)
    }
    mutating func next01() -> CGFloat { CGFloat(next() >> 11) / CGFloat(1 << 53) }
}
