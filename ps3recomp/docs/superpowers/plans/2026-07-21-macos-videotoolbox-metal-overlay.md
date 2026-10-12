# Mac VideoToolbox + Metal movie overlay (M0) — Implementation Plan

> **Estado 2026-07-21: ✅ GREEN in-boot** — 330 frames + EOS + áudio; bridge metal;
> fix main-thread. Commits: ps3 `9d93cf7` `e12fbed` `f48161d` `37d9bd3`; gow2 `297b22d` `0eb7049` `ea78ced`.
>
> **Stack:** plano **M0** do índice
> [`2026-07-21-00-macos-metal-stack-INDEX.md`](2026-07-21-00-macos-metal-stack-INDEX.md).
> Política Apple: `Claude.md` § «Port Apple / macOS».
>
> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** No macOS/arm64, com `PS3_MOVIE_HLE=1`, reproduzir o `.m2v` real do `movie_cache` (MPEG-2, ex. `SmLogo_v2.m2v`) via **VideoToolbox** (sem ffmpeg), apresentar frames em fullscreen no backend **Metal**, tocar o `.wav` irmão, e no fim do stream armar `g_movie_overlay_done=1` — paridade funcional do half-1 de `movie_hle.c` no Windows.

**Architecture:** Manter a divisão actual do ficheiro: half-1 = overlay host (hoje só Win/ffmpeg+D3D12); half-2 = `movie_io_*` portável. No Mac, implementar half-1 com (1) thread de decode VideoToolbox que produz RGBA/BGRA, (2) `rsx_metal_movie_frame` que copia para um staging thread-safe e o `metal_present` faz blit fullscreen, (3) áudio do `.wav` irmão via AudioQueue. **Não** reencode Theora/VP9; **não** forjar st620/+0x744; **não** substituir o caminho natural `cellVdec`/WAD — overlay é cosmético + produtor de EOS real.

**Tech Stack:** macOS VideoToolbox + CoreMedia + CoreVideo; Metal (`rsx_metal_backend.m`); AudioToolbox (AudioQueue); C11/ObjC `.m`; CMake Ninja `build-macos`; host `gow2-recomp/boot_gow2`.

## Global Constraints

- **Sem ffmpeg** no path Mac deste plano (nem `popen` para ffmpeg/ffprobe).
- **Sem reencode** do `.m2v` (não Theora/VP9); ficheiro do `PS3_MOVIE_CACHE` intacto.
- **Nunca** commitar `.m2v` / `.wav` / `movie_cache/` / EBOOT (copyright).
- Overlay gated por **`PS3_MOVIE_HLE=1`** (OFF por default no comportamento actual do half-1; `env_gow2.sh` já pode ter `PS3_MOVIE_HLE=1` — documentar).
- **Não** forjar `st620` / `obj+0x744`; EOS só via `g_movie_overlay_done` (e o sampler já o consome).
- Windows **não regredir**: half-1 Win permanece `#ifdef _WIN32`; CMake Windows não linka VT/Metal movie.
- Commits em **português**, paths explícitos, sem co-autoria.
- Push só fork pessoal (`andrebrumdev` / remote `personal`), nunca `sp00nznet`.
- Smoke Mac: matar por **PID** (`TERM` → `-9`), nunca `pkill -f boot_gow2`.
- Repo de runtime: `ps3recomp`; smoke/run: `gow2-recomp` (irmão).

---

## Contexto medido (não reinventar)

| Facto | Evidência |
|-------|-----------|
| `SmLogo_v2.m2v` = MPEG-2 sequence, ~30 fps | `file movie_cache/SmLogo_v2.m2v`; head `00 00 01 b3` |
| Win overlay: ffmpeg → RGBA → `rsx_d3d12_movie_frame` | `movie_hle.c:58–216`, `rsx_d3d12_backend.c:893–908` |
| POSIX half-1 = stubs no-op | `movie_hle.c:277–288` |
| `movie_io_open` chama `movie_hle_from_guest(host)` no open `.m2v` | `movie_hle.c:559–567` (path já host-resolvido) |
| Metal present existe; sem overlay de movie | `rsx_metal_backend.m:141–184` (clear + present) |
| Sampler Mac já usa `movie_hle_overlay_done()` | `gow2-recomp/movie_eos_arm.c` (`done_overlay ? … : timebased`) |
| Janela 1280×720 já abre com `PS3_RSX_BACKEND=sdl` ou `metal` | boot medido 2026-07-21 |

**Fora de escopo deste plano:** FORCE SEQDONE, WAD `R_*`, registry de shaders, pixels do pipeline natural guest. (Plano irmão: `2026-07-21-intro-vdec-open-force-wad.md`.)

---

## Mapa de ficheiros

| Ficheiro | Papel |
|----------|--------|
| `ps3recomp/libs/video/movie_hle.h` | API pública estável (`movie_hle_*`, `movie_hle_overlay_done`) |
| `ps3recomp/libs/video/movie_hle.c` | Half-2 inalterado; half-1 Mac deixa de ser stub |
| `ps3recomp/libs/video/movie_vt_metal.m` | **NOVO** — decode VT + thread + wav AudioQueue (só Apple) |
| `ps3recomp/libs/video/movie_vt_metal.h` | **NOVO** — C API chamada por `movie_hle.c` em `__APPLE__` |
| `ps3recomp/libs/video/rsx_metal_backend.h` | Declarar `rsx_metal_movie_frame/stop/playback_begin` |
| `ps3recomp/libs/video/rsx_metal_backend.m` | Staging + blit fullscreen no `metal_present` |
| `ps3recomp/CMakeLists.txt` | Compilar `.m` + link `-framework VideoToolbox -framework CoreMedia -framework CoreVideo -framework AudioToolbox` (e Metal já) |
| `gow2-recomp/build_macos.sh` | Se linkar objectos à parte, incluir `movie_vt_metal.o` + frameworks |
| `gow2-recomp/env_gow2.sh` | Comentário + default documentado de `PS3_MOVIE_HLE` |
| `gow2-recomp/smoke_movie_overlay_mac.sh` | **NOVO** — smoke PID-kill com asserts de log |
| `gow2-recomp/notes/2026-07-21-macos-vt-metal-overlay.md` | **NOVO** — evidência in-boot |
| `ps3recomp/tests/` ou `gow2-recomp/tests/test_movie_vt_policy.c` | Testes offline de política (path/ext/env), sem VT |

---

## Interface canónica (lock no início)

```c
/* movie_vt_metal.h — só __APPLE__; C linkage */
#ifdef __cplusplus
extern "C" {
#endif

/* Arranca thread de decode+áudio se path .m2v legível. loop=0 single pass (intro).
 * No-op se path NULL/inválido. Idempotente: para o anterior se já a correr. */
void movie_vt_start(const char* host_m2v_path, int loop);

/* Pede paragem cooperativa; join da thread (timeout documentado no .m). */
void movie_vt_stop(void);

/* 1 se a última passagem single-pass chegou ao fim natural do stream. */
long movie_vt_overlay_done(void);

#ifdef __cplusplus
}
#endif
```

```c
/* rsx_metal_backend.h — espelho do contrato D3D12 */
void rsx_metal_movie_frame(const void* rgba, unsigned w, unsigned h); /* RGBA8 ou BGRA8 documentado */
void rsx_metal_movie_stop(void);
void rsx_metal_movie_playback_begin(void);
```

**Contrato de pixel (actualizado pós-spike 297b22d):** **BGRA8888** packed, row-major, top-down — `kCVPixelFormatType_32BGRA` do AVAssetReader e `MTLPixelFormatBGRA8Unorm` da layer. **Não** swizzle para RGBA no Mac (custo zero). O pipe ffmpeg Win continua RGBA no D3D12; os backends divergem no byte order, o contrato `rsx_*_movie_frame` é por plataforma.

**Contrato de thread:** `rsx_metal_movie_frame` pode ser chamado de thread secundária; `metal_present` corre na thread do pump. Staging protegido por `pthread_mutex` (ou `os_unfair_lock`).

---

## Estratégia de decode MPEG-2 ES (`.m2v`)

O ficheiro é **elementary stream** MPEG-2 (não MP4). Ordem de tentativas no implementer:

| Prioridade | Abordagem | Aceite se |
|------------|-----------|-----------|
| **A (preferida MVP)** | `AVAssetReader` / `AVURLAsset` no path `file://…/SmLogo_v2.m2v` | obtém frames até EOS |
| **B** | Remux **leve em memória/temp** ES→MPEG-PS (pack headers mínimos) + VT/AVAsset | A falhou; B produz frames |
| **C** | `VTDecompressionSession` + parse de start codes / access units | A+B falharam |

**Gate de go/no-go Task 2:** se A e B falharem no SmLogo real, **STOP e documentar** — não inventar Theora. Pivot permitido: documentar gap + manter time-based EOS (já existe).

---

### Task 0: Spike de 30 min — VT/AVAsset engole o SmLogo?

**Files:**
- Create (local, **não commit**: dados): `/tmp/vt_spike/` (script de teste)
- Read: `gow2-recomp/movie_cache/SmLogo_v2.m2v` (só leitura; não copiar para git)

**Interfaces:**
- Produz: veredicto `SPIKE=A|B|C|FAIL` escrito na note da Task 7

- [ ] **Step 1: Confirmar asset no cache (sem o commitar)**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
ls -la movie_cache/SmLogo_v2.m2v movie_cache/SmLogo_v2.wav
file movie_cache/SmLogo_v2.m2v
# Esperado: MPEG sequence / MPEG-2
```

- [ ] **Step 2: Spike mínimo AVFoundation (Swift ou ObjC one-shot)**

Criar `/tmp/vt_spike/probe.m` (fora do repo) que:

1. Abre `AVURLAsset` com o path absoluto do `.m2v`
2. Cria `AVAssetReader` + output `kCVPixelFormatType_32BGRA`
3. Conta frames até 60 ou EOS
4. Imprime `frames=N duration≈…`

```bash
clang -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework Foundation \
  -o /tmp/vt_spike/probe /tmp/vt_spike/probe.m
/tmp/vt_spike/probe "$(pwd)/movie_cache/SmLogo_v2.m2v"
# GREEN: frames>=30
# RED frames=0: tentar remux ES→PS com script python mínimo para /tmp/smlogo.mpg e re-probe
```

- [ ] **Step 3: Registar veredicto**

Escrever em `gow2-recomp/notes/2026-07-21-macos-vt-metal-overlay.md` secção “Spike”:

```
SPIKE=A|B|C|FAIL
frames=N
notes=...
```

Se `FAIL`: **não continuar Tasks 3–6 de decode**; só documentar e fechar plano como bloqueado (time-based continua).

- [ ] **Step 4: Commit só a note de spike (se FAIL ou A/B/C)**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
# criar note mínima se ainda não existir
git add notes/2026-07-21-macos-vt-metal-overlay.md
git commit -m "diag(movie): spike VideoToolbox/AVAsset no SmLogo_v2.m2v"
```

---

### Task 1: Contrato Metal — `rsx_metal_movie_frame` (staging + blit)

**Files:**
- Modify: `ps3recomp/libs/video/rsx_metal_backend.h`
- Modify: `ps3recomp/libs/video/rsx_metal_backend.m` (`metal_present` ~141–184, state struct ~topo)
- Test: offline impossível sem GPU; smoke visual na Task 6

**Interfaces:**
- Consome: RGBA8888 de `movie_vt_metal.m`
- Produz: overlay fullscreen no present; thread-safe

- [ ] **Step 1: Declarar API no header**

Em `rsx_metal_backend.h`, após as declarações de init/shutdown/pump:

```c
/* Host movie overlay (paridade rsx_d3d12_movie_*). RGBA8 packed, top-down. */
void rsx_metal_movie_frame(const void* rgba, unsigned w, unsigned h);
void rsx_metal_movie_stop(void);
void rsx_metal_movie_playback_begin(void);
```

- [ ] **Step 2: Staging + mutex no `.m`**

Espelhar a ideia de `MovieOverlay` em `rsx_d3d12_backend.c:890–908`:

```objc
typedef struct {
    pthread_mutex_t mu;
    uint8_t* staging;   /* RGBA */
    unsigned w, h;
    int pending;        /* 1 = frame novo desde o último present */
    int active;         /* 1 = overlay movie ligado */
} MetalMovieOverlay;
static MetalMovieOverlay s_movie;
```

Implementar:

- `rsx_metal_movie_playback_begin` — `active=1`, log `[movie-metal] playback begin`
- `rsx_metal_movie_frame` — lock, realloc se size mudar, `memcpy`, `pending=1`, `active=1`
- `rsx_metal_movie_stop` — lock, `active=0`, free staging, log stop

- [ ] **Step 3: Blit no `metal_present`**

Quando `s_movie.active && staging`:

1. Criar/actualizar `MTLTexture` `BGRA8Unorm` ou `RGBA8Unorm` (escolher um e converter se preciso; preferir **BGRA8Unorm** nativo Metal e swizzle no upload se o pipeline for RGBA).
2. `replaceRegion` com o staging.
3. Draw fullscreen triangle/quad com fragment shader sample texture (MVP: full-screen blit; se ainda não houver PSO genérico, adicionar um PSO mínimo **só** para overlay — não bloquear em “TODO draw_arrays”).
4. Se `!active`, manter clear color actual.

**Aceite offline de código:** greps:

```bash
rg -n "rsx_metal_movie_frame|s_movie\.staging" ps3recomp/libs/video/rsx_metal_backend.m
```

- [ ] **Step 4: Build Mac**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
# Esperado: 0 erros no rsx_metal_backend.m
```

- [ ] **Step 5: Commit**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add libs/video/rsx_metal_backend.h libs/video/rsx_metal_backend.m
git commit -m "feat(metal): overlay de movie (frame/stop/begin) para host HLE"
```

---

### Task 2: Módulo `movie_vt_metal` — decode + thread + EOS

**Files:**
- Create: `ps3recomp/libs/video/movie_vt_metal.h`
- Create: `ps3recomp/libs/video/movie_vt_metal.m`
- Modify: `ps3recomp/CMakeLists.txt` (fonte + frameworks Apple)
- Verify: `gow2-recomp/build_macos.sh` se linkar objectos extra

**Interfaces:**
- Consome: path host `.m2v`; `rsx_metal_movie_*`
- Produz: `movie_vt_start/stop/overlay_done`; frames no Metal; `done=1` no EOS single-pass

- [ ] **Step 1: Esqueleto C API + gate env**

```objc
/* movie_vt_metal.m */
static volatile int s_running;
static volatile long s_overlay_done;
static pthread_t s_thr;
static char s_path[1024];
static int s_loop;

long movie_vt_overlay_done(void) { return s_overlay_done; }

static int hle_on(void) {
    const char* e = getenv("PS3_MOVIE_HLE");
    return (e && e[0] && e[0] != '0');
}
```

- [ ] **Step 2: Implementar decode conforme SPIKE Task 0**

- Se `SPIKE=A`: AVAssetReader loop → CVPixelBuffer → copia RGBA → `rsx_metal_movie_frame`.
- Se `SPIKE=B`: remux para `/tmp/ps3recomp_movie_XXXX.mpg` (ou buffer) e depois A.
- Pacing: `usleep` / `nanosleep` para ~33 ms se timestamps falharem (SmLogo ~30 fps).
- Single-pass (`loop=0`): no fim, `s_overlay_done = 1`, log:

```
[movie-vt] EOS flag set: g_movie_overlay_done path via movie_vt_overlay_done=1
```

- [ ] **Step 3: Áudio `.wav` irmão**

```c
/* path.m2v -> path.wav; se existir, AudioQueue ou AVAudioPlayer async */
static void movie_vt_play_wav(const char* m2v_path);
static void movie_vt_stop_wav(void);
```

Não bloquear a thread de vídeo no áudio. Se wav em falta: log uma vez, continuar vídeo.

- [ ] **Step 4: `movie_vt_start` / `stop`**

```c
void movie_vt_start(const char* host_m2v_path, int loop) {
    if (!hle_on() || !host_m2v_path) return;
    movie_vt_stop(); /* join anterior */
    s_overlay_done = 0; /* NOTA: Win NÃO reset no re-open; intro Mac single-pass: reset OK se documentado.
                         * Paridade Win: NÃO resetar se o guest reabrir o mesmo logo em loop.
                         * Decisão MVP: NÃO resetar se s_overlay_done já 1 (sticky), igual Win comment. */
    if (s_overlay_done) return; /* sticky done — ver movie_hle.c Win */
    /* na verdade Win não reseta e permite re-start; copiar comentário Win:
     * "g_movie_overlay_done is intentionally NOT reset" — então NÃO zerar aqui. */
    strncpy(s_path, host_m2v_path, sizeof s_path - 1);
    s_loop = loop;
    s_running = 1;
    rsx_metal_movie_playback_begin();
    pthread_create(&s_thr, NULL, movie_vt_thread, NULL);
}

void movie_vt_stop(void) {
    s_running = 0;
    if (s_thr) { pthread_join(s_thr, NULL); s_thr = 0; }
    movie_vt_stop_wav();
    rsx_metal_movie_stop();
}
```

**Sticky done (paridade Win, `movie_hle.c:229–230`):** não limpar `s_overlay_done` no re-open do mesmo logo.

- [ ] **Step 5: CMake Apple frameworks**

```cmake
if(APPLE)
  list(APPEND HLE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/libs/video/movie_vt_metal.m")
  # onde a lib for linkada:
  target_link_libraries(... "-framework VideoToolbox" "-framework CoreMedia"
                            "-framework CoreVideo" "-framework AVFoundation"
                            "-framework AudioToolbox" "-framework Metal"
                            "-framework QuartzCore" "-framework Foundation")
endif()
```

Reconfigurar se preciso:

```bash
cmake -B build-macos -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
```

- [ ] **Step 6: Commit**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add libs/video/movie_vt_metal.h libs/video/movie_vt_metal.m CMakeLists.txt
git commit -m "feat(macos): decode VideoToolbox do .m2v + thread de overlay"
```

---

### Task 3: Ligar half-1 POSIX em `movie_hle.c` aos stubs Mac

**Files:**
- Modify: `ps3recomp/libs/video/movie_hle.c` (bloco `#else /* !_WIN32 */` ~277–288)
- Read: `movie_hle.c:251–270` (Win `from_guest` — espelhar resolução de path)

**Interfaces:**
- Consome: `movie_vt_*`, `movie_io` já chama `movie_hle_from_guest`
- Produz: `movie_hle_overlay_done()` real no Mac

- [ ] **Step 1: Substituir stubs Apple**

```c
#elif defined(__APPLE__)

#include "movie_vt_metal.h"

long movie_hle_overlay_done(void) { return movie_vt_overlay_done(); }

void movie_hle_start(const char* host_path) {
    movie_vt_start(host_path, /*loop=*/1);
}
void movie_hle_start_once(const char* host_path) {
    movie_vt_start(host_path, /*loop=*/0);
}
void movie_hle_stop(void) { movie_vt_stop(); }

void movie_hle_from_guest(const char* path_or_guest) {
    /* movie_io_open passa path HOST já resolvido (.../SmLogo_v2.m2v).
     * Aceitar também guest "/_movies/X.m2v" como no Win. */
    if (!path_or_guest) return;
    size_t L = strlen(path_or_guest);
    if (L < 4 || strcmp(path_or_guest + L - 4, ".m2v") != 0) return;

    char host[1024];
    if (path_or_guest[0] == '/' && strstr(path_or_guest, "movie_cache")) {
        /* already host-ish; still prefer basename+cache for consistency */
    }
    const char* base = strrchr(path_or_guest, '/');
    base = base ? base + 1 : path_or_guest;
    const char* cache = getenv("PS3_MOVIE_CACHE");
    if (!cache || !*cache) cache = "movie_cache";
    snprintf(host, sizeof host, "%s/%s", cache, base);

    /* Se path_or_guest já for ficheiro legível, usar directo */
    struct stat st;
    if (stat(path_or_guest, &st) == 0 && S_ISREG(st.st_mode)) {
        strncpy(host, path_or_guest, sizeof host - 1);
        host[sizeof host - 1] = 0;
    } else if (stat(host, &st) != 0) {
        fprintf(stderr, "[movie] guest/host '%s' sem cache '%s'\n", path_or_guest, host);
        return;
    }
    strncpy(s_movie_cache_path, host, sizeof s_movie_cache_path - 1);
    fprintf(stderr, "[movie] guest movie -> host VT overlay '%s'\n", host);
    fflush(stderr);
    movie_vt_start(host, /*loop=*/0);  /* intro: single pass */
}

#else  /* outros POSIX: stubs como hoje */
void movie_hle_start(const char* host_path) { (void)host_path; }
/* ... stubs ... */
long movie_hle_overlay_done(void) { return 0; }
#endif
```

Actualizar o comentário do topo do ficheiro (linhas 7–15) para mencionar Mac VT+Metal.

- [ ] **Step 2: Garantir que `s_movie_cache_path` continua partilhado**

Já escrito em `movie_io_open` e em `from_guest` — não duplicar lógica de open.

- [ ] **Step 3: Build + link `boot_gow2`**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
cd ../gow2-recomp && ./build_macos.sh
# Esperado: link OK; símbolos movie_vt_* resolvidos
nm boot_gow2 | rg "movie_vt_|movie_hle_overlay_done|rsx_metal_movie" | head
```

- [ ] **Step 4: Commit**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add libs/video/movie_hle.c
git commit -m "feat(macos): liga movie_hle overlay a VideoToolbox+Metal"
```

---

### Task 4: Política offline + não-regressão Windows (teste)

**Files:**
- Create: `gow2-recomp/tests/test_movie_hle_path_policy.c` (ou em ps3recomp se preferir suite CMake)
- Optional: stub fraco se linkar só política pura

**Interfaces:**
- Testa: só regras puras de path/ext (`.m2v` sim, `.wav` não; env off)

Como VT não roda em unit sem GPU, extrair se necessário:

```c
/* Se extrair helper para teste: */
int movie_hle_should_start(int env_on, const char* path); /* 1 se .m2v e env */
```

- [ ] **Step 1: Testes**

```c
CHECK(movie_hle_should_start(0, "x.m2v") == 0, "env off");
CHECK(movie_hle_should_start(1, "x.wav") == 0, "nao e m2v");
CHECK(movie_hle_should_start(1, "SmLogo_v2.m2v") == 1, "m2v + env");
CHECK(movie_hle_should_start(1, NULL) == 0, "null");
```

- [ ] **Step 2: Correr**

```bash
cc -std=c11 -Wall -Wextra -O0 -o /tmp/test_movie_hle_path \
  tests/test_movie_hle_path_policy.c  # + objecto mínimo se aplicável
/tmp/test_movie_hle_path
# PASS
```

- [ ] **Step 3: Commit**

```bash
git add tests/test_movie_hle_path_policy.c
git commit -m "test(movie): politica de path/env do overlay HLE"
```

---

### Task 5: Smoke Mac in-boot (janela + logs)

**Files:**
- Create: `gow2-recomp/smoke_movie_overlay_mac.sh`
- Modify: `gow2-recomp/env_gow2.sh` (comentário; não forçar HLE se smoke o exportar)

**Interfaces:**
- Consome: `boot_gow2` com Metal/SDL + HLE
- Produz: exit 0 se asserts; log em `/tmp/movie_vt_smoke.log`

- [ ] **Step 1: Script**

```bash
#!/usr/bin/env bash
# smoke_movie_overlay_mac.sh — overlay VT+Metal (nao forja FSM)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"
. ./env_gow2.sh
unset PS3_NO_RSX || true
export PS3_RSX_BACKEND="${PS3_RSX_BACKEND:-metal}"   # ou sdl se metal falhar no host CI
export PS3_MOVIE_HLE=1
export PS3_MOVIE_IO=1
export PS3_MOVIE_EOS=1
export PS3_MOVIE_DONE_MS=auto   # fallback se overlay falhar
export PS3_PERF_FSM=1
export PS3_VDEC_ASYNC=1
unset PS3_VDEC_FORCE_SEQDONE_MS || true

LOG=/tmp/movie_vt_smoke.log
./boot_gow2 EBOOT.ELF >"$LOG" 2>&1 &
BPID=$!
sleep 45
kill -TERM "$BPID" 2>/dev/null || true
sleep 1
kill -9 "$BPID" 2>/dev/null || true
wait "$BPID" 2>/dev/null || true

echo "=== metrics ==="
grep -E '\[movie|movie-vt|movie-metal|RSX metal|RSX sdl|overlay_done|AUDDONE|Open|StartSeq' "$LOG" | head -40

# Asserts
grep -q '\[movie\].*VT overlay\|\[movie-vt\]' "$LOG" || { echo "FAIL: no VT start"; exit 1; }
# EOS real do overlay (preferido) OU time-based ainda funciona
if grep -q 'EOS flag set\|movie_vt.*done=1\|overlay_done=1' "$LOG"; then
  echo "GREEN: overlay done signal"
else
  echo "WARN: no overlay EOS in 45s — check decode; time-based may still fire"
fi
# Não exige Open (caminho B independente); regista se existir
echo "open=$(grep -c '\[cellVdec\] Open' "$LOG" || true)"
exit 0
```

- [ ] **Step 2: Correr smoke**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
chmod +x smoke_movie_overlay_mac.sh
./smoke_movie_overlay_mac.sh
```

**GREEN humano:** janela mostra o logo (não ecrã preto 45s); log com start VT + frames ou EOS.

- [ ] **Step 3: Commit script**

```bash
git add smoke_movie_overlay_mac.sh env_gow2.sh
git commit -m "test(macos): smoke do overlay VideoToolbox+Metal"
```

---

### Task 6: Integração com EOS arm (validar overlay_done → sampler)

**Files:**
- Read-only: `gow2-recomp/movie_eos_arm.c` (já usa `movie_hle_overlay_done`)
- Note: `gow2-recomp/notes/2026-07-21-macos-vt-metal-overlay.md`

**Interfaces:**
- Quando overlay completa **antes** do time-based, `done_overlay=1` e arm ainda exige `st>=5` (Task 2 A3b/EOS).

- [ ] **Step 1: Boot 60s com HLE+EOS**

```bash
(
  set -a; . ./env_gow2.sh; set +a
  export PS3_RSX_BACKEND=metal
  export PS3_MOVIE_HLE=1 PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto
  export PS3_PERF_FSM=1 PS3_VDEC_ASYNC=1
  unset PS3_NO_RSX
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/movie_vt_eos.log 2>&1 & BPID=$!
sleep 60
kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
grep -E 'movie-vt|overlay_done|MOVIEEOS|AUDDONE|Open|st620' /tmp/movie_vt_eos.log | head -50
```

**Esperado (ordem flexível):**

1. `[movie] … VT overlay` / `[movie-vt]` start  
2. frames a apresentar (visual)  
3. `overlay_done` / EOS flag **ou** time-based se vídeo > duração usada  
4. Se A3b activo: AUDDONE + Open possível (independente)  
5. `MOVIEEOS` **só** se `st620>=5` (não regredir gate Task 2)

- [ ] **Step 2: Escrever evidência na note**

Tabela métricas + screenshot opcional do utilizador (não commit de PNG de jogo se contiver arte copyright — preferir descrição).

- [ ] **Step 3: Commit note**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
git add notes/2026-07-21-macos-vt-metal-overlay.md
git commit -m "docs(movie): evidencia in-boot do overlay VT+Metal"
```

---

### Task 7: Documentação + INDEX + não-regressão

**Files:**
- Modify: `ps3recomp/libs/video/movie_hle.c` header comment (se ainda incompleto)
- Modify: `ps3recomp/docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md` (linha Mac intro / media)
- Modify: `gow2-recomp/CLAUDE.md` ou `ps3recomp/Claude.md` — 5 linhas “como ver o logo no Mac”
- Optional: `ps3recomp/docs/MACOS_PORT_PLAN.md` se existir secção media

- [ ] **Step 1: Secção “Como ver o logo”**

```bash
cd ~/Documents/PESSOAL/gow2-recomp
(
  set -a; . ./env_gow2.sh; set +a
  export PS3_RSX_BACKEND=metal   # ou sdl
  export PS3_MOVIE_HLE=1
  export PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto
  export PS3_PERF_FSM=1
  unset PS3_NO_RSX
  exec ./boot_gow2 EBOOT.ELF
)
```

- [ ] **Step 2: Checklist final**

- [ ] `libps3recomp_runtime` / `boot_gow2` builda arm64  
- [ ] Windows não toca nos ficheiros `.m` no CI Win (ou `#ifdef __APPLE__`)  
- [ ] Sem dados de jogo no git  
- [ ] Smoke PID-clean  
- [ ] `movie_hle_overlay_done` ≠ 0 após logo (ou FAIL documentado na spike)  
- [ ] Gate `st>=5` no arm EOS intacto  
- [ ] A3b / Open não regrediu (smoke opcional `PS3_NO_RSX=1` 30s: st max≥3)

- [ ] **Step 3: Commit docs**

```bash
git add docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md Claude.md
git commit -m "docs: overlay Mac VideoToolbox+Metal no INDEX e guia de run"
```

---

## Ordem de execução e dependências

```
Task 0 (spike) ──FAIL──► STOP + note
       │
       OK
       ▼
Task 1 (Metal frame API) ──► Task 2 (VT thread) ──► Task 3 (movie_hle wire)
                                      │
                                      ▼
                              Task 4 (unit path) ──► Task 5 (smoke) ──► Task 6 (EOS) ──► Task 7 (docs)
```

Tasks 1 e 0 podem ser **paralelas**. Task 2 depende do veredicto do 0.

---

## Critérios de aceite (plano completo)

| ID | Critério | Como provar |
|----|----------|-------------|
| A1 | Build Mac arm64 com VT+Metal | `cmake --build build-macos` + `./build_macos.sh` exit 0 |
| A2 | Sem ffmpeg no path Mac | greps no código Mac: zero `ffmpeg`/`ffprobe` em half-1 Apple |
| A3 | Overlay arranca no open `.m2v` | log `[movie] … VT overlay` / `[movie-vt]` com `PS3_MOVIE_HLE=1` |
| A4 | Pixels do logo na janela | observação humana 1280×720 (não ecrã preto estático 45s) |
| A5 | EOS do overlay | `movie_hle_overlay_done()!=0` / log EOS após single-pass |
| A6 | Wav opcional | sem crash se wav existe; log se falta |
| A7 | Não forja FSM | zero writes host a st620/+0x744 no diff |
| A8 | Arm EOS respeita st≥5 | se Open/A3b avançar, arm só pós-5; se não, overlay_done não arma cedo sozinho |
| A9 | Windows intacto | half-1 `#ifdef _WIN32` sem remoção; build Win não exige frameworks Apple |
| A10 | Smoke script | `smoke_movie_overlay_mac.sh` documentado e PID-clean |

---

## Riscos e mitigações

| Risco | Mitigação |
|-------|-----------|
| AVAsset recusa MPEG-ES nu | Task 0 + remux B; se FAIL, não fingir sucesso |
| Thread Metal crash | mutex + upload só no present; não reter `id<>` cross-thread sem retain |
| Overlay esconde legal/content hold | espelhar `playback_begin` pause do D3D12 |
| Duplo produtor EOS (time + overlay) | ambos sticky OR no sampler — ok; arm ainda gated st≥5 |
| Performance CPU swizzle YUV→RGBA | MVP ok; v2 texture NV12 |
| Contaminação de binário partilhado | rebuild explícito; smoke em árvore limpa |

---

## Commits esperados (resumo)

1. `diag(movie): spike VideoToolbox/AVAsset no SmLogo`  
2. `feat(metal): overlay de movie (frame/stop/begin)`  
3. `feat(macos): decode VideoToolbox do .m2v + thread`  
4. `feat(macos): liga movie_hle overlay a VT+Metal`  
5. `test(movie): politica de path/env do overlay HLE`  
6. `test(macos): smoke do overlay VT+Metal`  
7. `docs(movie): evidencia in-boot + INDEX`

---

## Self-review (cobertura)

| Requisito | Task |
|-----------|------|
| Sem ffmpeg | 0, 2, A2 |
| VideoToolbox + Metal | 1, 2 |
| `.m2v` real SmLogo | 0, 5, 6 |
| `.wav` irmão | 2 |
| `overlay_done` / sampler | 2, 3, 6 |
| Gated `PS3_MOVIE_HLE` | 2, 3, 5 |
| Não forjar FSM | Global + A7 |
| Windows safe | 3, A9 |
| Smoke + docs | 5, 6, 7 |
| Spike go/no-go | 0 |

Placeholders: nenhum TBD de implementação crítica; pivot FAIL só no spike documentado.

---

## Handoff de execução

**Plano completo em**  
`ps3recomp/docs/superpowers/plans/2026-07-21-macos-videotoolbox-metal-overlay.md`

**Duas formas de executar:**

1. **Subagent-Driven (recomendado)** — um subagente por task, review entre tasks (`superpowers:subagent-driven-development`)
2. **Inline** — esta sessão com checkpoints (`superpowers:executing-plans`)

**Qual preferes?**
