# macOS pós-st620=3 → SMPD → WAD — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** No Mac/arm64, com open/poll FIOS já honestos (`st620≥3`), provar o que acontece após o EOS armado e **abrir `R_LglScA` e/ou `R_PermA`** sem forjar `st620` nem escrever `obj+0x744` na RAM guest.

**Architecture:** A RE estática (Task 7 follow-up C3644, commit `80e44fa`) **refutou** o diagnóstico “read-hook cego + consumidor não-FSM `func_000C3644`”. Os leitores literais de `+0x744` são todos a FSM `0x2C0xxx`; `st620=0` vem de **`func_002BFF88` (Stop)** com broadcast **`'SMPD'`** via `func_0043FF30`. O wall actual é **a jusante desse broadcast** (ou, se a sonda negar B, cobertura do hook nos gates inline). Este plano: (1) sonda discriminadora, (2) RE do consumidor de `'SMPD'`, (3) fix mínimo alinhado com o canal Windows (callback/sequência, não stamp de flags).

**Tech Stack:** lift `gow2-recomp/recomp_macos_v2`, patches `recomp_mid_v2/patch_*.py`, `movie_eos_arm.c` / `PS3_MOVIE_EOS` / `PS3_MOVIE_DONE_MS`, runtime `ppu_loader.cpp` (`g_movie_eos_ea` + `vm_read8`), smokes bash PID-kill, env `PS3_TRACE_*` OFF por default.

**Repos / predecessores:**
- Plano **4.0** (`macos-intro-audio-open-wall`) ✅ — sticky + play-already → `st620 1→3`
- Plano **4.EOS** T0–T5 ✅ — sampler, arm NATURAL, FORCE portado mas **cold** no intro (`Open/StartSeq=0`)
- Plano **4.EOS Task 7** parcial — hipóteses A/C3644 **refutadas**; este documento **substitui** o “próximo passo” operacional
- Plano **04-content-intro-wad** ⏳ — só depois do aceite WAD daqui

## Global Constraints

- **Proibido forjar:** `vm_write` host de `obj+0x620` (st620) ou `obj+0x744` (f744) “para avançar”.
- **Proibido** `Sleep(1)` extra no poll / acquire POSIX (já medido: ~3× throughput).
- Probes **env-gated**, OFF por default, no-op no baseline; smokes de perf sem `PS3_TRACE_*`.
- Smokes matam o boot pelo **PID** com TERM depois **-9** (não `pkill` genérico) — evita órfãos a load 75.
- Preferir **chamar código guest** (OPD / broadcast / FIOS) a simular estados.
- Windows não regredir; mudanças de runtime partilhadas atrás dos mesmos gates.
- Nunca commit de game data (`*.m2v`, cache, EBOOT, WADs).
- Commits em português, paths explícitos, sem co-authorship trailers.
- **Lift `-O0` default** — `-O1` no lift quebrou boot (st620=0, 1 acquire); não misturar A/B opt neste plano.

---

## Estado medido (baseline honesto, 2026-07-20/21)

| Facto | Evidência |
|-------|-----------|
| Boot corre headless | `boot_gow2`, 25–30 s, load ~1–2 sem órfãos |
| `st620` 0→1→3 (EOS OFF) | `[MOVIEFSM]`; sticky pub/hit/con ≥1 |
| `f744=0` sem arm | EOS OFF / M3 |
| Arm + HIT com produtor | `[MOVIEEOS] arming` + `read-hook HIT` |
| Log amostrado `3→0` | **não prova** idle parasita (estados 4..10 são ns; sampler 200 ms) |
| `func_000C3644` | float blend; **0× `vm_read8`** — não é o HIT |
| `st620=0` no lift | só ramos de **Stop** `func_002BFF88` + cauda `'SMPD'` + `func_0043FF30` |
| WADs intro | **0** opens `R_LglScA` / `R_PermA` no Mac medido |
| FORCE/vdec no intro | portado; **Open/StartSeq=0** → cold |

### Hipóteses (só uma sobrevive à Task 1)

| ID | Hipótese | Se verdadeira, wall é… |
|----|----------|-------------------------|
| **B** (preferida) | Hook → gate `+0x744=1` → FSM 3…0xB em µs → `Stop`/`'SMPD'` → **ninguém abre WAD** | consumidor de `'SMPD'` / sequência pós-Stop |
| **H-hook** | Nenhum gate lê 1; Stop vem de outro sítio | cobertura do hook / caller real do HIT |
| **H-force** | `'SMPD'` sai e é consumido, mas falta canal vdec/SEQDONE que no Win abre WADs | reabrir FORCE **só** se Open/StartSeq>0 ou atalho documentado sem forja |

**Não reabrir:** “fix do consumidor não-FSM / seletor de ra do hook” como primeira linha de implementação (refutado).

---

## File map

| Path | Papel |
|------|--------|
| `gow2-recomp/recomp_mid_v2/patch_st3_probe.py` | sonda st=3 existente (`PS3_TRACE_ST3`) |
| `gow2-recomp/recomp_mid_v2/patch_smpd_probe.py` | **criar** — 4 sítios (3 gates + Stop) |
| `gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` / `_002.cpp` | alvos do patch (lift gitignored; patch versionado) |
| `gow2-recomp/movie_eos_arm.c` | arm NATURAL + `PS3_MOVIE_DONE_MS` / sampler |
| `ps3recomp/runtime/ppu/ppu_loader.cpp` | `g_movie_eos_ea`, `vm_read8` HIT log |
| `gow2-recomp/smoke_intro_macos.sh` / novo `smoke_post_st3_wad.sh` | aceite |
| `gow2-recomp/build_macos.sh` | rebuild host+link após patch no lift |
| `ps3recomp/docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md` | apontar este plano como wall activo |
| `ps3recomp/docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md` | Task 7: link para este plano |

---

### Task 1: Sonda discriminadora B vs H-hook

**Files:**
- Create: `gow2-recomp/recomp_mid_v2/patch_smpd_probe.py`
- Modify (via patch, não edit manual permanente): sítios no lift listados abaixo
- Test: apply dry-run + in-boot 40 s com arm

**Interfaces:**
- Consome: padrão de `patch_st3_probe.py` (marker idempotente, `getenv` gate)
- Produz logs (stderr):
  - `[EOSGATE] site=002C0508|002C0570|002C05C8 val=%u branch=adv|wait`
  - `[STOP] SMPD st620->0 ra_ctx=…` (antes do write 0 + após setar r3/r4 do broadcast)

**Sítios exactos (RE `80e44fa`):**

| # | Função | Ficheiro lift (aprox.) | O quê logar |
|---|--------|------------------------|-------------|
| 1 | `func_002C0508` | `ppu_recomp_001.cpp` ~36550 | `vm_read8(r30+0x744)` → val + ramo |
| 2 | `func_002C0570` | `ppu_recomp_002.cpp` ~224703 | idem |
| 3 | `func_002C05C8` | `ppu_recomp_002.cpp` ~224755 | idem |
| 4 | `func_002BFF88` | `ppu_recomp_001.cpp` ~36203 | imediatamente **antes** `vm_write32(…+0x620, 0)`: logar st anterior, e que r3/r4 serão `'SMPD'`/0x1C |

Gate: `PS3_TRACE_SMPD=1` (ou reutilizar `PS3_TRACE_ST3=1` se preferir um só env — **escolher um nome e documentar no script**). OFF default.

- [ ] **Step 1: Escrever `patch_smpd_probe.py` idempotente**

```python
#!/usr/bin/env python3
"""PS3_TRACE_SMPD — gates +0x744 inline + Stop/SMPD. OFF default. Read-only logs."""
from pathlib import Path
import sys

MARKER = "SMPD-PROBE"
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"

# Para cada sítio: needle único (linhas do lift) + insert com:
#   static int on=-1; if(on<0){ const char*e=getenv("PS3_TRACE_SMPD"); on=(e&&*e&&*e!='0'); }
#   if(on) fprintf(stderr, "[EOSGATE] ...\n");
# e no Stop:
#   if(on) fprintf(stderr, "[STOP] SMPD prev_st=%u ->0\n", (unsigned)prev);
```

Espelhar o estilo exacto de `patch_st3_probe.py` (marker no comment, skip se já aplicado, exit≠0 se needle em falta).

- [ ] **Step 2: Aplicar + rebuild incremental**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
python3 recomp_mid_v2/patch_smpd_probe.py recomp_macos_v2
# touch dos .cpp patchados se o build for mtime-based
./build_macos.sh   # LIFT_OPT=-O0
```

Expected: link OK, `boot_gow2` actualizado.

- [ ] **Step 3: RED — baseline sem arm (probe ON)**

```bash
# 30s, probes de perf OFF excepto PS3_TRACE_SMPD
(
  set -a; . ./env_gow2.sh; set +a
  export PS3_NO_RSX=1 PS3_TRACE_SMPD=1 PS3_MOVIE_EOS=0
  unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/smpd_off.log 2>&1 & BPID=$!
sleep 30; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
grep -E '\[EOSGATE\]|\[STOP\]|st620' /tmp/smpd_off.log | head -40
```

Expected RED para o wall WAD: **zero** `R_LglScA`/`R_PermA`.  
Expected OK: `st620` max≥3 possível; gates com val=0; `[STOP]` pode ou não aparecer sem filme “done”.

- [ ] **Step 4: GREEN diagnóstico — arm + produtor time-based curto**

```bash
# PS3_MOVIE_DONE_MS baixo o suficiente para armar dentro da janela (ex. 3000–8000)
# sem forjar st620. Produtor já documentado em movie_eos_arm.c.
(
  set -a; . ./env_gow2.sh; set +a
  export PS3_NO_RSX=1 PS3_TRACE_SMPD=1
  export PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=5000
  unset PS3_VDEC_FORCE_SEQDONE_MS
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/smpd_arm.log 2>&1 & BPID=$!
sleep 40; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
echo "=== classificar ==="
grep -c 'arming EOS' /tmp/smpd_arm.log
grep -c 'read-hook HIT' /tmp/smpd_arm.log
grep '\[EOSGATE\]' /tmp/smpd_arm.log
grep '\[STOP\]' /tmp/smpd_arm.log
grep -E 'R_LglScA|R_PermA' /tmp/smpd_arm.log || true
```

**Tabela de decisão (obrigatória no log da task):**

| Observado | Classe | Próxima task |
|-----------|--------|--------------|
| ≥1 `[EOSGATE] val=1 branch=adv` **e** `[STOP] SMPD` depois | **B** | Task 2 (consumidor SMPD) |
| HIT existe, todos gates `val=0`, Stop de outro sítio | **H-hook** | Task 1b (abaixo) |
| Sem arm / sem HIT | produtor frio | re-medir `PS3_MOVIE_DONE_MS` / open sticky; não avançar fix |
| Gate val=1, **sem** Stop, sem WAD | FSM parada noutro flag (`+0x746`?) | RE `func_002C0688` / flag 746 — mini-nota + Task 2 adaptada |

- [ ] **Step 1b (só se H-hook):** alargar log do HIT

Em `ppu_loader.cpp` `vm_read8` (já imprime `ra=%p` nas primeiras 3 HITs): com `PS3_TRACE_SMPD=1`, imprimir também `ra1`/`ra2` **e** resolver com `dladdr` para `dli_sname` (símbolo lift). Documentar o **caller real** do primeiro HIT armado. **Não** “fixar” seletor de ra sem este nome.

- [ ] **Step 5: Commit (gow2-recomp + nota de classificação)**

```bash
cd gow2-recomp
git add recomp_mid_v2/patch_smpd_probe.py
# se criou notes/2026-07-21-smpd-class.md com a tabela preenchida:
git add notes/2026-07-21-smpd-class.md
git commit -m "diag(smpd): sonda gates +0x744 e Stop/SMPD (PS3_TRACE_SMPD)"
```

---

### Task 2: RE do consumidor de `'SMPD'` / `func_0043FF30`

**Só executar se Task 1 classificou B** (ou gate val=1 + Stop).

**Files:**
- Read: lift `func_0043FF30`, callers/callees; handlers de mensagem `0x1C` / fourcc `'SMPD'`
- Read (ref): `docs/superpowers/plans/2026-07-14-gow2-intro-movie-fsm-wads.md` (sequência Win → WAD)
- Write: `gow2-recomp/notes/2026-07-21-smpd-consumer.md` (ou data do dia)
- Optional probe: 1 log no entry de `func_0043FF30` e no primeiro handler que filtra `r3=='SMPD'`

**Interfaces:**
- Consome: prova in-boot de `[STOP] SMPD`
- Produz: grafo “Stop → broadcast → ? → open WAD” com EAs de funções

- [ ] **Step 1: Localizar `func_0043FF30` no lift e extrair o corpo**

```bash
grep -n 'void func_0043FF30' recomp_macos_v2/ppu_recomp_*.cpp
# extrair até à próxima void func_*; listar callees func_00*
```

Mapear: tabela de listeners? fila? chamada directa a loader de WAD?

- [ ] **Step 2: Callers de `func_002BFF88` (Stop) no path intro**

Já contados 13 sítios de jogo. Com `PS3_TRACE_SMPD`, se necessário logar **qual** caller (via `ra` no entry de Stop **ou** patch nos 2–3 candidatos do path intro). Preferir **estático** primeiro.

- [ ] **Step 3: Comparar com Windows FORCE path (docs only)**

Ler no plano Windows / `bt_intro_wads.sh` o que acontece **após** SEQDONE: quem abre `R_LglScA`? É o mesmo listener SMPD ou um callback vdec OPD?

Preencher na nota:

```markdown
| Evento | Windows (doc/medido) | Mac recomp (medido) |
|--------|----------------------|---------------------|
| EOS / done | overlay / FORCE | arm time-based + hook |
| Stop / SMPD | ? | [STOP] sim/não |
| Open R_LglScA | sim | não |
| Gap | … | … |
```

- [ ] **Step 4: Classificar gap**

| Classe gap | Significado | Task de fix |
|------------|-------------|-------------|
| **G1** | `'SMPD'` nunca broadcast | porquê Stop não corre — voltar Task 1 |
| **G2** | broadcast corre; **0 listeners** / handler no-op | registar/ligar listener HLE **só** se for o contrato real; senão RE quem devia `cellFs`/FIOS open |
| **G3** | listener corre; open WAD falha (VFS/psarc/path) | path/VFS — handoff plano 04 com prova |
| **G4** | Win usa **outro** sinal (vdec OPD) e SMPD é irrelevante para WAD | reabrir FORCE **com** Open/StartSeq ou documentar atalho sem forja |

- [ ] **Step 5: Commit nota RE**

```bash
git add notes/2026-07-21-smpd-consumer.md
git commit -m "docs(smpd): RE do consumidor de broadcast pos-Stop"
```

---

### Task 3: Fix mínimo do gap (sem forja de FSM)

**Só após Task 2 com classe G2/G3/G4 nomeada.** Uma sub-faixa — **não implementar as três.**

#### 3-G2: listener / dispatch morto

- [ ] **Step 1:** RED smoke — contador `[SMPD] dispatch` = 0 com Stop vivo  
- [ ] **Step 2:** Implementação mínima: fazer o dispatch chegar ao **mesmo** handler que o guest usaria no Win (OPD guest-first). Se for HLE, gate `PS3_MOVIE_SMPD=1` e log `[SMPD] HLE …`.  
- [ ] **Step 3:** GREEN — ≥1 linha de open `R_LglScA` **ou** `R_PermA` em 60 s  
- [ ] **Step 4:** M3 forge-trap ainda verde (`PS3_MOVIE_EOS=1` sem produtor → zero arm)

#### 3-G3: VFS / path

- [ ] **Step 1:** logar path exacto do open falhado  
- [ ] **Step 2:** corrigir root/VFS/`movie_io` **sem** saltar a FSM  
- [ ] **Step 3:** GREEN open WAD  
- [ ] **Step 4:** handoff explícito para plano `2026-07-20-04-content-intro-wad.md`

#### 3-G4: FORCE / sequência tipo Win

- [ ] **Step 1:** medir `cellVdecOpen`/`StartSeq` no intro com sticky (ainda 0?)  
- [ ] **Step 2:** se continuar 0, **não** fingir FORCE; documentar e preferir G2 se SMPD for o sinal real  
- [ ] **Step 3:** se Open>0, `PS3_VDEC_FORCE_SEQDONE_MS` + smoke WAD (já portado em T2 do plano EOS)  
- [ ] **Step 4:** GREEN WAD; M3 intacto

**Proibido em qualquer faixa:** `st620 = 5/11` no host; `vm_base[obj+0x744]=1` “para destravar”.

---

### Task 4: Smoke de aceite + índice

**Files:**
- Create: `gow2-recomp/smoke_post_st3_wad.sh`
- Modify: `ps3recomp/docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md` (wall activo = este plano)
- Modify: `ps3recomp/docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md` — Task 7 status → “ver plano post-st3-smpd-wad”

- [ ] **Step 1: Smoke**

```bash
#!/usr/bin/env bash
# smoke_post_st3_wad.sh [secs=45]
# Aceite: sticky/open OK; com recipe NATURAL (ou FORCE se G4):
#   - [MOVIEEOS] arming + HIT  (se NATURAL)
#   - [STOP] ou st volta a 0 após progresso  (opcional se WAD abrir antes)
#   - grep -E 'R_LglScA|R_PermA' >= 1
#   - M3: sem produtor => 0 arm
# Mata SEMPRE por PID (TERM, depois -9).
set -uo pipefail
HERE=$(cd "$(dirname "$0")" && pwd); cd "$HERE"
SECS=${1:-45}
# ... run_boot helper igual smoke_intro_macos.sh ...
```

Critérios:

| Modo | PASS se |
|------|---------|
| M0 baseline EOS=0 | `st620_max≥3` (flaky: ≥1/1 em load baixo); **0** arm; **0** forge |
| M2 NATURAL | arm+HIT; **≥1** `R_LglScA` **ou** `R_PermA` **ou** reclass G* escrita se WAD impossível com prova |
| M3 forge-trap | 0 arm, 0 HIT forjado |

- [ ] **Step 2: Actualizar INDEX**

Na tabela cadeia, linha **4.EOS** / próximo passo:

```markdown
| **4.EOS+** | `2026-07-20-macos-post-st3-smpd-wad.md` | pós-st3 / SMPD / WAD | 🟡 activo | open R_* sem forja |
```

- [ ] **Step 3: Commits**

```bash
# gow2-recomp
git add smoke_post_st3_wad.sh
git commit -m "test(intro): smoke pos-st3 / SMPD / WAD"

# ps3recomp
git add docs/superpowers/plans/2026-07-20-macos-post-st3-smpd-wad.md \
        docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md \
        docs/superpowers/plans/2026-07-20-macos-movie-eos-fsm.md
git commit -m "docs(plan): wall pos-st3 via SMPD/WAD (substitui hipotese C3644)"
```

---

## Ordered execution summary

| # | Task | Red | Green |
|---|------|-----|-------|
| 1 | Sonda SMPD/gates | sem classificação B/H-hook | tabela preenchida + patch versionado |
| 2 | RE consumidor SMPD | grafo desconhecido | nota G1–G4 |
| 3 | Fix mínimo G* | 0 WAD opens | ≥1 `R_LglScA`/`R_PermA` **ou** reclass documentada |
| 4 | Smoke + INDEX | smoke falha | M0/M2/M3 verdes segundo tabela |

**Fora de âmbito:** RSX pixels (plano 05); FIOS async HLE 002B3D1C (⛔); opt `-O1` lift; visual overlay movie.

---

## Self-review

| Requisito | Task |
|-----------|------|
| Confirmar se 3→0 é bug ou conclusão | Task 1 |
| Não atacar fix C3644/refutado | Constraints + preâmbulo |
| Não forjar st620/f744 | Global + Task 3 |
| WAD open como aceite real | Task 3 + 4 |
| FORCE só se G4 / vdec vivo | Task 3-G4 |
| Sondas OFF default | Task 1 |
| PID-kill smokes | Task 1/4 |
| Handoff plano 04 | Task 3-G3 / aceite WAD |

**Placeholders:** nenhum TBD operacional — sítios de patch e comandos estão nomeados; linhas de lift são “~” e o patch deve falhar alto se o needle não bater (rebuild do lift).

---

## Handoff de execução

Plano guardado em:

`docs/superpowers/plans/2026-07-20-macos-post-st3-smpd-wad.md`

**Opções:**

1. **Subagent-driven** — um agente por task (1→2→3→4), review entre tasks  
2. **Inline** — executar nesta sessão com checkpoints  

Começar **sempre** pela Task 1 (sonda); não saltar para FORCE nem para “fix do hook seletivo”.
