# Sessão oráculo intro (2026-07-20) — recomp medido + gap nomeado

RPCS3 **não está instalado** nesta máquina (`which rpcs3` / Application Support
vazio). A coluna "RPCS3" abaixo é o **comportamento esperado** a partir de:

1. RE estática no lift (cadeia Play → FIOS documentada em `patch_fios_open_probe.py`)
2. Medição in-boot com `PS3_TRACE_FIOSOPEN=1` + `PS3_TRACE_SNDOPEN=1` + `PS3_TRACE_FIOSSCHED=1`
3. Spec de paths / módulos no tree RPCS3 local (`../_ref_rpcs3`)

Ferramentas: `./oracle_intro_checklist.sh`, `../ps3recomp/tools/oracle_parse_recomp_log.py`.

Planos: `ps3recomp/docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md`.

---

## Status late (fim de sessão) — wall reclassificado

| Gap | Estado late |
|-----|-------------|
| Open m2v FIOS | ✅ handle ≠0 |
| Scheduler escreve done90 | ✅ |
| Poll consome done / 002B4274 | ✅ sticky + play-already |
| st620 1→3 | ✅ in-boot |
| f744 / EOS / 3→5→11 / WAD | ⏳ **parede actual** (plano EOS Task 7) |

---

## Update 2026-07-21 — INTRO VENCIDO (st620 3→11) + higiene 0.1–0.3 fechada

**A "parede actual" da linha acima está VENCIDA.** O intro-movie FSM progride agora 1→3→**11**:
- Fix A3b (commit `b51013e`, do user): HLE de audio-stream-complete atado à duração real do `.wav`
  (`movie_audio_should_mark_done`: só marca com produtor real + st==3 + handle de áudio + one-shot;
  NÃO toca st620/+0x744). Desbloqueia 3→4 → `cellVdecOpenEx`+`StartSeq` correm (site 002C069C).
- Produtor NOVO e mais fiel: o **overlay VideoToolbox** (sessão Metal concorrente, M0) TOCA o `.m2v`
  real até ao fim (`[movie-vt] EOS flag set: movie_vt_overlay_done=1 (frames=330)`) → `overlay_done=1`
  → `[MOVIEEOS] overlay done (st620=11)`. `env_gow2.sh:20` liga `PS3_MOVIE_HLE=1` por default, logo o
  overlay é agora um produtor REAL sempre presente (o filme toca de verdade, não é timer).

**Higiene (do /goal 0.x):**
- **0.1 (docs/oracle):** este update. A tabela "Status late" está superada — st620 chega a 11.
- **0.2 (patch_fios_sticky):** FEITO — `recomp_mid_v2/patch_fios_sticky.py` versiona a metade de lift do
  sticky do done-word (commit `dbbd2b0`, round-trip byte-idêntico). Sticky durável a re-lift.
- **0.3 (smoke multi-run, sem forja):** CORRIDO 2026-07-21 (PID-kill only, seguro p/ sessão concorrente):
  - **Forja-segura** (EOS on, `PS3_MOVIE_HLE=0` + sem produtor tempo): **arm 0/3**, st_max **3** (não 11) —
    NADA arma sem produtor real, FSM pára em 3. Sem forja. (Nota: a barra M3 antiga do smoke assumia só
    `PS3_MOVIE_DONE_MS` como produtor; com o overlay VT default-on é preciso `PS3_MOVIE_HLE=0` p/ o teste ser válido.)
  - **NATURAL** (overlay real toca, `PS3_TRACE_MOVIEOBJ=1`): st620≥3 **3/3** (=11), **002B4274 DONE 3/3**, 0 órfãos.

**A jusante (fora do intro):** os WADs legais (R_LglScA/R_PermA) são PULADOS (o filme auto-completa antes do
FORCE); o jogo entra no motor/render e bate na parede de shaders — **H2: registry de variantes VAZIO**
(o jogo constrói combinations válidas, mas o lookup CRC precalc não tem entrada; 49612× Default.ps3fx).
Registry narrowed a **A1**: o typemap walk (`func_00171244`) nunca é despachado — único dispatcher natural
`func_0032E200` provado nunca-alcançado por 2 ângulos (direto + census indirecto dos 47 `ps3_call_opd`).
Planos: `2026-07-21-shader-combination-zeroed.md`, `2026-07-21-shader-registry-typemap-walk.md`.

**Fixes aplicados (motor + port):**
- `ps3recomp` giant lock POSIX + `lwcond` prio (`a82c594`); sticky API em `ppu_loader.cpp`
- `gow2-recomp` `patch_fios_play_already_active.py`, `patch_fios_cancel_yield.py` (`c5f718c`)
- lift local: `STICKY-RESTORE` / publish / consume (gitignored `recomp_macos_v2/`)

**Evidência:** `/tmp/fios_sticky.log`, `/tmp/fios_sticky2.log` —
`STICKY-RESTORE` → `002B4274 DONE` → `st620 1 -> 3` (fica em 3, `f744=0`).

---

## Como repetir

```bash
cd gow2-recomp
./oracle_intro_checklist.sh 35
# com log RPCS3 quando existir:
./oracle_intro_checklist.sh 35 /path/to/RPCS3.log

PS3_TRACE_FIOSOPEN=1 PS3_TRACE_SNDOPEN=1 PS3_TRACE_FIOSSCHED=1 \
  ./oracle_intro_checklist.sh 40
```

Aceite mínimo pós-4.0 (baseline, EOS OFF):

```bash
grep -E 'STICKY-RESTORE|002B4274 DONE|st620 .*->' /tmp/run.log
# esperável: DONE ou STICKY; st620 max ≥ 3; sem MOVIEEOS se EOS=0
```

---

## Tabela de gaps (10 linhas) — actualizada late

| # | Pergunta | recomp late | RPCS3 / consola (esperado) | Gap / acção |
|---|----------|-------------|----------------------------|-------------|
| 1 | Opens cellFs | 1× `gow2.psarc` | archive + members | OK; membros via FIOS |
| 2 | Path SmLogo | `/_movies/smlogo_v2.m2v` | mesmo | OK |
| 3 | Via | FIOS mediaobj | FIOS | OK |
| 4 | movie_io m2v? | não (psarc) | n/a | esperado |
| 5 | Path exacto | smlogo_v2.m2v flags 0x20 | — | OK |
| 6 | wav / SNDOPEN | path visto; WAV_SURVIVABLE | stream paralelo | 2º open pool = ruído |
| 7 | Poll `done≠0`? | **sim** (sticky → 4274) | done antes do poll útil | **destravado** |
| 8 | Scheduler FIOS | sched + complete + done90 | thread fios | OK |
| 9 | st620 | **0→1→3** (parque em 3) | 1→3→5→11→done | **falta 3→5→11** |
| 10 | f744 / WAD | f744=0; sem R_* | EOS + WAD | **parede actual** |

### Diagnóstico refinado (histórico → late)

| Fase | Frase |
|------|--------|
| Manhã | open falha / handles 0 |
| Mid | open OK; done **produzido**; poll **não consome** |
| Late | poll **consome** (sticky); FSM em **3**; falta **EOS/f744/WAD** |

Causas do não-consumo (documentadas, parcialmente mitigadas):

1. Play reentrante `002C0498` → teardown + re-open → `SEM OP LIVRE` → `io=0`  
   → **play-already-active**
2. Guest `[op+0x90]` fica 1 no fim do complete (RAWMEM) e 0 no 1º poll **sem** write32 ao EA  
   → **sticky publish/restore** (workaround honesto do sinal do produtor; clear fantasma opcional)

---

## Próxima acção única (implementação)

| Prioridade | Acção | Não fazer |
|------------|--------|-----------|
| **1** | Plano EOS **Task 7**: pós-st620=3 → f744 / 3→5→11 / open R_* | Forjar st620=5 ou f744 |
| **2** | Higiene: `patch_fios_sticky_*.py` + smoke N/M DONE4274 | Duplicar HLE 002B3D1C (refutado) |
| **3** | (opcional) pinchar clear fantasma de done | Sleep hacks cegos no poll |
| **4** | RPCS3 instalado para coluna oráculo real | Copiar GPL / forjar porque o emulador avançou |

---

## RPCS3 nesta máquina

**Não instalado.** Para fechar a coluna oráculo de verdade:

1. Instalar https://rpcs3.net/ ou build GitHub  
2. Mesmo dump NPUA80491  
3. `./oracle_intro_checklist.sh 40 "$HOME/Library/Application Support/rpcs3/RPCS3.log"`  

---

## Ficheiros desta sessão

| Artefacto | Path |
|-----------|------|
| Checklist / logs oracle | `oracle_out/` |
| Logs sticky | `/tmp/fios_sticky.log`, `/tmp/fios_sticky2.log` |
| Tools | `oracle_intro_checklist.sh`, `../ps3recomp/tools/oracle_parse_recomp_log.py` |
| Planos | `docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md` (+ 4.0, EOS, fios-async) |
