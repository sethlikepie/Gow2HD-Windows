import SwiftUI
import AppKit
import UniformTypeIdentifiers

enum SidebarItem: String, CaseIterable, Identifiable {
    case play = "Jogar"
    case files = "Arquivos do jogo"
    case graphics = "Gráficos"
    case audioControls = "Áudio e controles"
    case patches = "Patches"
    case mods = "Mods"
    case log = "Log"
    case about = "Sobre"
    var id: String { rawValue }
    var icon: String {
        switch self {
        case .play: return "play.circle.fill"
        case .files: return "externaldrive.fill"
        case .graphics: return "display"
        case .audioControls: return "gamecontroller.fill"
        case .patches: return "wand.and.stars"
        case .mods: return "puzzlepiece.extension.fill"
        case .log: return "doc.text.magnifyingglass"
        case .about: return "info.circle"
        }
    }
}

/// Sidebar selection. An ObservableObject instead of @State: the Command Line
/// Tools toolchain has no SwiftUI macro plugin, and @State is a macro in the
/// current SDK.
final class NavModel: ObservableObject {
    /// GOW2_SECTION=<raw value> opens on that screen (screenshots, debugging).
    @Published var section: SidebarItem? = ProcessInfo.processInfo.environment["GOW2_SECTION"]
        .flatMap { v in SidebarItem.allCases.first { $0.rawValue == v || "\($0)" == v } } ?? .play
}

struct ContentView: View {
    @EnvironmentObject var backend: Backend
    @StateObject private var nav = NavModel()
    @Namespace private var selectionNS
    @Environment(\.accessibilityReduceMotion) private var reduceMotion

    var body: some View {
        NavigationSplitView {
            ScrollView {
                VStack(alignment: .leading, spacing: Theme.S.xs) {
                    ForEach(Array(SidebarItem.allCases.enumerated()), id: \.element) { index, item in
                        SidebarRow(item: item, selected: (nav.section ?? .play) == item, ns: selectionNS) {
                            withAnimation(reduceMotion ? nil : Theme.M.selection) { nav.section = item }
                        }
                        .keyboardShortcut(KeyEquivalent(Character("\(index + 1)")), modifiers: .command)
                        .help("\(item.rawValue) (⌘\(index + 1))")
                    }
                }
                .padding(.horizontal, Theme.S.sm)
                .padding(.top, Theme.S.sm)
            }
            .navigationSplitViewColumnWidth(min: 190, ideal: 210)
        } detail: {
            Group {
                switch nav.section ?? .play {
                case .play: PlayView()
                case .files: FilesView()
                case .graphics: GraphicsView()
                case .audioControls: AudioControlsView()
                case .patches: PatchesView()
                case .mods: ModsView()
                case .log: LogView()
                case .about: AboutView()
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)
            .background(ZStack { Theme.C.stone950; StoneTexture() })
            .id(nav.section)
            .transition(reduceMotion ? .opacity
                        : .opacity.combined(with: .offset(y: Theme.M.sectionRise)))
            .animation(Theme.M.section, value: nav.section)
        }
        .task { await backend.refresh() }
    }
}

// MARK: - Play

/// Drives the hero's Ken Burns. ObservableObject, not @State (no macros).
final class KenBurns: ObservableObject {
    @Published var zoomed = false
}

struct PlayView: View {
    @EnvironmentObject var backend: Backend
    @EnvironmentObject var settings: GameSettings
    @EnvironmentObject var patchStore: PatchStore

    var body: some View {
        ZStack(alignment: .bottomLeading) {
            HeroBackdrop()
            VStack(alignment: .leading, spacing: Theme.S.xl) {
                Spacer(minLength: Theme.S.hero)
                VStack(alignment: .leading, spacing: Theme.S.sm) {
                    Eyebrow(text: "Port nativo · Apple Silicon")
                    FireTitle(text: "GOD OF WAR II", font: Theme.F.display, tracking: Theme.F.displayTracking)
                    MeanderDivider(opacity: 0.85).frame(width: 420)
                    Text("Recompilado estaticamente do PS3 para o seu Mac. Sem emulador.")
                        .font(Theme.F.body)
                        .foregroundStyle(Theme.C.ash)
                }
                if let err = backend.error {
                    Banner(text: err, color: Theme.C.blood, icon: "exclamationmark.triangle.fill")
                        .frame(maxWidth: 560, alignment: .leading)
                }
                if let st = backend.status {
                    HStack(spacing: Theme.S.sm) {
                        StatusChip(ok: st.elf_ok, label: "EBOOT")
                        StatusChip(ok: st.vfs_ok, label: "USRDIR")
                        StatusChip(ok: !st.binary.isEmpty, label: "Executável")
                        if !patchStore.enabled.isEmpty {
                            StatusChip(ok: true, label: "\(patchStore.patches.filter { patchStore.enabled.contains($0.id) }.count) patch(es)", icon: "wand.and.stars")
                        }
                    }
                    actions(st)
                    if !st.setup_ok || st.binary.isEmpty {
                        Text(st.binary.isEmpty ? "Compile o executável com ./build_macos.sh recomp_macos_e435." : st.message)
                            .font(Theme.F.caption).foregroundStyle(Theme.C.ash)
                    }
                } else {
                    HStack(spacing: Theme.S.sm) {
                        ProgressView().controlSize(.small)
                        Text("Verificando arquivos…").font(Theme.F.body).foregroundStyle(Theme.C.ash)
                    }
                    if let hint = backend.waitingHint {
                        Banner(text: hint, color: Theme.C.amber, icon: "lock.shield")
                            .frame(maxWidth: 560, alignment: .leading)
                    }
                }
            }
            .padding(Theme.S.hero)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .clipped()
    }

    @ViewBuilder private func actions(_ st: LauncherStatus) -> some View {
        HStack(spacing: Theme.S.md) {
            if backend.running {
                Button { backend.stop() } label: { Label("Parar", systemImage: "stop.fill") }
                    .buttonStyle(SecondaryButtonStyle(tint: Theme.C.blood))
                    .accessibilityHint("Encerra o jogo")
                ProgressView().controlSize(.small)
                Text("Em jogo").font(Theme.F.body).foregroundStyle(Theme.C.ash)
            } else {
                Button { backend.play(resume: false, settings: settings, patchFile: patchStore.writePatchFile()) } label: {
                    Label("Jogar", systemImage: "play.fill").frame(minWidth: 120)
                }
                .buttonStyle(PrimaryButtonStyle())
                .keyboardShortcut(.defaultAction)
                .disabled(!st.setup_ok || st.binary.isEmpty)
                .accessibilityHint("Inicia o jogo com as configurações atuais")
                Button { backend.play(resume: true, settings: settings, patchFile: patchStore.writePatchFile()) } label: {
                    Label("Continuar", systemImage: "clock.arrow.circlepath")
                }
                .buttonStyle(SecondaryButtonStyle())
                .disabled(!st.autosave || !st.setup_ok || st.binary.isEmpty)
                .help(st.autosave ? "Retoma o último save automático" : "Nenhum autosave ainda")
            }
        }
    }
}

/// Full-bleed hero: the screenshot with a slow Ken Burns, a bottom vignette
/// for the text and a side fade into the stone.
struct HeroBackdrop: View {
    @Environment(\.accessibilityReduceMotion) private var reduceMotion
    @StateObject private var kb = KenBurns()

    var body: some View {
        GeometryReader { geo in
            ZStack {
                Theme.C.stone950
                if let url = Bundle.main.url(forResource: "hero", withExtension: "jpg"),
                   let img = NSImage(contentsOf: url) {
                    Image(nsImage: img)
                        .resizable()
                        .aspectRatio(contentMode: .fill)
                        .frame(width: geo.size.width, height: geo.size.height)
                        .scaleEffect(kb.zoomed && !reduceMotion ? Theme.M.kenBurnsScale : 1, anchor: .trailing)
                        // Scoped to the zoom only: a withAnimation in onAppear would also
                        // animate the window's first layout for 24 s.
                        .animation(reduceMotion ? nil : Theme.M.kenBurns, value: kb.zoomed)
                        .clipped()
                        .accessibilityHidden(true)
                }
                LinearGradient(colors: [Theme.C.stone950.opacity(0.15), Theme.C.stone950.opacity(0.95)],
                               startPoint: .center, endPoint: .bottom)
                LinearGradient(colors: [Theme.C.stone950.opacity(0.85), .clear],
                               startPoint: .leading, endPoint: .center)
                EmberField(count: Theme.M.embers, running: !reduceMotion)
                    .opacity(reduceMotion ? 0 : 1)
            }
        }
        .onAppear {
            guard !reduceMotion else { return }
            DispatchQueue.main.async { kb.zoomed = true }   // after the first layout pass
        }
    }
}

struct StatusChip: View {
    let ok: Bool
    let label: String
    var icon: String? = nil
    var body: some View {
        HStack(spacing: Theme.S.xs) {
            Image(systemName: icon ?? (ok ? "checkmark.circle.fill" : "xmark.circle.fill"))
                .foregroundStyle(ok ? Theme.C.laurel : Theme.C.blood)
            Text(label).font(Theme.F.caption).foregroundStyle(Theme.C.parchment)
        }
        .padding(.horizontal, Theme.S.md)
        .padding(.vertical, Theme.S.xs + 2)
        .background(Theme.C.stone900.opacity(0.85), in: Capsule())
        .overlay(Capsule().strokeBorder(Theme.C.stone700, lineWidth: 1))
        .accessibilityElement(children: .combine)
        .accessibilityLabel("\(label): \(ok ? "ok" : "faltando")")
    }
}

struct Banner: View {
    let text: String
    let color: Color
    let icon: String
    var body: some View {
        Label(text, systemImage: icon)
            .font(Theme.F.body)
            .padding(Theme.S.md)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(color.opacity(0.14), in: RoundedRectangle(cornerRadius: Theme.R.md, style: .continuous))
            .foregroundStyle(color)
    }
}

/// Sidebar entry drawn from the tokens (the system List selection would use
/// the system accent blue, which the visual thesis forbids).
struct SidebarRow: View {
    let item: SidebarItem
    let selected: Bool
    let ns: Namespace.ID
    let action: () -> Void
    @StateObject private var hover = HoverState()

    var body: some View {
        Button(action: action) {
            HStack(spacing: Theme.S.md) {
                Image(systemName: item.icon)
                    .frame(width: 18)
                    .foregroundStyle(selected ? Theme.C.goldHi : Theme.C.gold)
                Text(item.rawValue)
                    .font(Theme.F.body.weight(selected ? .semibold : .regular))
                    .foregroundStyle(selected ? Theme.C.goldHi : Theme.C.parchment)
                Spacer(minLength: 0)
            }
            .padding(.horizontal, Theme.S.md)
            .frame(height: 34)
            .background {
                if selected {
                    // One selection, sliding between rows (matchedGeometryEffect).
                    ZStack(alignment: .bottom) {
                        RoundedRectangle(cornerRadius: Theme.R.sm, style: .continuous)
                            .fill(Theme.C.blood.opacity(0.22))
                        MeanderBand()
                            .stroke(Theme.C.gold.opacity(0.8), lineWidth: 1)
                            .frame(height: Theme.R.meander * 0.75)
                            .padding(.horizontal, Theme.S.sm)
                            .padding(.bottom, 2)
                    }
                    .matchedGeometryEffect(id: "selection", in: ns)
                } else if hover.on {
                    RoundedRectangle(cornerRadius: Theme.R.sm, style: .continuous)
                        .fill(Theme.C.stone800)
                }
            }
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .onHover { h in withAnimation(Theme.M.hover) { hover.on = h } }
        .accessibilityLabel(item.rawValue)
        .accessibilityAddTraits(selected ? [.isSelected] : [])
    }
}

/// Serif screen title, shared by every non-Play screen.
struct ScreenHeader: View {
    let title: String
    let subtitle: String
    var body: some View {
        Section {
            VStack(alignment: .leading, spacing: Theme.S.sm) {
                FireTitle(text: title.uppercased(), font: Theme.F.title, tracking: Theme.F.titleTracking)
                Text(subtitle).font(Theme.F.body).foregroundStyle(Theme.C.ash)
                MeanderDivider(opacity: 0.7).padding(.top, Theme.S.xs)
            }
            .padding(.vertical, Theme.S.xs)
        }
    }
}

// MARK: - Files

struct FilesView: View {
    @EnvironmentObject var backend: Backend

    var body: some View {
        Form {
            ScreenHeader(title: "Arquivos do jogo", subtitle: "Aponte para a sua própria cópia do jogo.")
            Section {
                Text("Como no Dusk, o jogo não vem junto: aponte para a sua própria cópia de God of War II HD (NPUA80491). O EBOOT precisa estar descriptografado (RPCS3 → Utilities → Decrypt PS3 Binaries).")
                    .font(Theme.F.body).foregroundStyle(Theme.C.ash)
            }
            if let st = backend.status {
                PathPicker(title: "EBOOT.ELF", path: st.elf, ok: st.elf_ok, directory: false) { url in
                    Task { await backend.setPaths(elf: url.path) }
                }
                PathPicker(title: "Pasta USRDIR", path: st.vfs_root, ok: st.vfs_ok, directory: true) { url in
                    Task { await backend.setPaths(usrdir: url.path) }
                }
                PathPicker(title: "Cache de vídeos (movie_cache)", path: st.movie_cache,
                           ok: st.movie_cache_ok, directory: true) { url in
                    Task { await backend.setPaths(movieCache: url.path) }
                }
                Section("Saves") {
                    HStack {
                        Text(st.savedata_root).font(Theme.F.caption).foregroundStyle(Theme.C.ash)
                            .lineLimit(1).truncationMode(.middle)
                        Spacer()
                        Button("Mostrar no Finder") { backend.reveal(st.savedata_root) }
                    }
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
        .background(Theme.C.stone950)
    }
}

struct PathPicker: View {
    let title: String
    let path: String
    let ok: Bool
    let directory: Bool
    let onPick: (URL) -> Void

    var body: some View {
        Section(title) {
            HStack {
                Image(systemName: ok ? "checkmark.circle.fill" : "questionmark.circle")
                    .foregroundStyle(ok ? Theme.C.laurel : Theme.C.amber)
                Text(path.isEmpty ? "Não definido" : path).font(Theme.F.body)
                    .lineLimit(1).truncationMode(.middle)
                Spacer()
                Button("Escolher…") {
                    let panel = NSOpenPanel()
                    panel.canChooseFiles = !directory
                    panel.canChooseDirectories = directory
                    panel.allowsMultipleSelection = false
                    if panel.runModal() == .OK, let url = panel.url { onPick(url) }
                }
            }
        }
    }
}

// MARK: - Graphics / Audio

struct GraphicsView: View {
    @EnvironmentObject var settings: GameSettings
    var body: some View {
        Form {
            ScreenHeader(title: "Gráficos", subtitle: "Tela, imagem e opções avançadas.")
            if let err = settings.saveError {
                Banner(text: err, color: Theme.C.blood, icon: "exclamationmark.triangle.fill")
            }
            Section("Tela") {
                Toggle("Tela cheia ao abrir", isOn: $settings.fullscreen)
                Text("Também muda no menu do jogo (botão PS, Select+Start ou F1). F11 ou Cmd+Enter alternam durante o jogo; Esc solta o mouse.")
                    .font(Theme.F.caption).foregroundStyle(Theme.C.ash)
                Toggle("VSync", isOn: $settings.vsync)
            }
            Section("Imagem") {
                Toggle("Upscale MetalFX (720p → resolução da tela)", isOn: $settings.metalFX)
                Toggle("HDR (EDR)", isOn: $settings.hdr)
                Toggle("Ritmo de quadros estável (fence por quadro)", isOn: $settings.frameFence)
                Text("O fence evita um triângulo gigante que pisca por um quadro, com um pequeno custo de fps.")
                    .font(Theme.F.caption).foregroundStyle(Theme.C.ash)
            }
            Section("Avançado") {
                Text("Variáveis PS3_* extras, uma por linha (ex.: PS3_TRACE_FPS=1)")
                    .font(Theme.F.caption).foregroundStyle(Theme.C.ash)
                TextEditor(text: $settings.extraEnv)
                    .font(.system(.body, design: .monospaced))
                    .frame(minHeight: 80)
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
        .background(Theme.C.stone950)
        .onAppear { settings.reloadFromFile() }
    }
}

struct AudioControlsView: View {
    @EnvironmentObject var settings: GameSettings
    private let keys: [(String, String)] = [
        ("Mover", "W A S D"), ("Câmera", "Mouse"), ("✕ / ○ / □ / △", "Espaço / E / J / K"),
        ("L1 / R1", "Shift esq. / Q"), ("L2 / R2", "Ctrl esq. / R"), ("L3 / R3", "F / G"),
        ("Start / Select", "Enter / Tab"), ("D-pad", "Setas"),
    ]
    var body: some View {
        Form {
            ScreenHeader(title: "Áudio e controles", subtitle: "Som, cutscenes e mapeamento.")
            Section("Áudio") { Toggle("Som ligado", isOn: $settings.soundOn) }
            Section("Cutscenes") {
                Toggle("Pular cutscene com Start ou ✕", isOn: $settings.movieSkip)
                Toggle("Apertar Start automaticamente na intro", isOn: $settings.autostart)
            }
            Section("Controle") {
                Text("DualShock 4, DualSense, Xbox e MFi funcionam ao conectar, com vibração.")
                    .font(Theme.F.body).foregroundStyle(Theme.C.ash)
            }
            Section("Teclado e mouse") {
                ForEach(keys, id: \.0) { k in
                    LabeledContent(k.0) { Text(k.1).font(.system(.body, design: .monospaced)) }
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
        .background(Theme.C.stone950)
    }
}

// MARK: - Patches

struct PatchesView: View {
    @EnvironmentObject var backend: Backend
    @EnvironmentObject var store: PatchStore

    var body: some View {
        Form {
            ScreenHeader(title: "Patches", subtitle: "Formato do RPCS3, escolhidos pelo hash do seu EBOOT.")
            Section {
                Text("Patches no formato do RPCS3 (patch.yml), escolhidos pelo hash do seu EBOOT como o RPCS3 faz. Patches de dados funcionam aqui; patches de código não têm efeito, porque o código já foi recompilado.")
                    .font(Theme.F.body).foregroundStyle(Theme.C.ash)
                if let p = store.progress {
                    ProgressView("Analisando o EBOOT…", value: p)
                } else if let h = store.ppuHash {
                    LabeledContent("Executável") { Text(h).font(Theme.F.mono).textSelection(.enabled) }
                }
                if let e = store.error { Banner(text: e, color: Theme.C.blood, icon: "exclamationmark.triangle.fill") }
                HStack {
                    Button("Importar patch.yml…") {
                        let panel = NSOpenPanel()
                        panel.allowedContentTypes = [.yaml]
                        if panel.runModal() == .OK, let url = panel.url {
                            Task { await store.importFile(url, elf: backend.status?.elf ?? "") }
                        }
                    }
                    Button("Recarregar") { Task { await store.load(elf: backend.status?.elf ?? "") } }
                }
                Text("Fontes: " + (store.sources.isEmpty ? "nenhuma (instale o RPCS3 ou importe um patch.yml)" : store.sources.map(\.lastPathComponent).joined(separator: ", ")))
                    .font(Theme.F.caption).foregroundStyle(Theme.C.ash)
            }
            if store.patches.isEmpty, store.progress == nil {
                Section { Text("Nenhum patch para este executável.").foregroundStyle(Theme.C.ash) }
            }
            ForEach(store.patches) { patch in
                Section {
                    Toggle(isOn: Binding(get: { store.enabled.contains(patch.id) },
                                         set: { on in if on { store.enabled.insert(patch.id) } else { store.enabled.remove(patch.id) } })) {
                        HStack {
                            Text(patch.name).font(Theme.F.headline)
                            Text(store.isCode(patch) ? "código · sem efeito" : "dados")
                                .font(Theme.F.eyebrow).padding(.horizontal, 6).padding(.vertical, 2)
                                .background((store.isCode(patch) ? Theme.C.amber : Theme.C.laurel).opacity(0.2), in: Capsule())
                        }
                    }
                    if !patch.author.isEmpty { LabeledContent("Autor", value: patch.author) }
                    ForEach(patch.configurables, id: \.name) { cfg in
                        if !cfg.options.isEmpty {
                            Picker(cfg.name, selection: Binding(
                                get: { store.values[patch.id + "/" + cfg.name] ?? cfg.defaultValue },
                                set: { store.values[patch.id + "/" + cfg.name] = $0 })) {
                                ForEach(cfg.options, id: \.1) { opt in Text(opt.0).tag(opt.1) }
                                if !cfg.options.contains(where: { $0.1 == cfg.defaultValue }) {
                                    Text("Padrão (\(cfg.defaultValue))").tag(cfg.defaultValue)
                                }
                            }
                        }
                    }
                    if !patch.notes.isEmpty {
                        Text(patch.notes).font(Theme.F.caption).foregroundStyle(Theme.C.ash)
                            .fixedSize(horizontal: false, vertical: true)
                    }
                }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
        .background(Theme.C.stone950)
        .task(id: backend.status?.elf) { await store.load(elf: backend.status?.elf ?? "") }
    }
}

// MARK: - Mods

struct ModsView: View {
    @EnvironmentObject var backend: Backend
    var body: some View {
        Form {
            ScreenHeader(title: "Mods", subtitle: "Arquivos que substituem os da USRDIR.")
            Section {
                Text("Mods substituem arquivos da USRDIR. Importe um .zip; ele é copiado para a pasta de mods.")
                    .font(Theme.F.body).foregroundStyle(Theme.C.ash)
                HStack {
                    Button("Importar .zip…") {
                        let panel = NSOpenPanel()
                        panel.allowedContentTypes = [.zip]
                        if panel.runModal() == .OK, let url = panel.url {
                            Task { await backend.importMod(zip: url) }
                        }
                    }
                    Button("Abrir pasta de mods") { backend.reveal(backend.status?.mods_dir ?? "") }
                }
            }
            if let mods = backend.status?.mods, !mods.isEmpty {
                Section("Instalados") {
                    ForEach(mods) { mod in
                        Toggle(mod.name, isOn: Binding(
                            get: { mod.enabled },
                            set: { on in
                                var names = mods.filter { $0.enabled }.map(\.name)
                                if on { names.append(mod.name) } else { names.removeAll { $0 == mod.name } }
                                Task { await backend.setMods(enabled: names) }
                            }))
                    }
                }
            } else {
                Section { Text("Nenhum mod instalado.").foregroundStyle(Theme.C.ash) }
            }
        }
        .formStyle(.grouped)
        .scrollContentBackground(.hidden)
        .background(Theme.C.stone950)
    }
}

// MARK: - Log / About

struct LogView: View {
    @EnvironmentObject var backend: Backend
    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text(backend.logURL.path).font(Theme.F.caption).foregroundStyle(Theme.C.ash)
                    .lineLimit(1).truncationMode(.middle)
                Spacer()
                Button("Atualizar") { backend.readLogTail() }
                Button("Abrir no Console") { NSWorkspace.shared.open(backend.logURL) }
            }
            ScrollViewReader { proxy in
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 1) {
                        ForEach(Array(backend.logTail.enumerated()), id: \.offset) { i, line in
                            Text(line).font(Theme.F.mono)
                                .textSelection(.enabled).id(i)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .onChange(of: backend.logTail.count) { _, n in
                    if n > 0 { proxy.scrollTo(n - 1, anchor: .bottom) }
                }
            }
            .background(Color(nsColor: .textBackgroundColor), in: RoundedRectangle(cornerRadius: 8))
        }
        .padding(16)
        .onAppear { backend.readLogTail() }
    }
}

struct AboutView: View {
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: Theme.S.lg) {
                FireTitle(text: "GOW2 RECOMP", font: Theme.F.title, tracking: Theme.F.titleTracking)
                MeanderDivider(opacity: 0.7).frame(maxWidth: 520)
                Text("God of War II HD recompilado estaticamente para macOS/arm64: o código PowerPC e SPU do jogo vira C/C++ nativo, rodando sobre uma reimplementação do sistema do PS3 com Metal, VideoToolbox e CoreAudio. Nenhum código ou arquivo do jogo é distribuído.")
                    .font(Theme.F.body).foregroundStyle(Theme.C.parchment)
                    .frame(maxWidth: 560, alignment: .leading)
                Button {
                    if let url = URL(string: "https://github.com/andrebrumdev/gow2-recomp") { NSWorkspace.shared.open(url) }
                } label: { Label("Projeto no GitHub", systemImage: "arrow.up.right.square") }
                    .buttonStyle(SecondaryButtonStyle())
                Text("God of War II © Sony Interactive Entertainment. Projeto sem afiliação com a Sony; é preciso ter o jogo. Motor ps3recomp sob licença MIT.")
                    .font(Theme.F.caption).foregroundStyle(Theme.C.ash)
                    .frame(maxWidth: 560, alignment: .leading)
            }
            .padding(Theme.S.hero)
            .frame(maxWidth: .infinity, alignment: .leading)
        }
    }
}
