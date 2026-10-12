# Nested walk typemap (`func_00171244`) nunca invocado — plano DIAGNÓSTICO-PRIMEIRO

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Descobrir, com medição in-boot e um experimento discriminador barato (1–2 boots), **por que** `func_00171244` (nested walk do typemap) **nunca é invocado no caminho natural**, e **onde a cadeia quebra** — sem GATE-FORCE, stream sintético, CRC bypass ou `PS3_SHADER_DEMO` como aceite.

**Architecture:** O registry de variantes / tabela CRC precalculada está **vazio** (veredito H2 fechado 2026-07-21). A cadeia que *deveria* populá-lo é guest-side: tipo `0xF85F9B1E` registado → walk do typemap via vtable `@0x5130B8+8` (OPD `0x522E70` = `func_00171244`) → lookup/dispatch → `ICGLdrShader` (`func_0032109C`) → `SHADERSRC` N>0 → CRC table. O runtime já tem GATE-FORCE (`PS3_GATE_FORCE=1`) que **força** esse walk com stream sintético — ferramenta de **diag only**, **não** aceite. Este plano só **observa** e classifica o ponto de quebra natural.

**Tech Stack:** `boot_gow2` (gow2-recomp), env `PS3_TRACE_TYMAP` / `PS3_TRACE_LDRSH` / `PS3_TRACE_SHADERSRC` / `PS3_TRACE_SHREG`, patches idempotentes `recomp_mid_v2/patch_tymap_*.py` + `patch_32e200_opd.py`, runtime `ps3recomp/runtime/ppu/ppu_loader.cpp` (só se precisar de probe host extra, gated).

## Global Constraints

- Metodologia: evidência → hipóteses **ordenadas** → experimento **discriminador** → refutações **medidas** → fix mínimo (só depois do veredito de classe).
- Probes **sempre** env-gated, **OFF por default**, no-op no baseline.
- **Proibido como “fix” / aceite:** CRC bypass, `disable USE_PRECALCULATED_SHADER_CRCS`, stub de combination, estampar magic, null-check cosmético, mascarar guard, `PS3_SHADER_DEMO`, **GATE-FORCE** (`PS3_GATE_FORCE=1`) como aceite natural, stream sintético como aceite.
- Aceites distinguem **in-boot medido** vs offline vs não-exercitado vs **FORÇADO**.
- Diagnóstico rigoroso **é** entrega válida. Se a conclusão honesta for “só GATE-FORCE destrava” → documentar **parede aberta**, não vencida.
- Commits em PT, paths explícitos, sem dados de jogo, sem co-authorship.
- Matar boots por **PID** (`TERM` → `-9`), nunca `pkill -f boot_gow2`.
- Não redo: ler e **citar** resultados já medidos. Não reabrir H0/H1/H3 de combination (já fechados).
- Não `git add` de `ppu_recomp_*.cpp` / WADs / EBOOT — só `patch_*.py` + docs + runtime se necessário.

---

## NÃO redo — factos já medidos (base)

| Facto | Evidência | Implicação |
|-------|-----------|------------|
| Combination Default **válida** `(TEXTURE=0;CONSTCOLOR=1;HASCOLOR=0;TRANSFORM=0)`; mat=`0x0FEFFAF8`, fmt=`0x4C61B0` | plano `2026-07-21-shader-combination-zeroed.md` + note `gow2-recomp/notes/2026-07-21-combo-discriminate.md` (commit gow2 `b99c8e2`) | H0/H3 refutadas; spam ≠ props lixo |
| Guest pede `disable USE_PRECALCULATED_SHADER_CRCS` | mensagem TTY FlashUI/Default | CRC table / registry **vazio** no port |
| Escala: **49612×** Default.ps3fx | note combo-discriminate | lookup falha em massa no hot path |
| **H1 desfavorecida**; **H2 confirmada** | Boot A≈B combo idêntica; Win desacopla shader de WAD | **não** “só carregar WAD de novo” |
| Parede **[D]** multi-sessão | `docs/gow2-recomp-notes.md` ~L97 | `ICGLdrShader` 0 callers directos; fonte vazia N=-1 18/18; registry nunca populado |
| Task 5 intro plan | `docs/superpowers/plans/2026-07-14-gow2-intro-movie-fsm-wads.md` Task 5 | CRC-miss string válida; “nenhum fix honesto”; pipeline de asset primário ausente no recipe intro |
| Pós-R_PermA full (20 169 344 B) | `docs/gow2-asset-pipeline-map.md` Task 3f/4 | `SHADERSRC` N=0 (records vazios); `[LDRSH]=0` natural; nested walk **aberto** |
| Tipo `0xF85F9B1E` **registado** → OPD `0x534838` | `ppu_loader.cpp:2075–2077` | registo existe; **walk** não corre |
| `func_00171244` **nunca** invocado natural | nota runtime + TYMAP-171=0 histórico | TYMAP-171=0, LDRSH=0 |
| GATE-FORCE: stream sintético + call OPD `0x522E70` | `ppu_loader.cpp:2085–2196`, gate `PS3_GATE_FORCE` em `ps3_indirect_call` `:1512–1524` | prova path **mecânico** com tag; **não** aceite |
| LDRSH recognized=1 só com payload sintético tag `0x283F6879` | `ppu_loader.cpp:2174–2178` | depois spins sem EFCT real; microcódigo em HOSTRES gzip, não SHGX stub |

---

## Pergunta central

> **Por que `func_00171244` (nested walk do typemap) nunca é invocado no caminho natural, e onde a cadeia quebra?**

### Cadeia que DEVIA invocar o walk

```
tipo 0xF85F9B1E registado  ──[SHREG]/[TYMAP-REG]──►  map @ TOC+0xD8C/D90 → OPD 0x534838
        │
        ▼
componente com typemap  (vt @ 0x5130B8, typemap = component+0xDC)
        │
        │  alguém vcall method +0x8  →  OPD 0x522E70
        ▼
func_00171244 (r3=typemap, r4=stream)     [TYMAP-171]
        │  lê type tag + size via func_001856A8
        │  lookup map TOC-0x3A3C via func_0018E814  [TYMAP-LK]
        ▼
dispatch → ICGLdrShader body func_0032109C (tag 0x283F6879)  [LDRSH]
        ▼
SHADERSRC / defs (func_003CC208) N>0  →  tabela CRC precalc populada
        ▼
CGOWShader lookup (func_001655F0) deixa de spammar "Unknown combination"
```

**Elo já partido (observado):** entre “tipo registado” e “`func_00171244` corre”. O resto a jusante (LDRSH / SHADERSRC N) só é testável **depois** do walk natural ≥1, ou fica classificado como classe C se o walk corre mas a fonte está vazia.

---

## Mapa de sítios (âncoras VERIFICADAS 2026-07-21)

### Guest — walk / registry

| Papel | Símbolo / EA | Ficheiro:linha (lift macOS) |
|-------|--------------|-----------------------------|
| **Nested walk typemap** | `func_00171244` @ `0x171244` | `gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp:317308` |
| Args do walk | r3=`this`/typemap, r4=stream | entry `:317308+`; r31=r4 stream, r28=r3 this |
| Stream reader | `func_001856A8` @ `0x1856A8` | `ppu_recomp_000.cpp:337827`; probe `[STREAM-OPD]` ~`:337872` |
| Type-map lookup | `func_0018E814` | `ppu_recomp_000.cpp` (~`:347047+`); probe `[TYMAP-LK]` (patch `patch_tymap_18e814.py`) |
| Map usada pelo walk | TOC + `(-0x3A3C)` | lido em `func_00171244` ~`:317344` |
| Map de registo ICGLdr | TOC + `0xD8C` / `0xD90` | `patch_tymap_18e814.py` + `[TYMAP-REG]` |
| **ICGLdr registo** | `func_00321034` (tipo `0xF85F9B1E`) | `ppu_recomp_001.cpp:136187`; `[SHREG]` / `[TYMAP-REG]` |
| **ICGLdrShader body** | `func_0032109C` tag `0x283F6879` | `ppu_recomp_001.cpp:136228` |
| Loader defs SHADERSRC | `func_003CC208` | `ppu_recomp_001.cpp:291118` |
| **Candidato caller natural** (vcall +0x8) | `func_0032E200` @ `0x32E200` | `ppu_recomp_003.cpp:419639`; probes `[CMP-ENTER]` / `[CMP-VCALL]` |
| OPD do walk | `0x522E70` → code `0x171244` | `ppu_loader.cpp:2076–2077`, GATE-FORCE call `:2209` |
| Vtable typemap | `vt @ 0x5130B8`, slot `+8` = OPD walk | runtime note + `patch_32e200_opd.py` header |
| Tipo ICGLdr | `0xF85F9B1E` → OPD body `0x534838` | `ppu_loader.cpp:2076` |
| **Zero callers directos** no lift | só tabela `ppu_recomp_030` + self | walk **só** via OPD/vtable / GATE-FORCE |

### Runtime — GATE-FORCE e OPD (diag only)

| Peça | Âncora |
|------|--------|
| `[OPD-ICG]` em `ps3_call_opd` | `runtime/ppu/ppu_loader.cpp:2052–2069` (loga se code `0x171244` / `0x32109C` ou OPD `0x522E70` / `0x534838`) |
| Nota “never invoked” | `ppu_loader.cpp:2071–2083` |
| `ps3_force_post_wad_icgldr` | `ppu_loader.cpp:2085–2215+` |
| Opt-in gate | `ps3_indirect_call` `:1512–1524` — `PS3_GATE_FORCE=1` **e** `g_ps3_rperma_full` |
| `g_ps3_rperma_full` set | `libs/video/movie_hle.c:616` (após R_PermA full) |
| Component slot GATE-FORCE | `TOC-0x2584` → base; `base+0x130000+0x4980` → component; `component+0xDC` → typemap |
| Fallback component | `0x4306ADF0` (ICG-CTOR prior smoke) |

### Probes / patches já existentes (reusar, reaplicar se lift limpo)

| Env | Tag log | Patch / sítio | Estado lift (2026-07-21) |
|-----|---------|---------------|---------------------------|
| `PS3_TRACE_TYMAP` | `[TYMAP-171]` entry 171244 | `patch_tymap_probes2.py` | **AUSENTE** no entry actual (reaplicar) |
| `PS3_TRACE_TYMAP` | `[TYMAP-VT]` type_tag + opd_slot | inline em 171244 ~`:317335` | **presente** |
| `PS3_TRACE_TYMAP` | `[TYMAP-LK]` map vs d8c/d90/m3a3c | `patch_tymap_18e814.py` | **presente** ~`:347053` |
| `PS3_TRACE_TYMAP`/`SHREG` | `[TYMAP-REG]` / `[SHREG]` | `patch_tymap_18e814.py` em `func_00321034` | verificar no boot |
| `PS3_TRACE_TYMAP`/`LDRSH` | `[CMP-ENTER]` / `[CMP-VCALL]` | `patch_32e200_opd.py` | **presente** `:419639+` |
| `PS3_TRACE_SHADERSRC`/`TYMAP` | `[STREAM-OPD]` | `patch_1856a8_stream_opd.py` | **presente** ~`:337872` |
| `PS3_TRACE_LDRSH` | `[LDRSH]` | patches mid_v2 / body 32109C (confirmar no apply) | revalidar |
| `PS3_GATE_FORCE` | `[GATE-FORCE]` | `ppu_loader.cpp` | **OFF default**; só diag |

### Recipe / boot

| Peça | Path |
|------|------|
| Env base | `gow2-recomp/env_gow2.sh` |
| Binary | `gow2-recomp/boot_gow2` |
| Apply patches | `gow2-recomp/apply_all_patches.sh` (inclui tymap/32e200) |
| Note veredito H2 | `gow2-recomp/notes/2026-07-21-combo-discriminate.md` |

---

## Hipóteses ORDENADAS (prioridade de teste — classes A/B/C do pedido)

Refinamento da H2 (registry vazio) no **elo do walk**. Ordem = barato de refutar primeiro.

### Classe A — componente / typemap com tipo `0xF85F9B1E` nunca construído / walk never scheduled no heap natural

1. **A1 — Caller natural (`func_0032E200` ou equivalente) nunca corre** no recipe actual.  
   *Teste:* `[CMP-ENTER]=0` com `PS3_TRACE_TYMAP=1` em boot natural (e, se possível, pós-WAD).

2. **A2 — Caller corre, mas o objecto `this`/typemap não tem `vt==0x5130B8`** (ou slot +8 ≠ OPD `0x522E70`).  
   *Teste:* `[CMP-ENTER]>0` mas `[CMP-VCALL]` nunca mostra `opd=0x00522E70` / `code=0x00171244`; `[OPD-ICG]=0` para o walk.

3. **A3 — Typemap object nunca instalado** (`component+0xDC==0` ou component slot vazio) no momento em que o pipeline deveria streamar.  
   *Teste:* probe host env-gated (Task 2) dumpa slot TOC/`+0xDC` **sem** chamar o OPD; ou `[GATE-FORCE] wait: no typemap` se alguém ligar force só para *ler* (preferir probe read-only).

### Classe B — objecto existe e o walk é agendado, mas predicado falha (count 0, stream null, early-out)

4. **B1 — Walk invocado com `r4=stream==0` ou stream `end<=cur`** → early-out em `func_001856A8` / retorno sem lookup.  
   *Teste:* `[TYMAP-171]` com r4=0 ou r4 non-zero mas zero type tags lidos; `[TYMAP-VT]` ausente.

5. **B2 — Walk lê type tags, mas **nunca** `0xF85F9B1E`** (stream natural sem esse type; só outros tipos).  
   *Teste:* `[TYMAP-VT]` type_tag ≠ `0xF85F9B1E` em todos os hits; `[TYMAP-LK]` type≠F85F.

6. **B3 — Lookup `func_0018E814` usa mapa errado** (TOC-0x3A3C ≠ mapa onde `0xF85F9B1E` foi registado em d8c/d90).  
   *Teste:* `[TYMAP-REG]` d8c_eq_m3a=0; `[TYMAP-LK]` same_d8c=0 same_m3a=0 no type F85F — hit miss sistemático.

### Classe C — walk + dispatch correm, mas ICGLdr recebe fonte vazia (stream / SHADERSRC)

7. **C1 — `func_0032109C` entra com payload size 0 / N=-1** (registo de tipo ok, bytes de definição ausentes).  
   *Teste:* `[LDRSH]` natural ≥1 mas SHADERSRC N≤0; alinhado a asset-map Task 4 (N=0 pós-WAD).

8. **C2 — SHADERSRC streams `end>cur` mas zero definition records** (já medido Win pós-R_PermA).  
   *Teste:* `[SHADERSTREAM]` / probes SHADERSRC; N=0 com bytes.

### Fora de escopo deste plano (não reabrir sem evidência nova)

- H0/H3 combination (fechados).
- H1 “só WAD-skip causa Invalid Default” (desfavorecida; WAD ainda pode ser pré-requisito de **conteúdo** de SHGX, mas Default residente falha **sem** WAD — prior Win + Mac).
- Fix de pixels / D3D12 / `PS3_SHADER_DEMO`.
- Usar GATE-FORCE para “passar” aceite.

---

## Experimento DISCRIMINADOR barato (1–2 boots)

**Pergunta única:** no caminho **natural** (`PS3_GATE_FORCE` unset), o elo que falha é **(A) walk nunca agendado / typemap ausente**, **(B) walk ou predicado falha**, ou **(C) walk corre mas fonte ICGLdr vazia**?

| Boot | Recipe | Probes ON | O que prova |
|------|--------|-----------|-------------|
| **N1 — Natural baseline** | `env_gow2.sh` + `PS3_MOVIE_EOS=1` `PS3_MOVIE_DONE_MS=auto` `PS3_VDEC_ASYNC=1`; **sem** `PS3_GATE_FORCE`; **sem** FORCE obrigatório se WAD ainda H4 | `PS3_TRACE_TYMAP=1` `PS3_TRACE_LDRSH=1` `PS3_TRACE_SHADERSRC=1` `PS3_TRACE_SHREG=1` | contagens: CMP-ENTER, CMP-VCALL→522E70, TYMAP-171, TYMAP-VT tags, TYMAP-LK F85F, LDRSH, SHADERSRC N, OPD-ICG |
| **N2 — (opcional se N1 sem WAD e quiseres correlacionar stream)** | N1 + caminho que abra `R_PermA` full se disponível (FORCE timing honesto **depois** Open) | mesmos probes; **ainda** `PS3_GATE_FORCE` unset | se N1 e N2 ambos TYMAP-171=0 → **A** independente de WAD (prior Win); se N2 sobe 171 sem LDRSH → **B/C** |

**Custo:** 1 rebuild se reaplicar `patch_tymap_probes2` / ensure entry probe; 1–2 boots × 60–90 s.

**Anti-forja no discriminador:** não setar `PS3_GATE_FORCE`; não injectar stream; não CRC bypass. GATE-FORCE só em **Task diag opcional** separada, etiquetada FORÇADO, para provar mecânica já conhecida — **não** entra no aceite.

### Critério de decisão → rota de fix mínimo FIEL

| Resultado medido (natural) | Classe | Fix mínimo fiel (só após veredito) | **Não** fazer |
|----------------------------|--------|-------------------------------------|---------------|
| `[SHREG]`/`[TYMAP-REG]` ≥1 **e** `[CMP-ENTER]=0` **e** `[TYMAP-171]=0` | **A1** | achar **quem** deveria chamar o component stream / `func_0032E200` (pipeline asset primário / GroupStart / post-intro FSM); fix o **scheduler/gate guest** ou OPD do **caller**, não o walk | GATE-FORCE permanente |
| `[CMP-ENTER]>0`, `[CMP-VCALL]` sem `0x522E70`, `[TYMAP-171]=0` | **A2** | corrigir vtable/OPD resolve no site do vcall (padrão `ps3_call_opd` como em `patch_32e200_opd.py`); auditar se `this` é o typemap certo | forjar vt@5130B8 à mão |
| component/`+0xDC` sempre 0 no momento do pipeline | **A3** | fix ICG-CTOR / install path (OPD ctor já tem `patch_icg_ctor_opd.py`) até typemap live | inventar component em slot |
| `[TYMAP-171]≥1`, r4=0 ou zero tags | **B1** | fix stream object construction no caller (ponteiro/cur/end) | stream sintético no loader como aceite |
| `[TYMAP-171]≥1`, tags ≠ F85F | **B2** | achar stream natural que deveria carregar type F85F (HOSTRES/EBOOT gzip vs WAD SHGX stub — nota runtime); alimentar **bytes reais** | fake tag 0x283F6879 |
| `[TYMAP-LK]` F85F com map≠reg map | **B3** | alinhar mapas (registo e walk no mesmo map) **fielmente** — bug OPD/TOC/init | copiar ponteiros à força sem entender owner |
| `[TYMAP-171]≥1` + dispatch F85F + `[LDRSH]≥1` mas SHADERSRC N≤0 | **C1/C2** | fix stream de defs / `func_001856A8` / HOSTRES inflate path; popular **fonte** real | N=-1 cosmético; CRC bypass |
| Só com `PS3_GATE_FORCE` se obtém TYMAP-171/LDRSH | **parede aberta** | documentar; planear caller natural; **não** marcar [D] vencida | celebrar GATE-FORCE |

---

### Task 0: Releitura + confirmação de âncoras (sem código de fix)

**Files:**
- Read: este plano; `docs/gow2-asset-pipeline-map.md` (Task 3f/4, cadeia open→stream); `docs/gow2-recomp-notes.md` parede [D] ~L97; `docs/superpowers/plans/2026-07-14-gow2-intro-movie-fsm-wads.md` Task 5; `docs/superpowers/plans/2026-07-21-shader-combination-zeroed.md` (H2); `gow2-recomp/notes/2026-07-21-combo-discriminate.md`
- Read: `runtime/ppu/ppu_loader.cpp:1512–1524`, `2052–2215`
- Read (lift): `ppu_recomp_000.cpp:317308+`, `337827+`; `ppu_recomp_001.cpp:136187+`, `136228+`, `291118+`; `ppu_recomp_003.cpp:419639+`

**Interfaces:**
- Produz: checklist de âncoras OK/drift; lista de probes presentes vs a reaplicar

- [ ] **Step 1: Confirmar âncoras no checkout**

```bash
BASE_P=/Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
BASE_G=/Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp

rg -n "GATE-FORCE|OPD-ICG|0xF85F9B1E|func_00171244 never|ps3_force_post_wad_icgldr|PS3_GATE_FORCE" \
  "$BASE_P/runtime/ppu/ppu_loader.cpp" | head -25

rg -n "void func_00171244|void func_001856A8|void func_0032109C|void func_003CC208|void func_00321034|void func_0032E200" \
  "$BASE_G/recomp_macos_v2/ppu_recomp_000.cpp" \
  "$BASE_G/recomp_macos_v2/ppu_recomp_001.cpp" \
  "$BASE_G/recomp_macos_v2/ppu_recomp_003.cpp"

# callers directos do walk (esperado: só definição + tabela 030)
rg -n "func_00171244" "$BASE_G/recomp_macos_v2" --glob 'ppu_recomp_*.cpp' | head -20

# probes presentes
rg -n "TYMAP-171|TYMAP-VT|TYMAP-LK|TYMAP-REG|CMP-ENTER|CMP-VCALL|STREAM-OPD|\[LDRSH\]|\[SHREG\]" \
  "$BASE_G/recomp_macos_v2" --glob 'ppu_recomp_00[013].cpp' | head -40
```

Expected:
- `func_00171244` @ `ppu_recomp_000.cpp:317308` (± drift <50 linhas se patches)
- `func_001856A8` @ `:337827`
- `func_0032109C` @ `001:136228`
- `func_003CC208` @ `001:291118`
- `func_0032E200` @ `003:419639`
- **sem** caller directo além de self + `ppu_recomp_030` table
- `[TYMAP-VT]`, `[TYMAP-LK]`, `[CMP-ENTER]` presentes; `[TYMAP-171]` entry pode estar **ausente** → Task 1

- [ ] **Step 2: Citar priors (não re-correr combination discriminador)**

Documentar em 5 linhas no log de execução / note:
- H2 confirmada; combination válida; registry vazio.
- Asset-map Task 4: SHADERSRC N=0, LDRSH=0 pós-WAD full.
- Runtime: tipo registado, walk never.

- [ ] **Step 3: Commit** — só se actualizares note de âncoras; senão skip

```bash
# opcional
# git add docs/superpowers/plans/2026-07-21-shader-registry-typemap-walk.md
# git commit -m "docs(plan): diagnostico nested walk typemap 00171244"
```

---

### Task 1: Garantir probes de walk (env-gated) + rebuild

**Files:**
- Run: `gow2-recomp/recomp_mid_v2/patch_tymap_probes2.py` (entry `[TYMAP-171]`)
- Confirm: `patch_tymap_18e814.py`, `patch_32e200_opd.py`, `patch_1856a8_stream_opd.py` já aplicados ou via `apply_all_patches.sh`
- Modify (via script only): `recomp_macos_v2/ppu_recomp_000.cpp` entry de `func_00171244`
- **Não** commitar o `.cpp` lift

**Interfaces:**
- Consumes: `PS3_TRACE_TYMAP`, `PS3_TRACE_LDRSH`, `PS3_TRACE_SHADERSRC`, `PS3_TRACE_SHREG`
- Produces stderr (max N caps já no patch):
  - `[TYMAP-171] #n r3=… r4=…`
  - `[CMP-ENTER] #n r3=… r4=…`
  - `[CMP-VCALL] #n opd=… code=…`
  - `[TYMAP-VT]`, `[TYMAP-LK]`, `[STREAM-OPD]`, `[SHREG]`/`[TYMAP-REG]`, `[LDRSH]` se existirem

- [ ] **Step 1: Reaplicar entry probe se ausente**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
python3 recomp_mid_v2/patch_tymap_probes2.py recomp_macos_v2
# ou: python3 recomp_mid_v2/patch_tymap_probes2.py  # ROOT=mid_v2 default — passar recomp_macos_v2
# Verificar:
rg -n "TYMAP-171" recomp_macos_v2/ppu_recomp_000.cpp | head
```

Expected: `TYMAP-171` imediatamente após `void func_00171244`.

Se o script assume `ROOT/ppu_recomp_000.cpp` com ROOT=mid_v2, invocar com o path correcto:

```bash
# patch_tymap_probes2.py: ROOT = argv[1] or parent of script
python3 - <<'PY'
from pathlib import Path
import runpy, sys
sys.argv = ["patch_tymap_probes2.py", str(Path("recomp_macos_v2").resolve())]
# Prefer direct:
import pathlib
p = pathlib.Path("recomp_macos_v2/ppu_recomp_000.cpp")
s = p.read_text(errors="replace")
print("TYMAP-171 already" if "TYMAP-171" in s else "MISSING")
PY
# Aplicação canónica (script espera ROOT com ppu_recomp_*.cpp):
python3 recomp_mid_v2/patch_tymap_probes2.py recomp_macos_v2
```

- [ ] **Step 2: Sanity OFF-default (sem env = zero tags de probe)**

```bash
./build_macos.sh   # rebuild chunks afectados (000 e 003 se necessário)
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  unset PS3_GATE_FORCE
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/tymap_off.log 2>&1 & BPID=$!
sleep 20; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
echo "off_hits=$(grep -cE 'TYMAP-171|CMP-ENTER|TYMAP-LK|LDRSH|GATE-FORCE' /tmp/tymap_off.log || true)"
# expected: 0 (ou só ruído não-probe)
```

- [ ] **Step 3: Commit só scripts se editados**

```bash
# se alteraste patch_*.py para idempotência/path:
# git add recomp_mid_v2/patch_tymap_probes2.py
# git commit -m "diag(shader): garantir probe TYMAP-171 entry env-gated"
# no monorepo: espelhar path games/gow2 se for o canónico de patches
```

---

### Task 2: Probe host read-only do slot typemap (opcional mas barato) — **sem** call OPD

**Files:**
- Modify: `runtime/ppu/ppu_loader.cpp` — novo helper `ps3_trace_typemap_slot(ppu_context*)` **só se** Task 1/3 não bastar para A3
- Gate: `PS3_TRACE_TYMAP_SLOT=1` (nome distinto de GATE-FORCE)

**Interfaces:**
- Lê: TOC, `TOC-0x2584` base, `base+0x130000+0x4980` component, `component+0xDC` typemap, `*typemap` vt
- **Não** escreve stream; **não** chama `0x522E70`
- Log: `[TYMAP-SLOT] #n toc= base= slot= comp= typemap= vt=`

- [ ] **Step 1: Implementar só se N1 não discriminar A3**

```c
/* Pseudocódigo — inserir perto de ps3_force_post_wad_icgldr, NÃO reutilizar s_done do force.
 * Chamado de ps3_indirect_call no máximo 1×/N ticks quando PS3_TRACE_TYMAP_SLOT=1.
 */
static void ps3_trace_typemap_slot(ppu_context* ctx) {
    static int on = -1, n = 0;
    if (on < 0) {
        const char* v = getenv("PS3_TRACE_TYMAP_SLOT");
        on = (v && v[0] && v[0] != '0') ? 1 : 0;
    }
    if (!on || !ctx || n >= 16) return;
    uint32_t toc = (uint32_t)ctx->gpr[2];
    if (!toc) return;
    uint32_t base = vm_read32(toc + (uint32_t)(-0x2584));
    uint32_t slot = base ? base + 0x130000u + 0x4980u : 0;
    uint32_t comp = slot ? vm_read32(slot) : 0;
    uint32_t tm   = comp ? vm_read32(comp + 0xDC) : 0;
    uint32_t vt   = tm ? vm_read32(tm) : 0;
    n++;
    fprintf(stderr,
        "[TYMAP-SLOT] #%d toc=0x%08X base=0x%08X slot=0x%08X comp=0x%08X tm=0x%08X vt=0x%08X\n",
        n, toc, base, slot, comp, tm, vt);
    fflush(stderr);
}
```

Hook em `ps3_indirect_call` **antes** do bloco GATE-FORCE, independente de `g_ps3_rperma_full`:

```c
ps3_trace_typemap_slot(ctx);  /* no-op unless PS3_TRACE_TYMAP_SLOT=1 */
```

- [ ] **Step 2: Rebuild lib runtime + link boot_gow2; OFF sanity**

```bash
cmake --build /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp/build-macos -j"$(sysctl -n hw.ncpu)"
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp && ./build_macos.sh
# OFF: zero [TYMAP-SLOT]
# ON: ≥1 linhas com vt==0x5130B8 ou tm==0
```

- [ ] **Step 3: Commit runtime se implementado**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add runtime/ppu/ppu_loader.cpp
git commit -m "diag(ppu): probe TYMAP-SLOT read-only env-gated (sem call OPD)"
```

---

### Task 3: Boot N1 discriminador natural (núcleo do plano)

**Files:**
- Run only: `boot_gow2` + env; log `/tmp/tymap_N1.log`
- Depends: Task 1 (probes)

**Interfaces:**
- Produz tabela de contagens (abaixo)

- [ ] **Step 1: Boot N1 (~75–90 s)**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  unset PS3_GATE_FORCE
  export PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto PS3_VDEC_ASYNC=1
  # WAD: não forçar GATE; FORCE opcional só se quiseres tentar open R_* (não aceite)
  export PS3_TRACE_TYMAP=1 PS3_TRACE_LDRSH=1 PS3_TRACE_SHADERSRC=1 PS3_TRACE_SHREG=1
  # se Task 2: export PS3_TRACE_TYMAP_SLOT=1
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/tymap_N1.log 2>&1 & BPID=$!
sleep 90; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
```

- [ ] **Step 2: Extrair métricas (script de contagens)**

```bash
LOG=/tmp/tymap_N1.log
echo "=== N1 counts ==="
for p in \
  '\[SHREG\]' '\[TYMAP-REG\]' '\[CMP-ENTER\]' '\[CMP-VCALL\]' \
  '\[TYMAP-171\]' '\[TYMAP-VT\]' '\[TYMAP-LK\]' '\[TYMAP-SLOT\]' \
  '\[STREAM-OPD\]' '\[LDRSH\]' '\[OPD-ICG\]' '\[GATE-FORCE\]' \
  'Invalid shader combination' 'Default\.ps3fx' 'R_PermA' 'R_LglScA'
do
  printf '%s ' "$p"; grep -acE "$p" "$LOG" 2>/dev/null || echo 0
done
echo "=== CMP-VCALL sample (opd/code) ==="
grep -a '\[CMP-VCALL\]' "$LOG" | head -20
echo "=== OPD-ICG sample ==="
grep -a '\[OPD-ICG\]' "$LOG" | head -20
echo "=== TYMAP-171 / VT sample ==="
grep -aE '\[TYMAP-171\]|\[TYMAP-VT\]' "$LOG" | head -20
echo "=== TYMAP-LK type F85F? ==="
grep -a '\[TYMAP-LK\]' "$LOG" | grep -i F85F | head -10 || true
grep -a '\[TYMAP-LK\]' "$LOG" | head -5
echo "=== SHADERSRC / STREAM ==="
grep -aE '\[SHADERSRC\]|\[SHADERSTREAM\]|\[STREAM-OPD\]' "$LOG" | head -15
echo "=== GATE-FORCE must be 0 ==="
grep -ac '\[GATE-FORCE\]' "$LOG" || true
```

- [ ] **Step 3: Classificar na grelha A/B/C**

Preencher (obrigatório):

| Sinal | Valor N1 | Interpretação |
|-------|----------|---------------|
| `[SHREG]` / `[TYMAP-REG]` | | registo tipo vivo? |
| `[CMP-ENTER]` | | caller 32E200 corre? |
| `[CMP-VCALL]` com opd=`0x00522E70` ou code=`0x00171244` | | vcall do walk? |
| `[TYMAP-171]` | | walk natural? |
| `[TYMAP-VT]` type=`0xF85F9B1E` | | tag ICGLdr no stream? |
| `[TYMAP-LK]` hit F85F same_d8c/m3a | | mapa alinhado? |
| `[LDRSH]` natural | | body ICGLdr? |
| `[GATE-FORCE]` | **deve ser 0** | se >0, log contaminado — re-correr |
| `R_PermA` open / bytes | | contexto WAD (não aceite sozinho) |

**Árvore de decisão (usar já):**

```
GATE-FORCE > 0? → INVALIDAR N1; re-boot sem PS3_GATE_FORCE
TYMAP-171 == 0?
  ├─ CMP-ENTER == 0 → A1 (caller nunca corre)
  ├─ CMP-ENTER > 0 e nenhum CMP-VCALL→522E70 → A2 (vcall errado / vt errado)
  └─ TYMAP-SLOT tm==0 (se medido) → A3
TYMAP-171 >= 1?
  ├─ r4==0 ou sem TYMAP-VT → B1
  ├─ TYMAP-VT tags ≠ F85F → B2
  ├─ TYMAP-LK F85F miss / map mismatch → B3
  └─ LDRSH>=1 e SHADERSRC N<=0 → C1/C2
```

- [ ] **Step 4: Note de medição (obrigatório)**

Escrever `gow2-recomp/notes/2026-07-21-typemap-walk-N1.md` com a tabela + classe {A1|A2|A3|B1|B2|B3|C1|C2|misto} + **uma frase** “próximo fix fiel candidato”. Explicitar: **não** é parede [D] vencida.

```bash
# git add notes/2026-07-21-typemap-walk-N1.md
# git commit -m "diag(shader): N1 nested walk typemap — classe A/B/C medida"
```

---

### Task 4: Boot N2 opcional (WAD full natural, ainda sem GATE-FORCE)

**Files:**
- Run only; depende de conseguir `open R_PermA` + `bytes_read→20169344` **sem** forjar walk
- Recipe: plano irmão `2026-07-21-intro-vdec-open-force-wad.md` se Open/FORCE ainda falharem no Mac A3b

**Interfaces:**
- Mesmas contagens que N1 + `g_ps3_rperma_full` / `[movieio]` peak

- [ ] **Step 1: Só se N1 deixou A ambíguo por ausência de stream de asset**

```bash
# Exemplo: FORCE timing honesto APÓS Open — NÃO PS3_GATE_FORCE
(
  set -a; . ./env_gow2.sh; set +a
  unset PS3_GATE_FORCE
  export PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1
  export PS3_VDEC_FORCE_SEQDONE_MS=3000   # ajustar grelha se auto-close cedo
  export PS3_TRACE_TYMAP=1 PS3_TRACE_LDRSH=1 PS3_TRACE_SHADERSRC=1 PS3_TRACE_SHREG=1
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/tymap_N2.log 2>&1 & BPID=$!
sleep 90; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
# contagens iguais a Task 3 Step 2
```

- [ ] **Step 2: Comparar N1 vs N2**

| | N1 | N2 |
|-|----|----|
| R_PermA full | | |
| TYMAP-171 | | |
| LDRSH | | |
| CMP-ENTER | | |

Se ambos TYMAP-171=0 com N2 full → **confirma A** (prior Win Task 4).  
Se N2 sobe 171 sem F85F → **B2**.  
Se N2 sobe LDRSH com N=0 → **C**.

- [ ] **Step 3: Append note** (não substituir N1)

---

### Task 5: Análise estática do caller do vt `@0x5130B8+8` (offline, se A1/A2)

**Files:**
- Read: `ppu_recomp_003.cpp` `func_0032E200` (`:419639+`) — sites `[CMP-VCALL]` / `ps3_call_opd`
- Read: quem trampolina para `func_0032E200` (`g_trampoline_fn = func_0032E200` em `001` ~`:147770`)
- Optional: `nm` / `rg` no EBOOT rodata por vtable `0x5130B8` (offline)

**Interfaces:**
- Produz: grafo de 1–2 níveis “quem chama o vcall do walk”

- [ ] **Step 1: Listar sites de vcall em 32E200 e predicados de early-out**

```bash
# early-out no entry (u8 @ +0x25, contadores, etc.)
sed -n '419639,419780p' recomp_macos_v2/ppu_recomp_003.cpp
# todos os ps3_call_opd / CMP-VCALL no body
rg -n "CMP-VCALL|ps3_call_opd|ps3_indirect_call" recomp_macos_v2/ppu_recomp_003.cpp | head -40
# callers de 32E200
rg -n "func_0032E200" recomp_macos_v2 --glob 'ppu_recomp_*.cpp' | head -20
```

- [ ] **Step 2: Hipótese de predicado (documentar, não fixar ainda)**

Anotar em note:
1. Condição em `func_0032E200` que salta o vcall do typemap (flags, count, null).
2. Quem deve setar esse flag no pipeline de asset (GroupStart / WAD group SHGX_ / HOSTRES).
3. Se o OPD resolve ainda trata code EA como OPD noutro site (padrão já visto em Task 4 asset-map).

- [ ] **Step 3: Experimento discriminador de predicado (só se CMP-ENTER>0 e 171=0)**

Um boot com probe **extra** no site do vcall que dumpa:
- `this` (r3), `*this` vt, `vm_read32(vt+8)` OPD, `vm_read32(opd)` code  
antes do call — gate `PS3_TRACE_TYMAP` já existente; se insuficiente, patch mínimo idempotente `patch_cmp_vcall_vt_dump.py`.

Expected se A2: vt ≠ `0x5130B8` ou OPD ≠ `0x522E70` ou code ≠ `0x171244`.

---

### Task 6: Diag FORÇADO opcional (etiquetado) — **não** aceite

**Files:**
- Run: `PS3_GATE_FORCE=1` **só** se N1/N2 confirmarem A e quiseres re-provar mecânica já documentada no runtime

**Interfaces:**
- Produz: log com `[GATE-FORCE] calling OPD 0x522E70` + `[TYMAP-171]` / `[OPD-ICG]`
- Etiqueta no note: **FORÇADO — não conta para aceite**

- [ ] **Step 1: Um boot force (WAD full pré-requisito do helper)**

```bash
(
  set -a; . ./env_gow2.sh; set +a
  export PS3_GATE_FORCE=1
  export PS3_TRACE_TYMAP=1 PS3_TRACE_LDRSH=1
  # precisa g_ps3_rperma_full=1 → R_PermA completo
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/tymap_FORCE.log 2>&1 & BPID=$!
sleep 90; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
grep -aE 'GATE-FORCE|TYMAP-171|OPD-ICG|LDRSH' /tmp/tymap_FORCE.log | head -40
```

Expected (já prior): force pode atingir walk; LDRSH recognized=1 **só** com payload sintético antigo (código actual usa size=0 — ver `:2174–2180`); **não** popular CRC real.

- [ ] **Step 2: Se force for o *único* caminho que gera TYMAP-171**

Escrever no note:

> **Parede aberta:** nested walk natural inalcançável no recipe actual; GATE-FORCE destrava o OPD mas não é aceite. Próximo trabalho = A1/A2/A3 no caller/scheduler, não “mais force”.

**Não** actualizar CLAUDE.md parede [D] para VENCIDA.

---

### Task 7: Rota de fix mínimo (só a classe vencedora) — **pós-veredito**

**Não começar esta task sem tabela N1 (e N2 se corrida) preenchida.**

| Classe | Files típicos | Fix fiel | Aceite do fix |
|--------|---------------|----------|---------------|
| A1 | guest pipeline / FSM asset / caller de 32E200; possível OPD no caller | fazer o **jogo** agendar o component stream | TYMAP-171 natural ≥1 |
| A2 | lift site vcall + `ps3_call_opd`; patch idempotente | OPD resolve correcto no site | CMP-VCALL→522E70 + TYMAP-171 |
| A3 | ICG-CTOR / `patch_icg_ctor_opd.py` / install +0xDC | typemap live com vt 5130B8 | TYMAP-SLOT tm≠0 + walk natural |
| B1–B3 | stream ctor / map TOC / `func_0018E814` | predicado/stream/map correctos | TYMAP-VT F85F + LK hit |
| C1–C2 | `func_001856A8` / HOSTRES / SHADERSRC defs | bytes de definição reais | SHADERSRC N>0 natural |

- [ ] **Step 1: Um único experimento discriminador do fix (1 boot)**  
  Mesmas métricas N1; **sem** GATE-FORCE.

- [ ] **Step 2: Se o fix não mover TYMAP-171/LDRSH/SHADERSRC**  
  Reverter; voltar a hipóteses — não acumular paliativos.

- [ ] **Step 3: Commit fix + script idempotente** (nunca só o lift `.cpp`)

```bash
# exemplo
# git add recomp_mid_v2/patch_<nome>.py docs/... notes/...
# git commit -m "fix(shader): <classe> — <uma linha do que era fiel>"
```

---

### Task 8: Aceite honesto do plano (fecho)

**PASS deste plano de diagnóstico** (qualquer um, **in-boot natural**, medido):

1. **`[TYMAP-171]` ≥ 1 sem `PS3_GATE_FORCE`** (walk natural), **ou**
2. **`SHADERSRC` N>0 pelo caminho natural** (não sintético), **ou**
3. **`[LDRSH]` natural ≥ 1** (sem GATE-FORCE / sem tag injectada), **ou**
4. **Veredito de classe A/B/C escrito** com tabela N1 (e N2 se aplicável) + rota de fix fiel — mesmo se o fix ainda não existir.

**PASS diagnóstico “parede aberta” (válido):**  
só GATE-FORCE gera walk/LDRSH; documentado explicitamente; [D] **não** vencida.

**FAIL / desonesto (rejeitar):**
- Aceite baseado em `PS3_GATE_FORCE`, stream sintético, CRC bypass, `PS3_SHADER_DEMO`.
- Declarar “registry populado” só com `[SHREG]` (registo de **tipo** ≠ população de **variantes/CRC**).
- Contar Invalid↓ sem N natural de walk/SHADERSRC.

**Não é aceite deste plano:** pixels de jogo / set_shader FP de conteúdo (parede [E] / plano 05).

- [ ] **Step 1: Actualizar índice de walls**

Em `docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md`, satélite:

```markdown
| [`2026-07-21-shader-registry-typemap-walk.md`](2026-07-21-shader-registry-typemap-walk.md) | 🟡 diag | por que 00171244 nunca corre (A/B/C) |
```

- [ ] **Step 2: Actualizar CLAUDE.md / `gow2-recomp-notes` só com factos medidos**  
  Ex.: “N1 2026-07-21: TYMAP-171=0, CMP-ENTER=…, classe A1” — não copiar hipóteses como factos.

- [ ] **Step 3: Commit docs**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add docs/superpowers/plans/2026-07-21-shader-registry-typemap-walk.md \
        docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md
# + notes se no monorepo
git commit -m "docs(plan): diagnostico nested walk typemap 00171244 (A/B/C)"
```

---

## Ordem de execução (checklist rápido)

```
Task 0  âncoras + priors (sem código)
Task 1  probes TYMAP-171 + rebuild + OFF sanity
Task 2  (opcional) TYMAP-SLOT read-only
Task 3  Boot N1 → tabela → classe A/B/C     ← núcleo
Task 4  Boot N2 se WAD necessário para desambiguar
Task 5  estática caller 32E200 se A1/A2
Task 6  GATE-FORCE opcional, etiquetado FORÇADO
Task 7  fix mínimo só da classe vencedora
Task 8  fecho honesto + índice
```

## Relação com planos irmãos

| Plano | Relação |
|-------|---------|
| `2026-07-21-shader-combination-zeroed.md` | **pai**: fechou H2 (registry vazio); este plano **abre** o elo do walk |
| `2026-07-14-gow2-intro-movie-fsm-wads.md` Task 5 | classificação Win CRC-miss; “nenhum fix honesto” → este plano aprofunda o *porquê* do walk |
| `docs/gow2-asset-pipeline-map.md` Task 4 | SHADERSRC N=0 / LDRSH=0 pós-WAD; jump table TYDISP; OPD recovery |
| `2026-07-21-intro-vdec-open-force-wad.md` | se N2 precisar de R_PermA no Mac A3b |
| `2026-07-20-05-rsx-shaders-pixels.md` | **depois** de registry natural; pixels ≠ este plano |

## Anti-padrões (checklist mental)

- [ ] Não marcar [D] vencida com GATE-FORCE.
- [ ] Não confundir `[SHREG]` (regista **tipo** OPD) com registry de **combinations/CRC**.
- [ ] Não “fixar” `USE_PRECALCULATED_SHADER_CRCS` desligando o check.
- [ ] Não stream sintético no `ppu_loader` como aceite.
- [ ] Não reabrir H0/H1/H3 de combination sem evidência nova.
- [ ] Não `git add` `ppu_recomp_*.cpp` / EBOOT / WAD.
- [ ] Não `pkill -f boot_gow2`.

---

## Self-review do plano (cobertura do pedido)

| Pedido do user | Task |
|----------------|------|
| (1) Quem devia invocar o walk + provar in-boot se corre e com args | Task 1 probes + Task 3 CMP-ENTER/VCALL/TYMAP-171 + Task 5 estática |
| (2) Discriminador A/B/C barato 1–2 boots | Task 3 N1 (+ Task 4 N2) + grelha de decisão |
| (3) Critério → fix mínimo fiel por classe | secção “Critério de decisão” + Task 7 |
| (4) Aceite natural ≥1 walk / SHADERSRC N>0 / LDRSH natural; **não** GATE-FORCE | Task 8 |
| Citar asset-map, recomp-notes [D], Task 5, GATE-FORCE 2058–2196 | secção NÃO redo + âncoras |
| Anti-forja / parede aberta se só force | Global Constraints + Task 6/8 |
