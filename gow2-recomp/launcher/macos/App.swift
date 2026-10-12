import SwiftUI

@main
struct GoW2RecompApp: App {
    @StateObject private var backend = Backend()
    @StateObject private var settings = GameSettings()
    @StateObject private var patches = PatchStore()

    var body: some Scene {
        Window("GoW2 Recomp", id: "main") {   // single window: always opens, no restored "closed" state
            ContentView()
                .environmentObject(backend)
                .environmentObject(settings)
                .environmentObject(patches)
                .frame(minWidth: 900, minHeight: 600)
                .tint(Theme.C.gold)          // text-bearing controls: 8.1:1 on stone
                .toggleStyle(BloodSwitch())    // switches stay blood (MASTER)
                .preferredColorScheme(.dark)
        }
        .windowStyle(.hiddenTitleBar)
        .commands {
            CommandGroup(after: .newItem) {
                Button("Jogar") { backend.play(resume: false, settings: settings, patchFile: patches.writePatchFile()) }
                    .keyboardShortcut("r")
                    .disabled(backend.running || !(backend.status?.setup_ok ?? false))
            }
        }
    }
}
