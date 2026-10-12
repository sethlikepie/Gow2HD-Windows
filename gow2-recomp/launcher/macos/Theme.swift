import SwiftUI

/// Design tokens -- mirror of MASTER.md. No other file hard-codes a color,
/// size, radius or animation.
enum Theme {
    enum C {
        static let stone950 = Color(hex: 0x0E0D0C)
        static let stone900 = Color(hex: 0x171513)
        static let stone800 = Color(hex: 0x221F1C)
        static let stone700 = Color(hex: 0x2E2A26)
        static let parchment = Color(hex: 0xEDE6DC)
        static let ash = Color(hex: 0xA89F93)
        static let blood = Color(hex: 0x9B1B12)
        static let bloodHi = Color(hex: 0xB8261A)
        static let bloodLo = Color(hex: 0x6E120C)
        static let gold = Color(hex: 0xC9A15A)
        static let goldHi = Color(hex: 0xE3C27E)
        static let ember = Color(hex: 0xE0762C)
        static let laurel = Color(hex: 0x7FA36B)
        static let amber = Color(hex: 0xD0A23C)
    }

    enum F {
        static let display = Font.system(size: 44, weight: .bold, design: .serif)
        static let displayTracking: CGFloat = 6
        static let title = Font.system(size: 24, weight: .semibold, design: .serif)
        static let titleTracking: CGFloat = 2
        static let headline = Font.system(size: 15, weight: .semibold)
        static let body = Font.system(size: 13)
        static let caption = Font.system(size: 11)
        static let mono = Font.system(size: 11, design: .monospaced)
        static let eyebrow = Font.system(size: 11, weight: .semibold)
        static let eyebrowTracking: CGFloat = 2
    }

    enum S {
        static let xs: CGFloat = 4, sm: CGFloat = 8, md: CGFloat = 12, lg: CGFloat = 16
        static let xl: CGFloat = 24, xxl: CGFloat = 32, hero: CGFloat = 48
    }

    enum R {
        static let sm: CGFloat = 6, md: CGFloat = 10, lg: CGFloat = 14
        static let meander: CGFloat = 8
    }

    enum M {
        static let hover = Animation.easeOut(duration: 0.15)
        static let press = Animation.easeOut(duration: 0.10)
        static let section = Animation.easeOut(duration: 0.28)
        static let sectionRise: CGFloat = 10
        static let selection = Animation.spring(response: 0.30, dampingFraction: 1.0)
        static let glowBreath = Animation.easeInOut(duration: 3.2).repeatForever(autoreverses: true)
        static let glowMin: CGFloat = 6, glowMax: CGFloat = 14
        static let embers = 48
        static let kenBurns = Animation.easeInOut(duration: 24).repeatForever(autoreverses: true)
        static let kenBurnsScale: CGFloat = 1.06
        static let pressScale: CGFloat = 0.98
        static let glowRadius: CGFloat = 18
        static let glowOpacity: Double = 0.55
    }
}

extension Color {
    init(hex: UInt32) {
        self.init(.sRGB,
                  red: Double((hex >> 16) & 0xFF) / 255,
                  green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255,
                  opacity: 1)
    }
}

/// Hover flag for a single control. An ObservableObject (not @State): the
/// Command Line Tools toolchain has no SwiftUI macro plugin.
final class HoverState: ObservableObject {
    @Published var on = false
}

/// Primary action: blood gradient with a gold hairline, flame on hover, slight press.
struct PrimaryButtonStyle: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        PrimaryButtonBody(configuration: configuration)
    }
}

private struct PrimaryButtonBody: View {
    let configuration: ButtonStyle.Configuration
    @Environment(\.isEnabled) private var enabled
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @StateObject private var hover = HoverState()

    var body: some View {
        let lit = hover.on && enabled
        configuration.label
            .font(Theme.F.headline)
            .foregroundStyle(Theme.C.parchment)
            .padding(.horizontal, Theme.S.xl)
            .frame(minHeight: 44)
            .background(
                LinearGradient(colors: [lit ? Theme.C.bloodHi : Theme.C.blood,
                                        configuration.isPressed ? Theme.C.bloodLo : Theme.C.blood],
                               startPoint: .top, endPoint: .bottom),
                in: RoundedRectangle(cornerRadius: Theme.R.md, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: Theme.R.md, style: .continuous)
                        .strokeBorder(Theme.C.gold.opacity(lit ? 0.9 : 0.55), lineWidth: 1))
            .background(FlameGlow(active: lit, animated: !reduceMotion))
            .scaleEffect(configuration.isPressed ? Theme.M.pressScale : 1)
            .opacity(enabled ? 1 : 0.35)
            .onHover { h in withAnimation(Theme.M.hover) { hover.on = h } }
            .animation(Theme.M.press, value: configuration.isPressed)
            .contentShape(Rectangle())
    }
}

/// Ember glow behind the primary button; flickers while lit (~7 Hz).
struct FlameGlow: View {
    let active: Bool
    let animated: Bool
    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30, paused: !(active && animated))) { ctx in
            let t = ctx.date.timeIntervalSinceReferenceDate
            let flicker = animated ? 1 + 0.25 * sin(t * 44) * sin(t * 17 + 1.3) : 1
            RoundedRectangle(cornerRadius: Theme.R.md, style: .continuous)
                .fill(Theme.C.ember.opacity(active ? Theme.M.glowOpacity : 0))
                .blur(radius: Theme.M.glowRadius * CGFloat(flicker) / 2)
                .scaleEffect(active ? 1.08 : 0.9)
        }
        .allowsHitTesting(false)
        .accessibilityHidden(true)
    }
}

/// Secondary action: stone fill with a hairline.
struct SecondaryButtonStyle: ButtonStyle {
    var tint: Color = Theme.C.parchment
    func makeBody(configuration: Configuration) -> some View {
        SecondaryButtonBody(configuration: configuration, tint: tint)
    }
}

private struct SecondaryButtonBody: View {
    let configuration: ButtonStyle.Configuration
    let tint: Color
    @Environment(\.isEnabled) private var enabled
    @StateObject private var hover = HoverState()

    var body: some View {
        configuration.label
            .font(Theme.F.headline)
            .foregroundStyle(tint)
            .padding(.horizontal, Theme.S.lg)
            .frame(minHeight: 44)
            .background(hover.on && enabled ? Theme.C.stone700 : Theme.C.stone800,
                        in: RoundedRectangle(cornerRadius: Theme.R.md, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: Theme.R.md, style: .continuous)
                        .strokeBorder(Theme.C.stone700, lineWidth: 1))
            .scaleEffect(configuration.isPressed ? Theme.M.pressScale : 1)
            .opacity(enabled ? 1 : 0.35)
            .onHover { h in withAnimation(Theme.M.hover) { hover.on = h } }
            .animation(Theme.M.press, value: configuration.isPressed)
            .contentShape(Rectangle())
    }
}

/// Small caps label above a group.
struct Eyebrow: View {
    let text: String
    var body: some View {
        Text(text.uppercased())
            .font(Theme.F.eyebrow)
            .tracking(Theme.F.eyebrowTracking)
            .foregroundStyle(Theme.C.gold)
    }
}

/// Switch in the primary blood color. The app tint is gold because tinted
/// text (form buttons, picker values) must stay legible on stone; blood text
/// on stone900 is ~2:1.
struct BloodSwitch: ToggleStyle {
    func makeBody(configuration: Configuration) -> some View {
        Toggle(configuration).toggleStyle(.switch).tint(Theme.C.blood)
    }
}
