# God of War II HD — native macOS port by static recompilation

**God of War II HD** (PS3, `NPUA80491`) running **natively on Apple Silicon**:
no emulator, no JIT. The PowerPC (PPU) and Cell SPU code is translated ahead
of time into C/C++, compiled with clang for arm64, and runs on a
reimplementation of the PS3 OS and libraries with **Metal** for graphics,
**VideoToolbox** for cutscenes and **CoreAudio** for sound.

> This repository contains **no game code and no game assets**. Like
> [Dusk](https://duskport.com/dusk/), you bring your own legally dumped copy,
> and the setup kit decrypts, translates and builds it **on your machine**.

| | |
|---|---|
| ![Combat in the palace of Rhodes](docs/img/palace-combat.jpg) | ![The Colossus of Rhodes through the palace windows](docs/img/colossus-window.jpg) |
| ![Blades of Chaos effects](docs/img/blades-of-chaos.jpg) | ![Volumetric light in the palace](docs/img/palace-lighting.jpg) |

*Captured from the native Metal renderer on an M-series Mac (1280×720 internal,
MetalFX upscaled).*

---

## Status

| Area | State |
|---|---|
| Boot, intro videos, logos | ✅ natural path, videos decoded by VideoToolbox |
| Main menu, New Game, saves | ✅ memory-card saves on disk |
| Rhodes gameplay (combat, HUD, particles, lighting) | ✅ 45–60 fps on Apple Silicon |
| Colossus of Rhodes fight | 🟡 playable; the bronze drape on its shoulder is skinned with a sheared bone matrix (under investigation) |
| After the Colossus cutscene | 🔴 a script virtual call jumps to an invalid target (`ICALL-BAD 0x80029F47`); regression window narrowed to 2026-09-20 |
| Audio | 🟡 music/SFX through the SCREAM mixer SPU program; clock still partly host-driven |
| Controllers | ✅ DualShock 4 / DualSense / Xbox / MFi via GameController, keyboard + mouse |

The engineering log (every wall, the hypotheses refuted by measurement and the
fixes) lives in [`docs/`](docs/) and in the engine repository.

## How it works

```
EBOOT.ELF (your copy, decrypted)
   │
   ├── PPU  ── ppu_lifter.py ──► ~650 C++ chunks, 69k functions ──┐
   │          (functions.json: curated function bounds,           │
   │           jump tables, mid-function entry points)            │
   │                                                               ├─► clang -O1/-O2 arm64 ─► g2play
   └── SPU  ── spu_lifter.py ──► SPU jobs / SPURS policy modules ─┘            │
                                                                                ▼
                               ps3recomp runtime (MIT): LV2 syscalls, PPU threads,
                               lwmutex/lwcond, SPURS, FIOS, cellGcm → RSX FIFO walker
                               → Metal backend (MSL from RSX VP/FP), cellVdec → VideoToolbox,
                               cellAudio → CoreAudio, cellPad → GameController
```

What makes a PS3 title hard to recompile, and what this port had to solve:

- **Cell B.E.**: one PPU plus up to six SPUs with their own ISA and 256 KB local
  store; SPU programs (SPURS workloads, job chains, policy modules) are lifted
  separately and scheduled on host threads.
- **Indirect control flow**: 64-bit PowerPC function descriptors (`.opd`/TOC),
  vtables, computed `bctr` jump tables and mid-function entries, all of which
  the lifter must resolve statically.
- **RSX**: the command FIFO (with CALL/JUMP, wait points and ring wraps) is
  walked on the host, and the NV40-class vertex/fragment programs are
  decompiled to Metal Shading Language.
- **Faithfulness over shortcuts**: fixes follow console behaviour (RPCS3 is
  used as a measurement oracle via its GDB stub); diagnostics are gated off by
  default.

## Launcher (macOS)

![GoW2 Recomp launcher](docs/img/launcher-play.jpg)

`GoW2 Recomp.app` is a native SwiftUI launcher in the spirit of Dusk:
point it at your own `EBOOT.ELF` and `USRDIR`, pick graphics/audio/control
options, manage mods and play. Build it with:

```
launcher/macos/build_app.sh     # -> ./GoW2 Recomp.app
```

- **No Python, no interpreter**: validation, config, autosave checks (SHA-256
  per file) and mod import run in Swift inside the app.
- **RPCS3-style patches**: the launcher computes your executable's RPCS3 PPU
  hash by streaming the ELF (for NPUA80491 01.00 it is
  `PPU-31e32090ea333902dbf322c24487bab7e8c8d0d1`), reads RPCS3's
  `patch.yml` (anchors, aliases, configurable values) or any `.yml` you
  import, and passes the enabled ones to the runtime (`PS3_PATCH_FILE`). Data
  patches (e.g. aspect ratio) take effect; code patches are flagged, because
  the code was already recompiled and would need a re-lift.
- Design system in [`launcher/macos/MASTER.md`](launcher/macos/MASTER.md).

## Bring your own game (macOS kit)

A [Dusk](https://duskport.com/dusk/)-style kit: point it at your own game
folder (`PS3_GAME`, from RPCS3's `dev_hdd0/game/NPUA80491` or your PS3) and
your license (`UP9000-NPUA80491_00-GODOFWARIIHDUS00.rap`), and it builds the
native binary on your Mac:

```bash
./kit/setup.sh /path/to/PS3_GAME        # finds the .rap in RPCS3's exdata or ~/Downloads
./jogar_g2.sh
```

`setup.sh` does the following:

1. It decrypts `EBOOT.BIN` itself, the same way RPCS3 does (engine
   `tools/unself`).
2. It recompiles the PPU code with the pinned lifter, the patch scripts and a
   small delta.
3. It recompiles the seven SPU programs.
4. It extracts the movies from your `gow2.psarc`.
5. It links `./g2play`.

Every generated file is checked against the hashes in `kit/`. The result is
byte-identical to the build the project tests. Details are in
[`kit/README.md`](kit/README.md).

Controls: WASD move, mouse camera, Space/E/J/K = ✕/○/□/△, Enter = Start,
F11 or Cmd+Enter = fullscreen, Esc releases the mouse.

## State of the art in game recompilation

| Project | Platform | Approach | Notable result |
|---|---|---|---|
| [N64Recomp](https://github.com/N64Recomp/N64Recomp) | Nintendo 64 (MIPS) | Static recompilation to C | *Zelda 64: Recompiled* and many more; modding framework |
| [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) | Xbox 360 (PowerPC) | Static recompilation to C++ | *Unleashed Recompiled*, the first 360 recomp playable start to finish |
| ReXGlue | Xbox 360 | Static recompilation | Tooling for 360 ports |
| PS2Recomp | PlayStation 2 (MIPS R5900) | Static recompilation | Early stage |
| [psprecomp](https://github.com/sp00nznet/psprecomp) | PSP (Allegrex MIPS) | Static recompilation to C | Toolkit |
| [ps3recomp](https://github.com/sp00nznet/ps3recomp) | PlayStation 3 (PPU) | Static recompilation runtime | The base of this port's engine |
| **This port** | **PlayStation 3 (PPU + SPU)** | **Static recompilation, PPU and SPU, native Metal** | **A commercial PS3 title in gameplay on macOS/arm64** |
| [Dusk / Dusklight](https://duskport.com/) | GameCube | Decompilation (source rebuilt by hand) | *Twilight Princess* native port; you supply the ISO |
| [RPCS3](https://rpcs3.net/) | PlayStation 3 | Emulation (LLVM JIT/AOT) | The reference used here as an oracle |

Catalogs: [Recompendium](https://nio03.github.io/unricopie/en/),
[decompilation & recompilation list](https://readonlymemo.com/decompilation-projects-and-n64-recompiled-list/).

Static recompilation sits between emulation and decompilation: like a
decompilation it produces native code with no interpreter or JIT (so it can
use native APIs such as Metal directly), but like an emulator it needs no
hand-written game source. It only needs the original binary, which is why the
user has to supply it.

## Repository layout

- `functions.json` — curated PPU function bounds (truncation repairs,
  mid-function targets).
- `recomp_mid_v2/` — hand-written glue: SPU workload registration, function
  overrides, mid-asm hooks, and the idempotent `patch_*.py` scripts that must
  survive every re-lift.
- `config/gow2_recomp.toml` — lifter configuration (hooks).
- `jogar_g2.sh`, `env_gow2.sh`, `gow2_launcher.py` — launcher and environment.
- `docs/` — design notes and investigation logs.

Excluded on purpose (see `.gitignore`): `EBOOT.ELF`, `extracted/`, `*.psarc`,
SPU images, lifted PPU/SPU sources, texture dumps and build output. Those are
derived from the game and are regenerated from your own copy.

## Legal

God of War II is © Sony Interactive Entertainment. This project is not
affiliated with or endorsed by Sony. It distributes no copyrighted game code
or assets; you must own the game. The engine is MIT licensed
(© sp00nznet and contributors).

---

### Em português

Port nativo de **God of War II HD** (PS3) para **macOS/Apple Silicon** por
**recompilação estática**: o código PowerPC e SPU do jogo é traduzido para
C/C++ e compilado para arm64, rodando sobre uma reimplementação do sistema do
PS3 com Metal, VideoToolbox e CoreAudio. Nenhum código ou arquivo do jogo é
distribuído: como no Dusk, você fornece sua própria cópia e o kit de
instalação (`kit/setup.sh`) descriptografa, traduz e compila tudo na sua máquina.

Full technical history lives in the Claude project memory (`ps3recomp-feasibility`,
levas 13-17) and in `ps3recomp/docs/`.

## Central do jogo (PS/Home, Select+Start ou F1) e o launcher

`gow2_launcher.py` (setup + mods + "Jogar" numa tela só) também repassa as
configurações do overlay para o jogo. Esta seção documenta o overlay em si
(controles, onde ficam as configurações, o que cada diagnóstico significa) e
como ele se encaixa no fluxo do launcher.

**Importante:** o overlay é uma UI do host. Ele abrir/renderizar na tela **não
é prova** de que o jogo (guest) está desenhando pixels reais — essa aceitação
é separada (ver a cadeia de paredes A-G no `CLAUDE.md` do motor). Um print com
o menu aberto inclui a UI por cima do frame do jogo.

### Controles

- **Abrir/fechar a central:** botão **PS/Home** do controle (macOS, via
  GameController), **segure Select e aperte Start**, ou **F1** no teclado
  (`toggle_key=2` no arquivo troca para F2). A central sempre abre em
  **"Voltar ao jogo"** e o jogo continua rodando atrás dela, sem receber
  entrada. No Windows o botão PS/Home não é lido (XInput não o expõe):
  use Select+Start ou F1.
- **Navegação:** esquerda/direita troca de cartão; baixo (ou X/Enter) entra
  no painel; cima/baixo percorre as linhas; **X/Enter** confirma; **O/Esc**
  volta um nível e, nos cartões, fecha. PS/Home, Select+Start e F1 fecham
  de qualquer lugar. Um botão que ainda estiver pressionado quando a central
  fecha só chega ao jogo depois de solto (o X que confirmou "Voltar ao jogo"
  não vira pulo; o Enter não vira pausa).
- **Cartões:** Voltar ao jogo · Tela (tela cheia, VSync, tamanho da janela) ·
  Controles (zona morta 0–30 %, vibração do jogo, remapear botões, restaurar
  padrão) · Som (som do jogo, sons da interface, vibração da interface) ·
  Desenvolvedor (FPS/tempo de quadro, draws do jogo, estado do WAD, shaders,
  Logs) · Sair (pede confirmação; as configurações já estão salvas).
- **Remapear:** escolha o botão do PS3 na lista e pressione o botão do
  controle que deve acioná-lo; o antigo dono troca de lugar (o mapa nunca
  fica com botões repetidos). Esc cancela; sem resposta em 5 s, cancela.
- **Visual:** no macOS o jogo fica desfocado e escurecido atrás da central
  (Metal + MetalPerformanceShaders); no Windows só escurece. Texto em SF Pro
  e ícones SF Symbols no macOS; Inter (OFL, `third_party/inter`) e ícones
  geométricos no resto — ou se a fonte do sistema faltar.
- **Som do jogo** (menu) e **"Som ligado"** (launcher, `PS3_MUTE`) somam-se:
  o jogo só toca com os dois ligados.
- Valores não medidos aparecem como **"indisponível"** — nunca como zero.
- **O menu sempre inicia fechado.**

### Onde fica o arquivo de configurações

Local padrão por usuário (mesma fórmula usada pelo launcher e pelo runtime):

- **macOS:** `~/Library/Application Support/ps3recomp/gow2/runtime-overlay.settings`
- **Windows:** `%APPDATA%\ps3recomp\gow2\runtime-overlay.settings`
- **Linux:** `$XDG_CONFIG_HOME/ps3recomp/gow2/runtime-overlay.settings` (ou
  `~/.config/ps3recomp/gow2/runtime-overlay.settings` sem `XDG_CONFIG_HOME`)

Pode ser sobrescrito com a variável de ambiente `PS3_OVERLAY_SETTINGS`
(caminho explícito do arquivo). O `gow2_launcher.py` também aceita uma chave
opcional `"overlay_settings"` no `user_config.json`; vazia/ausente usa o
caminho padrão acima (configs antigas, sem essa chave, continuam funcionando
sem alteração). Se o arquivo estiver ausente, corrompido ou com um campo
inválido, o runtime cai de volta nos valores padrão desse campo (nunca
recusa iniciar por isso) — janela 1280x720, VSync ligado, mapeamento padrão.
Isso vale também para as chaves v2 (`ui_sounds`, `ui_haptics`, `game_mute`,
`vibration`): arquivos salvos por uma versão anterior do overlay, sem essas
chaves, carregam com os padrões (sons e vibração da interface ligados, som
do jogo ligado, vibração do jogo ligada).

### Precedência (variáveis de ambiente vs. configurações salvas)

**Tela cheia e VSync têm uma fonte só: o arquivo de configurações do
overlay.** O menu do jogo e o launcher SwiftUI (`GoW2 Recomp.app`) editam o
mesmo arquivo; o launcher não exporta mais `PS3_FULLSCREEN`/`PS3_METAL_VSYNC`
e passa o caminho em `PS3_OVERLAY_SETTINGS`. Na primeira execução do launcher
novo, um arquivo sem essas chaves recebe os valores que o launcher tinha
(padrão: ligados).

Um valor **explícito** ainda vence o arquivo: `PS3_FULLSCREEN`/
`PS3_METAL_VSYNC` definidos no ambiente de quem chamou (ou em "Avançado" no
launcher). O `env_gow2.sh` não força mais o VSync (só repassa o valor de quem
chamou). O `jogar_g2.sh`, o `gow2_launcher.py` e o launcher SwiftUI guardam o
valor do chamador antes de carregar o `env_gow2.sh` e removem a variável se
ele não definiu nada — nenhum padrão de script mascara o arquivo. O
`jogar_g2.sh` continua abrindo em **tela cheia** por padrão: se o arquivo
ainda não tem a chave `fullscreen`, ele grava `fullscreen=1` antes de iniciar
(depois vale o que a central salvar; `PS3_FULLSCREEN=0 ./jogar_g2.sh` abre em
janela só naquela vez). Arquivos gravados antes desta mudança podem já ter
`fullscreen=0`: basta ligar Tela cheia na central uma vez.

Se o launcher SwiftUI não conseguir gravar o arquivo, ele mostra na tela
"Não foi possível salvar as configurações: …" (com o motivo) em vez de
fingir que o ajuste mudou.

### Limitações por backend

| Ajuste | Metal (macOS) | D3D12 (Windows) |
|---|---|---|
| Tamanho de janela | aplica ao vivo | precisa reiniciar |
| Tela cheia | aplica ao vivo | precisa reiniciar (borderless no monitor primário, sem DPI awareness) |
| VSync | aplica ao vivo | aplica ao vivo |

A central marca cada ajuste que o backend atual não oferece como
"indisponível", e cada um que só vale depois de reiniciar mostra
"requer reiniciar" logo abaixo do nome — nenhum ajuste finge aplicar algo
que só foi salvo.

No Windows, hoje só o caminho de bridge D3D12 do `cellGcmSys.c` inicializa o
overlay; **não há ainda um provedor de diagnósticos de WAD/shader do GoW2
para Windows**, então as linhas de WAD e de shader aparecem como
"indisponível" lá (o overlay em si funciona; só falta o adaptador de
diagnóstico, que hoje só existe para o `boot_macos.cpp`).

### O que cada diagnóstico significa

- **FPS / tempo de frame:** contagem real da apresentação do backend — só os
  flips do jogo (guest). Apresentações do filme/intro (que correm na thread
  de decodificação) **não** entram nessa mesma contagem.
- **Draws do guest:** número de draw calls do próprio jogo, replays contados
  pelo backend — nunca inclui os draws do próprio menu do overlay. No Metal
  com `PS3_METAL_PER_DRAW_RT=1` (padrão do `env_gow2.sh`) os registros de
  clear e blit da mesma lista não entram na conta. No D3D12
  esse número é limitado a `MAX_DRAWS=256` por frame (registros reproduzidos,
  não o total real de draws se passar disso).
- **Shaders (hits/misses/compiles/failures):** "compiles" tem definição
  diferente por backend — no Metal é o número de pares VS+FS (vertex+
  fragment) compilados; no D3D12 é o número de compilações de fragment
  program bem-sucedidas. "Failures" conta decodificações/compilações que
  caíram para um caminho alternativo.
- **Estado do WAD / filme:** vem do caminho `movie_io` (o que o jogo abre via
  `PS3_MOVIE_IO=1`, ligado por padrão em `env_gow2.sh`); WADs lidos por outro
  caminho (psarc/FIOS direto) não aparecem aqui.
- **"Indisponível" nunca é "zero":** qualquer campo que o backend/provedor
  ainda não mediu aparece como "indisponível" (unavailable), nunca como `0`
  — um `0` no painel é sempre uma medição real de zero.
- **Log recente (aba Logs, separada da de Diagnóstico):** um anel limitado
  em memória (64 entradas; a aba mostra o anel inteiro, a mais nova por
  último); sob contenção alguma linha pode ser descartada (o registro nunca
  bloqueia a thread de render/guest para garantir isso).
- Se o backend não conseguir criar o renderer do overlay (ou a textura de
  fonte), o overlay se desliga sozinho para a sessão: F1/Select+Start não
  fazem nada e nenhuma entrada é retida do jogo (aviso
  `overlay disabled: ...` no `stderr`).

### Compatibilidade com launchers/scripts antigos

Arquivos `user_config.json` salvos antes do overlay continuam funcionando
sem qualquer alteração (a chave `overlay_settings` é opcional). Nenhum
comportamento de setup/extração/mods mudou.
