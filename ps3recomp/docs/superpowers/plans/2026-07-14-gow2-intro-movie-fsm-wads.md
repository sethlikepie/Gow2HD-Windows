# GoW2 intro/movie FSM → WAD load → pixels de jogo — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Destravar a FSM do player de intro (SmLogo → EOS → pós-logo) de forma **reproduzível**, fazer os WADs (`R_LglScA`, `R_PermA`, …) carregarem e **descomprimirem**, e atravessar a parede `Invalid shader combination` o suficiente para o pipeline RSX (já publicado) ver shaders/texturas reais do jogo na tela.

**Architecture:** O caminho “honesto” do jogo é FIOS/psarc → movie I/O → cellVdec/SPU MPEG → byte EOS no objeto do player (`obj+0x744`) → estados `st620` 1→3→5→11→done → open de WADs. O runtime já tem **atalhos HLE** (`PS3_MOVIE_IO`, `PS3_MOVIE_EOS`, `PS3_VDEC_FORCE_SEQDONE_MS`, overlay ffmpeg). Empiricamente (`longskip.log` / `skip.log`), o atalho **já abre** `R_LglScA` + `R_PermA` após FORCE SEQDONE; o boot atual **não usa** esse recipe no launcher default e, pós-WAD, morre em **spu1 MISS** (dearch/EDGE-zlib) + **CGOWShader CRC/combination**.

**Tech Stack:** `boot_v2_new.exe` (gow2_work), `movie_hle.c`, `cellVdec.c`, `boot_main.cpp` (trace MOVIEOBJ/EOS), `ppu_loader.cpp` (`g_movie_eos_ea`, `tblsize_guard`), `gow2_spu_register.c` (spu1), env vars de bring-up, cache `gow2_work/movie_cache/`.

## Global Constraints

- Dois repos: **ps3recomp** (runtime/HLE) e **gow2_work** / gow2-recomp (register, launcher, logs). Commits separados.
- Não regredir o loop de flip estável (~40fps / 263 flips/100s baseline sem SPU_ALL).
- Preferir **gates env** para force-paths; só promover a default após smoke.
- Dados de jogo (`extracted/`, `spu_images/`, `EBOOT.ELF`) nunca no git; `movie_cache/*.wad_ps3` e `.m2v` são locais.
- Plano anterior (RSX FP/VP/tex + SPU SEH) está **publicado** — este plano **não** reimplementa o backend; só o alimenta com conteúdo real.

## Estado empírico (2026-07-14)

### O que já funciona (com recipe de skip)

Env usado em `bt_skip.sh` / `run_long_skip.sh`:

```text
PS3_VDEC_FORCE_SEQDONE_MS=8000
PS3_MOVIE_IO=1
PS3_MOVIE_CACHE="../movie_cache"
PS3_MOVIE_EOS=1
PS3_VFS_ROOT=.../extracted/USRDIR
PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
```

Timeline observada (`longskip.log` / `skip.log`):

| T | Evento |
|---|--------|
| ~boot | FIOS + spu0 HIT; **spu1 MISS** já no early dearch |
| ~t+ | `[movieio] open 'SmLogo_v2' → …/SmLogo_v2.m2v` |
| | `[MOVIEEOS] func_0045B2A8 -> 1 (state-3 gate)` |
| | `cellVdec` StartSeq / reads de AU |
| 8s | `[cellVdec] FORCE SEQDONE after 8000 ms` |
| pós | close m2v; **`open 'R_LglScA' → R_LglScA.wad_ps3`**; **`open 'R_PermA' → R_PermA.wad_ps3`** |
| stuck | spam `Invalid shader combination: …/Default.ps3fx` + `dispatch MISS fp=0x2A5C4E67A14505B8` (spu1) a cada flip |

### O que o launcher default **não** faz

`gow2_work/recomp_mid_v2/rodar_gow2.cmd` só define:

```bat
PS3_MOVIE_HLE=1
PS3_NOMOVIES=1
PS3_PAD_AUTOSTART=1
PS3_RSX_BACKEND=d3d12
…
```

Sem `PS3_MOVIE_IO` / `PS3_MOVIE_EOS` / `FORCE_SEQDONE` → a FSM **não** chega nos WADs de forma confiável.

### Cadeia de paredes (ordem)

```
[A] SmLogo stuck (sem I/O/EOS/force)
        │
        ▼  recipe skip / EOS natural
[B] WADs open (R_LglScA, R_PermA)          ← JÁ OBSERVADO
        │
        ▼  spu1 registered + task ABI ok
[C] WAD contents inflated (dearchi)        ← MISS atual
        │
        ▼  CGOWShader combination/CRC ok
[D] Real .ps3fx → microcode → set_shader   ← Invalid shader spam
        │
        ▼  pipeline RSX publicado
[E] Textura/draw de jogo na tela
```

### Peças de código (âncoras)

| Peça | Path | Papel |
|------|------|--------|
| Movie I/O host | `ps3recomp/libs/video/movie_hle.c` | open/read/stat de `.m2v` e `.wad_ps3` no cache; dispara overlay |
| Overlay EOS flag | `movie_hle.c` → `g_movie_overlay_done` | fim natural do vídeo host |
| EOS read-hook | `ppu_loader.cpp` `g_movie_eos_ea` + `vm_read8` | força `[obj+0x744]==1` |
| Arm EOS | `runtime/ppu/tests/boot_main.cpp` (~339–346) | se `st620==1` e overlay done → arma EA |
| Vdec force | `cellVdec.c` `PS3_VDEC_FORCE_SEQDONE_MS` | SEQDONE artificial |
| Movie object | singleton `[0x540054]` → `obj`; `st620` em `obj+0x620` | FSM |
| State-3 gate log | guest hook `func_0045B2A8` → `[MOVIEEOS] … state-3 gate` | evidência de avanço |
| spu1 | `gow2_work/…/gow2_spu_register.c` fp `0x2A5C4E67A14505B8` | EDGE-zlib / dearch |
| Shader zero-guard | `ppu_loader.cpp` `tblsize_guard` inclui `0x53D6AC` | evita combination nula |
| Diag shader | `PS3_TRACE_SHADERMSG`, `PS3_TRACE_CRC`, `PS3_TRACE_SHADERSRC` | scripts `run_crc.sh`, `run_shadersrc.sh` |

---

## Mapa de arquivos a tocar

| Arquivo | Repo | Mudança típica |
|---------|------|----------------|
| `recomp_mid_v2/rodar_gow2.cmd` | gow2_work | Recipe skip + SPU1 + D3D12 |
| `recomp_mid_v2/bt_intro_wads.sh` (novo) | gow2_work | Smoke único da FSM→WAD |
| `recomp_mid_v2/gow2_spu_register.c` | gow2_work | spu1 default ou `PS3_SPU1` no recipe |
| `libs/video/movie_hle.c` | ps3recomp | robustez open WAD; logs de progressão |
| `libs/codec/cellVdec.c` | ps3recomp | EOS natural vs force; stats |
| `runtime/ppu/tests/boot_main.cpp` | ps3recomp | arm EOS em mais estados; log st620 transitions |
| `runtime/ppu/ppu_loader.cpp` | ps3recomp | CRC/combination guards se necessário |
| `docs/gow2-recomp-notes.md` | ps3recomp | documentar paredes A–E |

---

### Task 1: Baseline reprodutível — recipe “intro skip → WAD open”

**Files:**
- Create: `gow2_work/recomp_mid_v2/bt_intro_wads.sh`
- Modify: `gow2_work/recomp_mid_v2/rodar_gow2.cmd` (opt-in section ou recipe documentado)
- Test: run 90–120s; greps de aceite

**Interfaces:**
- Consumes: env vars já implementadas (lista acima)
- Produces: script + critérios de exit code; log `intro_wads.log`

- [x] **Step 1: Congelar o recipe mínimo que já abriu WADs** — `bt_intro_wads.sh` criado. ACHADO: recipe histórico quebrara pelo revert `f811ee3` (vdec voltou a sync; FORCE SEQDONE só roda na thread async) → `PS3_VDEC_ASYNC=1` restaurado no recipe.

Criar `bt_intro_wads.sh` (Git Bash), baseado em `run_long_skip.sh` + greps de aceite:

```bash
#!/usr/bin/env bash
set +e
cd "$(dirname "$0")"
EXE=./boot_v2_new.exe
LOG=intro_wads.log
taskkill //F //IM boot_v2_new.exe >/dev/null 2>&1; sleep 1
rm -f "$LOG"

export PS3_VFS_ROOT="/c/Users/softlive/Documents/self-projects/gow2_work/extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1
export PS3_PAD_AUTOSTART=1
# spu1 off no baseline Task 1 (mede WAD open only)
unset PS3_SPU1 PS3_SPU_ALL

timeout -k 5 120 "$EXE" ../EBOOT.ELF >"$LOG" 2>&1
EC=$?

echo "exit=$EC (124=timeout esperado)"
echo "force=$(grep -ac 'FORCE SEQDONE' "$LOG")"
echo "eos_gate=$(grep -ac 'state-3 gate' "$LOG")"
echo "r_lgl=$(grep -ac "open 'R_LglScA'" "$LOG")"
echo "r_perm=$(grep -ac "open 'R_PermA'" "$LOG")"
echo "invalid=$(grep -ac 'Invalid shader combination' "$LOG")"
echo "spu1_miss=$(grep -ac 'fp=0x2A5C4E67A14505B8' "$LOG")"
echo "flips=$(grep -ac SetFlipCommand "$LOG")"

# Aceite Task 1: WADs abrem; não exige pixels
fail=0
[ "$(grep -ac 'FORCE SEQDONE' "$LOG")" -ge 1 ] || { echo "FAIL no FORCE"; fail=1; }
[ "$(grep -ac "open 'R_LglScA'" "$LOG")" -ge 1 ] || { echo "FAIL no R_LglScA"; fail=1; }
[ "$(grep -ac "open 'R_PermA'" "$LOG")" -ge 1 ] || { echo "FAIL no R_PermA"; fail=1; }
[ "$(grep -ac CRASH "$LOG")" -eq 0 ] || { echo "FAIL crash marker"; fail=1; }
exit $fail
```

- [x] **Step 2: Rodar e capturar baseline** — force=1 r_lgl=1 r_perm=1 invalid=343 spu1_miss=330 flips=328, exit 0, reproduzido 2x.

```bash
bash bt_intro_wads.sh
```

Expected:
- `force>=1`, `r_lgl>=1`, `r_perm>=1`, exit 0 do script
- `invalid` alto (OK nesta task)
- `spu1_miss` alto (OK nesta task)

Se R_PermA falhar por case (`r_perma.wad_ps3` no disk): Windows costuma ser case-insensitive; se portar a Linux, normalizar `movie_io_open` para try lowercase.

- [x] **Step 3: Atualizar `rodar_gow2.cmd` com bloco documentado** — bloco rem + `rodar_gow2_intro_skip.cmd` criado. NOTA (controller): no combo visual (MOVIE_HLE+D3D12) a FSM para em st620=11 e WADs não abrem ainda — Task 2 ataca (arm EOS em st620=11 + desacoplar do trace). Combo NÃO crasha (f811ee3 não reproduz).

Não forçar FORCE no double-click sem opção — adicionar comentário + vars alinhadas ao smoke:

```bat
rem --- Intro FSM bring-up (descomente para pular logo e abrir WADs) ---
rem set PS3_MOVIE_IO=1
rem set PS3_MOVIE_CACHE=../movie_cache
rem set PS3_MOVIE_EOS=1
rem set PS3_VDEC_FORCE_SEQDONE_MS=8000
rem set PS3_SPU1=1
```

E um `rodar_gow2_intro_skip.cmd` que já vem com o recipe ligado (UX de bring-up).

- [x] **Step 4: Commit (gow2_work)** — `c061dab` (3 arquivos, revisão direta do controller).

```bash
git add recomp_mid_v2/bt_intro_wads.sh recomp_mid_v2/rodar_gow2.cmd recomp_mid_v2/rodar_gow2_intro_skip.cmd
git commit -m "chore(boot): reproducible intro-skip recipe that opens R_PermA WADs"
```

---

### Task 2: Observabilidade da FSM (st620 transitions)

**Files:**
- Modify: `ps3recomp/runtime/ppu/tests/boot_main.cpp` (thread `PS3_TRACE_MOVIEOBJ`)
- Optional: guest hook log em build gow2 se `func_0045B2A8` já instrumentado

**Interfaces:**
- Consumes: `obj` em `[0x540054]`, campos `+0x620`, `+0x744`, `+0x71C`, `+0x714`
- Produces: linhas `[MOVIEFSM] st620: N -> M` só em mudança; arm EOS log existente

- [x] **Step 1: Log edge-triggered de estado** — `[MOVIEFSM] st620 N -> M` só em mudança (boot_main.cpp, commit 2dabeeb).

No loop MOVIEOBJ (~a cada 200ms), guardar `prev_st` e:

```cpp
uint32_t st = MO32(0x620);
static uint32_t prev = 0xFFFFFFFFu;
if (st != prev) {
    fprintf(stderr, "[MOVIEFSM] st620 %u -> %u f744=%u eos_ea=0x%08X overlay_done=%ld\n",
            prev == 0xFFFFFFFFu ? 0 : prev, st, p[0x744], g_movie_eos_ea,
            (long)movie_hle_overlay_done());
    fflush(stderr);
    prev = st;
}
```

- [x] **Step 2: Armar EOS também se st620 já saiu de 1 cedo demais** — evidência exigiu mais que 1||3: FSM estaciona em st620=11; arm agora dispara em qualquer estado (one-shot preservado, loga o estado). BÔNUS: arm desacoplado do gate PS3_TRACE_MOVIEOBJ (antes o EOS era refém do trace — bug escondido).

Hoje só arma se `MO32(0x620)==1`. Se o gate state-3 dispara antes do overlay done, documentar e opcionalmente:

```cpp
if (eos_on && !g_movie_eos_ea && movie_hle_overlay_done()
    && (MO32(0x620) == 1 || MO32(0x620) == 3)) {
    g_movie_eos_ea = obj + 0x744;
    …
}
```

- [x] **Step 3: Smoke com `PS3_TRACE_MOVIEOBJ=1`** — verde; e sem trace também (MOVIEOBJ=0, MOVIEFSM presente, arm funciona). Combo visual: FSM 11→0, WADs abrem via EOS.

```bash
PS3_TRACE_MOVIEOBJ=1 bash bt_intro_wads.sh
# ou timeout 60 com o mesmo env
grep MOVIEFSM intro_wads.log
```

Expected: sequência contendo `1` e depois valores >1 antes/depois de FORCE; WAD opens após transição.

- [x] **Step 4: Commit (ps3recomp)** — `2dabeeb` (+ smoke `bt_visual_combo.sh` no gow2_work `9033476`). Review: Spec ✅ sem Critical. Rastreado: read-hook nunca desarma (pré-existente) — validar FORCE+EOS simultâneos em T4/T6.

```bash
git add runtime/ppu/tests/boot_main.cpp
git commit -m "feat(boot): edge-triggered movie FSM state logs (st620)"
```

---

### Task 3: Caminho natural de EOS (sem FORCE) — opcional mas alvo de fidelidade

**Files:**
- Modify: `libs/video/movie_hle.c` (overlay once; sticky `g_movie_overlay_done`)
- Modify: `libs/codec/cellVdec.c` (drain real → SEQDONE)
- Modify: `boot_main.cpp` (arm EOS)

**Interfaces:**
- Consumes: `PS3_MOVIE_HLE=1` + `PS3_MOVIE_IO=1` + `PS3_MOVIE_EOS=1`, **sem** `PS3_VDEC_FORCE_SEQDONE_MS`
- Produces: `g_movie_overlay_done=1` → hook `[obj+0x744]` → SEQDONE real ou aceito → mesmo open de WADs

- [x] **Step 1: Medir por que o natural não chega** — MEDIDO E RESOLVIDO (natural_eos_test.log, 120s SEM FORCE): a causa era o arm exigir st620==1 + estar refém do trace (fixado na Task 2). Timeline natural: HLE playback → 330 frames → EOS flag → arm(st620=11) → FSM 11→0 → R_LglScA + R_PermA abrem. force=0, crash=0.

Run 180s **sem** FORCE:

```bash
unset PS3_VDEC_FORCE_SEQDONE_MS
export PS3_MOVIE_HLE=1 PS3_MOVIE_IO=1 PS3_MOVIE_EOS=1 PS3_MOVIE_CACHE=../movie_cache
export PS3_VDEC_STATS=1 PS3_TRACE_MOVIEOBJ=1
# … mesmo VFS/VM …
timeout 180 ./boot_v2_new.exe ../EBOOT.ELF > natural_eos.log 2>&1
```

Coletar:

| Métrica | Interpretação |
|---------|----------------|
| `g_movie_overlay_done` / `[movie] EOS flag set` | overlay terminou? |
| `[MOVIEEOS] overlay done -> arming` | armou hook? |
| `[MOVIEEOS] read-hook HIT` | player leu +0x744? |
| `VDECSTATS` decode/picout/getpic | player drena frames? |
| `st620` stuck em 1? | FSM parada |
| `open 'R_PermA'` | sucesso natural |

- [x] **Step 2: Hipóteses e patches mínimos (só as que o log confirmar)** — patch necessário foi só o da Task 2 (arm ampliado+desacoplado). Read-hook em [obj+0x744] BASTA (não precisou write direto).

| Hipótese | Fix se confirmada |
|----------|-------------------|
| Overlay não termina (loop=1) | `movie_hle_start_mode(..., 0)` já no path m2v; garantir `PS3_MOVIE_HLE=1` no recipe |
| Player não lê +0x744 | forçar write BE `vm_base[obj+0x744]=1` one-shot além do read-hook |
| Vdec: decode>0 mas getpic=0 | async path já existe; checar giant lock / callback OPD |
| FIOS io+0x90 nunca done | `movie_io` já síncrono; log `iodone` do MOVIEOBJ |
| Re-open SmLogo reseta progresso | sticky overlay_done já documentado — não resetar |

- [x] **Step 3: Aceite dual-path** — Force: WAD open ≤30s ✅ (bt_intro_wads.sh). Natural: WAD open sem FORCE ✅ (11s de SmLogo + margem, dentro de 120s).

| Path | Aceite |
|------|--------|
| Force (Task 1) | WAD open em ≤30s (já true) |
| Natural (Task 3) | WAD open **sem** FORCE em ≤ duração real do SmLogo + 30s margem |

Se natural não fechar em 1–2 iterações, **não bloquear** Tasks 4–5: documentar “force is bring-up default; natural tracked”.

- [x] **Step 4: Commit** — coberto pelo `2dabeeb` da Task 2 (mesmos fixes); sem código adicional necessário. Evidência: natural_eos_test.log.

```bash
git commit -m "fix(movie): natural EOS path for intro FSM (or document residual)"
```

---

### Task 4: Pós-WAD — registrar spu1 (dearch / EDGE-zlib)

**Files:**
- Modify: `gow2_work/recomp_mid_v2/gow2_spu_register.c`
- Modify: `bt_intro_wads.sh` / `rodar_gow2_intro_skip.cmd` → `PS3_SPU1=1`
- Depends: SEH isolation já em `spu_workload.c` (plano anterior, publicado)

**Interfaces:**
- Fingerprint spu1: `0x2A5C4E67A14505B8` → `spu1_spu_func_00003050`
- Evidência: log `name='dearchi'` + MISS spu1 no early boot e no loop pós-WAD

- [x] **Step 1: Smoke com spu1 ligado no recipe intro** — `bt_intro_spu1.sh`: MISS 332→0, HIT=4, DMA real (352 GET/350 PUT: varredura de descritores 224B + push LFQ), retorna limpo, 0 SPUCRASH, host vivo.

```bash
export PS3_SPU1=1
# mesmo FORCE + MOVIE_IO recipe
timeout 120 ./boot_v2_new.exe ../EBOOT.ELF > intro_spu1.log 2>&1
grep -E 'spu1|2A5C4E67A14505B8|SPUJOB|SPUCRASH|dearch' intro_spu1.log | head -40
```

Expected (mínimo):
- `dispatch HIT … gow2_spu1` **ou** `fp=0x2A5C4E67…` com HIT
- host sobrevive (SEH se crash de job)
- ideal: menos MISS spu1 no pós-R_PermA

- [x] **Step 2: Se HIT mas inflate falha** — CENÁRIO CONFIRMADO, mas causa NÃO é DMA/VM: o JOGO nunca pede o dearchive do R_PermA na janela da intro (HITs pós-WAD têm args idênticos aos de boot, sem DMA em massa). Gargalo é upstream do spu1 (loader de asset/FSM de consumo). BÔNUS fix (controller, ps3recomp 6610361): stack de 256MB era COMMITTED por job → RESERVATION (elimina 'load failed' em 2/5 runs).

Instrumentar (env `PS3_TRACE_SPUDMA=1`):
- GETs do job descriptor
- se EA uncommitted → skip (já em `spu_dma.h`) — WAD payload precisa estar em range committed (`PS3_VM_LOW_MB` pode ser pequeno demais para buffer de 20MB do R_PermA)

Check:

```text
R_PermA size ≈ 20 169 344 → guest buffer + heap
PS3_VM_LOW_MB=512 → 512MB low commit — deve bastar; se VM_LOW menor, subir.
```

- [~] **Step 3: Fallback HLE dearch (só se lift spu1 não inflar)** — NÃO aplicável: o lift roda; o problema é o jogo não emitir o pedido (upstream). Fallback HLE não ajudaria.

Se após 1 sessão spu1 HIT mas WAD “oco”:
- capturar args do task (`args_ea`) com trace
- opcional: `spu_register_ppu_fallback` com host zlib/miniz no mesmo layout EDGE (esforço L — só se lift bloquear)

Preferir **fazer o lift rodar** a HLE paralelo.

- [~] **Step 4: Promover spu1 no recipe intro (não no boot mínimo sem movie)** — gate mantido; spu1 fica OPT-IN (não promovido ao launcher) por decisão conservadora do implementer até o consumo do WAD funcionar. FORCE+EOS juntos validado: EOS ganha, sem dupla-progressão.

```c
/* gow2_spu_register.c — manter gate; recipe define PS3_SPU1=1 */
if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU1"))
    spu_workload_register(0x2A5C4E67A14505B8ull, spu1_spu_func_00003050, "gow2_spu1");
```

Atualizar comentário: spu1 estável o bastante para intro/dearch sob SEH; spu2/3 still ALL-only.

- [x] **Step 5: Commit** — gow2_work `e70ef8d` (bt_intro_spu1.sh + comentário register) + ps3recomp `6610361` (stack reservation, fix controller).

```bash
git add recomp_mid_v2/gow2_spu_register.c recomp_mid_v2/bt_intro_wads.sh
git commit -m "feat(spu): enable spu1 on intro-WAD bring-up path"
```

---

### Task 5: Parede CGOWShader (`Invalid shader combination`)

**Files:**
- Diagnose via: `PS3_TRACE_SHADERMSG`, `PS3_TRACE_CRC`, `PS3_TRACE_SHADERSRC` (`run_crc.sh`, `run_shadersrc.sh`)
- Modify: `ppu_loader.cpp` (guards / material pointer) e/ou hooks guest em `gow2_work` se CRC table
- Depends: Task 1 (WADs open) + Task 4 (conteúdo real no container)

**Interfaces:**
- Mensagem guest: `Invalid shader combination: $/enginesupport/shaders/PS3/Default.ps3fx (TEXTURE=…)`
- Pointer material: `0x53D6AC` (guard contra zero já em `tblsize_guard`)
- Flag compile: texto do jogo sugere `USE_PRECALCULATED_SHADER_CRCS` em `CGOWShader.cpp`

- [x] **Step 1: Classificar o failure mode** — CLASSIFICADO (bt_class5.sh, commit e947165): CRC-miss com combination-string VÁLIDA; raiz = registry de variantes nunca populado (ICGLdrShader/func_0032109C tem 0 callers diretos; func_003CC208 recebe fonte vazia N=-1 em 18/18). PERGUNTA-CHAVE respondida: paredes shader e WAD-consumo são DESACOPLADAS (1º Invalid no frame ~1, linha 398, muito antes do R_PermA na linha 1658; taxa invalid/flip idêntica antes/depois: 1.03 vs 1.0; shaders que falham são $/enginesupport/ RESIDENTES, não os do WAD). Raiz de fundo comum: pipeline de asset primário não roda no recipe de intro.

Com recipe intro + spu1, 120s:

```bash
export PS3_TRACE_SHADERMSG=1
# opcional PS3_TRACE_CRC=1 PS3_TRACE_SHADERSRC=1 se builds com hooks existirem
```

Perguntas:
1. Combination string é lixo/zeros ou **válida mas CRC miss**? (ex. `TEXTURE=0;CONSTCOLOR=1;…` parece válida)
2. `[FIX] blocked spurious zero at [0x53D6AC]` ainda dispara?
3. `PS3_TRACE_SHADERSRC` mostra de onde lê a definição (WAD vs embedded)?

- [~] **Step 2: Estratégias (em ordem de preferência)** — NENHUMA aplicada, com justificativa por evidência: (1) não há zero-write residual; (2) bypass de CRC não gera pixels (não há microcódigo NV40 real para compilar — fonte vazia) — só vale como ferramenta de RE; (3) a tabela não está "num WAD que não carrega": o pipeline que a popularia não roda; (4) stub = desonesto, proibido. Parede multi-sessão confirmada; diagnóstico rigoroso = entrega válida do brief.

| # | Estratégia | Quando |
|---|------------|--------|
| 1 | Garantir material/shader-system pointers não zerados (expandir `tblsize_guard` se novos addrs) | zero-write residual |
| 2 | Forçar caminho “compile runtime” / desligar tabela precalc (patch de flag guest ou mid-asm hook em CGOWShader lookup) | CRC miss com string válida |
| 3 | Pré-popular CRC table a partir de dump RPCS3 / asset | se tabela está em WAD e não carrega |
| 4 | `PS3_SHADER_STUB` (já explorado em `run_dumpeff.sh`) só para não-spam — **não** conta como pixels reais | último recurso diag |

Para (2), o fluxo típico:
- achar a função que imprime a mensagem (`PS3_TRACE_SHADERMSG` RBP walk → nm)
- achar o bit `USE_PRECALCULATED_SHADER_CRCS` ou o early-return “invalid”
- patch: retornar “compile ok” / pular check CRC quando env `PS3_SHADER_CRC_BYPASS=1`

```c
/* Pseudocódigo do hook (title-side ou mid-asm):
 * se getenv("PS3_SHADER_CRC_BYPASS") e combination string non-empty:
 *   forçar caminho de load que já loga "time to load/compile shader … 0 ms"
 *   e não leave draw disabled
 */
```

Nota: o log já mostra `time to load/compile shader (…) 0 ms` **depois** do ERROR — pode ser que o draw ainda rode com shader default quebrado. Confirmar se `set_shader` D3D12 recebe FP de jogo (`PS3_TRACE_RSX_SHADERS=1`).

- [~] **Step 3: Aceite Task 5** — métricas inalteradas (350 invalid/342 flips antes == depois, 0 crash) porque nenhum fix era honesto. Aceite atingido pela via do diagnóstico completo com evidência da cadeia real (prevista no brief).

| Métrica | Antes (longskip) | Alvo |
|---------|------------------|------|
| `Invalid shader` / min | centenas | queda ≥50% ou stop spam Default.ps3fx |
| `[D3D12] set_shader` / `RSX-SH` decompile_ok | 0 de jogo (só demo) | ≥1 FP com EA guest de conteúdo |
| `bind_texture` | 0 | ≥1 (desejável) |
| flips | >0 | mantém |

- [x] **Step 4: Commit** — gow2_work `e947165` (reprodutor bt_class5.sh). Sem código de fix (decisão fundamentada acima).

```bash
git commit -m "fix(gow2): CGOWShader combination/CRC path for post-WAD draws"
```

---

### Task 6: Integração — “jogo na tela” smoke

**Files:**
- Create/update: `gow2_work/smoke_intro_to_rsx.sh`
- Modify: `ps3recomp/docs/gow2-recomp-notes.md` (substituir “content wall” por estado A–E)

- [x] **Step 1: Script E2E** — `smoke_intro_to_rsx.sh` (gow2_work 9367303), rodado 150s: WADs abrem, spu1 4x limpo, invalid=293, 0 crash, 285 flips.

```bash
#!/usr/bin/env bash
# smoke_intro_to_rsx.sh — intro FSM → WAD → spu1 → RSX real
set +e
cd recomp_mid_v2
export PS3_VFS_ROOT=".../extracted/USRDIR"
export PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64
export PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1
export PS3_MOVIE_IO=1 PS3_MOVIE_CACHE="../movie_cache" PS3_MOVIE_EOS=1
export PS3_VDEC_FORCE_SEQDONE_MS=8000
export PS3_SPU1=1
export PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1
export PS3_TRACE_RSX_SHADERS=1
export PS3_PAD_AUTOSTART=1
# opcional: PS3_SHADER_CRC_BYPASS=1 quando Task 5 entregar
timeout -k 5 150 ./boot_v2_new.exe ../EBOOT.ELF > ../smoke_intro_rsx.log 2>&1

LOG=../smoke_intro_rsx.log
echo "R_PermA=$(grep -ac "open 'R_PermA'" $LOG)"
echo "spu1_hit=$(grep -acE 'gow2_spu1|2A5C4E67A14505B8.*HIT|dispatch HIT.*3050' $LOG)"
echo "rsx_sh=$(grep -ac 'RSX-SH' $LOG)"
echo "set_shader=$(grep -ac set_shader $LOG)"
echo "invalid=$(grep -ac 'Invalid shader' $LOG)"
echo "flips=$(grep -ac SetFlipCommand $LOG)"
```

- [x] **Step 2: Matriz de aceite global** — 5/7 ✅ (recipe, FSM logs, EOS natural [stretch!], spu1, host estável); 2/7 ❌ honestos (queda de Invalid; FP de conteúdo) — ambos bloqueados pela raiz "pipeline de asset primário não roda" (diagnóstico T5, multi-sessão).

| # | Critério | Task |
|---|----------|------|
| 1 | Recipe documentado + script verde (WAD open) | 1 |
| 2 | Log `st620` transitions legível | 2 |
| 3 | (Stretch) natural EOS sem FORCE | 3 |
| 4 | spu1 HIT no path pós-logo / dearch | 4 |
| 5 | Queda de Invalid shader **ou** bypass controlado | 5 |
| 6 | ≥1 decompile FP de conteúdo de jogo **ou** bind_texture real | 5–6 |
| 7 | Host sem crash 150s; flips > 0 | 6 |

- [x] **Step 3: Docs** — gow2-recomp-notes.md: content wall desmembrada em A–E (A/B vencidas, C parede atual, D desacoplada/multi-sessão, E bloqueada), recipe oficial (+nota PS3_VDEC_ASYNC/f811ee3), matriz de aceite.

Atualizar `docs/gow2-recomp-notes.md`:

- Parede de conteúdo desmembrada em A–E
- Recipe env oficial
- O que o plano RSX anterior precisa do jogo (e agora recebe)

- [x] **Step 4: Commit docs + smoke** — ps3recomp `8594581` (docs) + gow2_work `9367303` (smoke).

```bash
git commit -m "docs+smoke: intro FSM to WAD to RSX bring-up path"
```

---

## Fora de escopo

- Gameplay além do menu / legal screen completo
- spu0 EDGE graphics full / SPURS kernel completo
- Perfect CRC dump de todos os `.ps3fx` do jogo
- Substituir movie_hle por decode 100% no guest SPU MPEG
- Vulkan

## Riscos

| Risco | Mitigação |
|-------|-----------|
| FORCE SEQDONE pula legal/audio e deixa estado inconsistente | Task 3 natural; validar st620 após force |
| spu1 HIT mas SEH abort → WAD incompleto | trace DMA; fallback HLE zlib só se necessário |
| CRC bypass mascara bugs e gera lixo na tela | gate env; preferir fix de data/pointer |
| `PS3_NOMOVIES=1` no launcher default conflita com movie path | recipe intro **não** seta NOMOVIES; documentar |
| Case sensitivity de `R_PermA` vs `r_perma.wad_ps3` | try multi-case em `movie_io_open` se portar |

## Estimativa

| Task | Esforço | Bloqueia pixels? |
|------|---------|------------------|
| 1 Recipe + smoke WAD open | S (0.5 d) | desbloqueia caminho |
| 2 FSM logs | S | diag |
| 3 EOS natural | M (1–3 d) | fidelidade |
| 4 spu1 dearch | M (1–3 d) | **sim** (conteúdo) |
| 5 CGOWShader | M–L (2–5 d) | **sim** (draws) |
| 6 Integração | S | aceite |

**Ordem recomendada:** 1 → 2 → 4 → 5 → 6, com 3 em paralelo se houver tempo.

---

## Self-review

1. **Spec coverage:** “destravar FSM que carrega WADs” → Tasks 1–3; “ver jogo na tela” exige 4–6 (spu1 + shader), explicitamente encadeados.
2. **Evidência:** `longskip.log` já prova WAD open pós-FORCE; plano não redescobre isso — industrializa e ataca o pós-WAD.
3. **Placeholders:** nenhum TBD sem hipótese testável e comando de medição.
4. **Repos vs plano RSX:** não reabre FP/VP/tex; referencia pipeline publicado.
