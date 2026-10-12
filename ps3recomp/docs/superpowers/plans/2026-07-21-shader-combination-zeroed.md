# Default.ps3fx combination “zerada” — plano DIAGNÓSTICO-PRIMEIRO

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Descobrir, com medição in-boot e um experimento discriminador barato (1–2 boots), **por que** o guest imprime  
`Invalid shader combination: $/enginesupport/shaders/PS3/Default.ps3fx (TEXTURE=0;CONSTCOLOR=1;HASCOLOR=0;TRANSFORM=0)`  
e qual hipótese é verdadeira: **(H1) WAD-skip (R_PermA nunca carregado)** vs **(H2) registry/typemap não popula mesmo com assets** — sem forjar CRC, stub de combination, magic, nem `PS3_SHADER_DEMO` como aceite.

**Architecture:** O spam é do **próprio guest (CGOWShader)**, não do runtime. O runtime só observa (probes env-gated). A cadeia guest verificada no lift macOS é: material object → `func_00168350` (lê props) → `func_001F2AD4` (formata a string `TEXTURE=%d;…`) → `func_001655F0` (lookup/CRC) → mensagem em `0x4C5FBF`. O plano **re-mede no estado A3b 2026-07-21** (filme auto-completa, WADs pulados) e **confronta** com um boot que força o caminho WAD — sem reabrir o megaprojeto de inflate a cegas.

**Tech Stack:** `boot_gow2` (gow2-recomp), env `PS3_MOVIE_*` / `PS3_VDEC_*`, probes em `runtime/ppu/ppu_loader.cpp` + patch idempotente no lift (`recomp_macos_v2`, gitignored) via `patch_*.py` em `gow2-recomp/recomp_mid_v2` ou espelho `games/gow2/recomp_mid_v2`.

## Global Constraints

- Metodologia: evidência → hipóteses **ordenadas** → experimento **discriminador** → refutações **medidas** → fix mínimo (só depois do veredito).
- Probes **sempre** env-gated, **OFF por default**, no-op no baseline.
- **Proibido como “fix”:** CRC bypass, stub de combination/shader, estampar magic à mão, null-check cosmético, mascarar guard, `PS3_SHADER_DEMO` como aceite de conteúdo.
- Aceites distinguem **in-boot medido** vs offline vs não-exercitado.
- Nunca forjar `st620` / `obj+0x744` só para “passar” o filme; reabrir WAD só via timing honesto (FORCE pós-Open) ou policy documentada.
- Commits em PT, paths explícitos, sem dados de jogo, sem co-authorship.
- Matar boots por **PID** (`TERM` → `-9`), nunca `pkill -f boot_gow2`.
- Não redo: ler e **citar** resultados já medidos (abaixo). Diagnóstico honesto = entrega válida.

## NÃO redo — factos já medidos (base)

| Facto | Evidência | Implicação |
|-------|-----------|------------|
| Mensagem é guest CGOWShader | texto TTY + `PS3_TRACE_SHADERMSG` / format `0x4C5FBF` | não “fixar” o printf host |
| Format de erro guest VA `0x4C5FBF` | `EBOOT.ELF` off `0x4b5fbf` + LOAD `0x10000`; `ppu_loader.cpp:537–545` | `PS3_TRACE_SHADERFMT` |
| Format de combo `TEXTURE=%d;CONSTCOLOR=%d;HASCOLOR=%d;TRANSFORM=%d` guest VA **`0x4C61B0`** | `EBOOT.ELF` off `0x4b61b0` | string **válida**, não lixo |
| Ponteiro de format em **`[TOC-0x3ACC]` = live `0x53D6AC`**, ELF `0x4C61B0` | `tblsize_guard` `ppu_loader.cpp:808–818`; se zero → “combination degenerates to zeros” | guard já bloqueia zero-write; **≠** “props todas 0” |
| Classificação Win (bt_class5 / e947165): **CRC-miss com string VÁLIDA**; registry de variantes vazio; `ICGLdrShader` sem callers diretos; `func_003CC208` fonte vazia | plano `2026-07-14-gow2-intro-movie-fsm-wads.md` Task 5; `docs/gow2-recomp-notes.md` parede [D] | “zerada” no log **não** significa “sprintf falhou” |
| Paredes **shader e WAD desacopladas** (Windows, recipe intro+WAD): 1º Invalid **antes** de `R_PermA`; taxa invalid/flip ~igual antes/depois; shaders `$/enginesupport/` **residentes** | mesmo plano Task 5 Step 1 | **H1 fraca a priori** para *Default.ps3fx* |
| Pós-R_PermA full (20 169 344 B): `SHADERSRC` N=0 (records vazios); `LDRSH=0` natural; nested walk typemap aberto | `docs/gow2-asset-pipeline-map.md` Task 3f/4 | **H2 já vista com WAD** |
| `LDRSH recognized=1` só com stream **sintético** + tag `0x283F6879` (GATE-FORCE) | `ppu_loader.cpp:2174–2178` | não é aceite natural |
| Milestone A3b (2026-07-21): filme auto-completa → **WADs legais nunca pedidos** | user + `movie_eos_arm.c` A3b | estado **novo** — re-medir, não assumir Win |

## Mapa de sítios (âncoras VERIFICADAS 2026-07-21)

### Guest — composição da combination (CGOWShader path)

| Papel | Símbolo / EA guest | Ficheiro:linha (lift macOS) |
|-------|--------------------|-----------------------------|
| **Builder de combination** | `func_00168350` @ `0x168350` (família fragments `0x1683C4`…`0x168440`) | `gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp:308336` (cópia fragment em `ppu_recomp_002.cpp:41795+`) |
| Material `this` | `r3` → `r29` | entrada `func_00168350` |
| **TEXTURE** (u8) | `vm_read8(mat+0x90)` → arg r6 do format | `…/ppu_recomp_000.cpp:308401` (`loc_00168440`) |
| **HASCOLOR** (u8) | `vm_read8(mat+0x99)` → arg r7 | mesma linha +1 |
| **CONSTCOLOR / bit** | derivado de `vm_read32(mat+0x78)` + sign-fold → arg r4 | `…:308398–308405` |
| Gate extra | `vm_read8(mat+0x7C)` | `…:308388` |
| Mode switch | `vm_read32(mat+0x94)` (1/2/3…) | `…:308363` |
| Format ptr | `vm_read32(TOC + -0x3ACC)` → deve ser `0x4C61B0` | `…:308403`; TOC slot live **`0x53D6AC`** |
| **sprintf guest** | `func_001F2AD4` @ `0x1F2AD4` (lê format, emite `%` / `0x25`) | `…/ppu_recomp_000.cpp:443997` |
| **Lookup pós-string** | `func_001655F0` @ `0x1655F0` | `…/ppu_recomp_000.cpp:305255` |
| Erro “Invalid shader combination” | rodata `0x4C5FBF` | `ppu_loader.cpp:544–545` (`SHADERFMT_LO/HI`) |
| Mensagem precalc | string `USE_PRECALCULATED_SHADER_CRCS in CGOWShader.cpp]` | `EBOOT.ELF` off `0x4b656d` |

### Guest — população de registry (cadeia WAD / typemap — H2)

| Papel | Símbolo | Âncora |
|-------|---------|--------|
| Stream reader (SHADERSRC/ICGLdr) | `func_001856A8` | patches `patch_1856a8_stream_opd.py`; probe `[STREAM-OPD]` em `ppu_recomp_000.cpp:337850` |
| Loader de defs SHADERSRC | `func_003CC208` | `ppu_recomp_001.cpp:291118` |
| **ICGLdrShader body** | `func_0032109C` (tag `0x283F6879`) | `ppu_recomp_001.cpp:136228` |
| Nested walk / typemap | `func_00171244` OPD `0x522E70`, type `0xF85F9B1E` | `ppu_loader.cpp:2071–2077`, `2058–2061` `[OPD-ICG]` |
| GATE-FORCE (diag only) | `ps3_force_post_wad_icgldr` | `ppu_loader.cpp:2085–2196` — **não é aceite** |

### Runtime — probes já existentes (reusar)

| Env | Onde | O que mede |
|-----|------|------------|
| `PS3_TRACE_SHADERMSG` | `ppu_loader.cpp:1819–1861` (sys_tty_write 403) | rolling “Invalid shader…” + stack (Win) |
| `PS3_TRACE_SHADERFMT` | `ppu_loader.cpp:537–570` | hit no format `0x4C5FBF` (stack Win only) |
| `PS3_TRACE_SHADERSRC` | `ppu_loader.cpp:665–688` + patches lift | `[SHADERSTREAM]` cur/end/bytes; streams vazios |
| `PS3_TRACE_SHADERVEC` | `ppu_loader.cpp:1281–1291` | containers `0x4306C700` / `0x4306C100` |
| `PS3_TRACE_LDRSH` / `PS3_TRACE_TYMAP` | patches mid_v2 / macos lift | `[LDRSH]`, `[TYMAP-*]`, `[CMP-VCALL]` |
| `tblsize_guard` @ `0x53D6AC` | `ppu_loader.cpp:808–818` | `[FIX] blocked spurious zero at [0x53D6AC]` |

### Movie / WAD skip (contexto A3b)

| Peça | Âncora |
|------|--------|
| Arm EOS só `st620 >= 5` | `gow2-recomp/movie_eos_arm.c:133–145` |
| A3b audio-stream-complete (desbloqueia 3→4) | `movie_eos_arm.c:148–160`, sampler ~`:519+` |
| FORCE SEQDONE latch em `cellVdecStartSeq` | plano `2026-07-21-intro-vdec-open-force-wad.md`; `libs/codec/cellVdec.c` |
| Recipe env | `gow2-recomp/env_gow2.sh` |

## Hipóteses ORDENADAS (prioridade de teste)

1. **H0 — Pointer de format/material system zero (regredir `tblsize_guard`)**  
   Se `[0x53D6AC]==0` no momento do Invalid, a string “degenera”. Observado actual tem `CONSTCOLOR=1` ⇒ **improvável**, mas barato de fechar.

2. **H1 — WAD-skip (A3b):** props / registry vazios porque `R_LglScA`/`R_PermA` nunca abrem; material Default “defaulta” a 0/1/0/0 e CRC table não existe.  
   *Nota:* evidência Win **desacopla** Default.ps3fx do WAD; ainda assim o estado Mac pós-A3b **nunca** re-mediu o A/B. Discriminador abaixo.

3. **H2 — Registry não popula (mesmo com WAD):** nested walk typemap / SHADERSRC N=0 / ICGLdr não invocado no caminho natural → CRC miss com string **válida**.  
   Já observado pós-stream-full no Win; candidato **mais forte** se Boot B (com WAD) não mudar o spam Default.

4. **H3 — Resolve errado (OPD/TOC/stream):** `func_001F2AD4` ou `func_001655F0` leem ponteiros errados (OPD como code, stream base lixo) → props “parecem” zero ou CRC sempre miss.  
   Discriminar com dump de `mat+0x90/99/78` **antes** do sprintf vs string emitida.

5. **H4 — Sub-hipótese operacional:** reabrir WAD (FORCE timing / impedir auto-skip cedo) é **pré-requisito de conteúdo** (texturas/SHGX) **mesmo se** H1 for refutada para Default.ps3fx. Não confundir “pixels de jogo” com “Invalid some”.

## Experimento DISCRIMINADOR (1–2 boots)

**Pergunta única:** o boot com `R_PermA` consumido muda a combination Default.ps3fx / taxa de Invalid **em relação** ao boot A3b sem WAD?

| Boot | Recipe | O que prova |
|------|--------|-------------|
| **A — WAD-skip (estado actual)** | `PS3_MOVIE_EOS=1` `PS3_MOVIE_DONE_MS=auto` `PS3_VDEC_ASYNC=1`; **sem** FORCE curto; probes de combo ON | baseline: props, first-invalid frame, `open R_*=0` |
| **B — WAD forçado** | mesmo base + `PS3_VDEC_FORCE_SEQDONE_MS` baixo o bastante para FORCE **depois** de Open/StartSeq mas **antes** do auto-close “longo”; ou recipe Win `bt_intro_wads.sh` equivalente Mac se Open já corre | se `open R_PermA` + `bytes_read→20M` e **mesma** combo Default + invalid/flip ≈ A ⇒ **H1 refutada** para Default; se combo/invalid **muda** ⇒ H1 (parcial) viva |

**Custo:** 2 boots × ~45–75 s. Sem rebuild se só env; 1 rebuild se Task 2 (probe novo).

**Critério de decisão (roteamento):**

| Resultado medido | Veredito | Próximo trabalho |
|------------------|----------|------------------|
| Boot A e B: mesma string Default `(0;1;0;0)`, invalid/flip ±10%, e B tem `R_PermA` full | **H1 refutada** para Default.ps3fx | **H2**: nested walk typemap / SHADERSRC N>0 natural (`patch_tymap_*`, `func_00171244`) |
| Boot B: combo Default **muda** (ex. TEXTURE=1) **ou** invalid/min cai ≥50% **e** correlaciona com pós-WAD | **H1 viva** | reabrir/estabilizar caminho WAD (FORCE/timing); depois ainda medir registry |
| Boot A: `[0x53D6AC]==0` ou `[FIX] blocked zero` + string lixo | **H0** | reforçar guard / achar zero-writer (não CRC bypass) |
| Props no mat ≠ args do sprintf / TOC format ≠ `0x4C61B0` | **H3** | fix OPD/TOC/stream (fiel), não stub |
| B: WAD full mas `LDRSH=0` e `SHADERSRC` N=0 | **H2 confirmada com asset** | walk typemap (Task 4 natural do CLAUDE.md) — **não** “só carregar WAD de novo” |

**Anti-forja no discriminador:** não usar `PS3_SHADER_CRC_BYPASS`, não estampar mat+0x90, não GATE-FORCE como aceite, não contar `PS3_SHADER_DEMO`.

---

### Task 0: Releitura + baseline de contagens (sem código)

**Files:**
- Read: este plano; `docs/gow2-asset-pipeline-map.md` (Task 3f/4); `docs/gow2-recomp-notes.md` parede [D]; `runtime/ppu/ppu_loader.cpp:537–818`, `1819–1861`, `2071–2196`; `movie_eos_arm.c:133–160`
- Read (lift, não editar ainda): `gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp:308336–308412`, `443997`, `305255`

**Interfaces:**
- Produz: tabela de contagens Boot A em `/tmp/combo_bootA.log` (ou note local)

- [ ] **Step 1: Confirmar âncoras no teu checkout**

```bash
# motor
rg -n "SHADERFMT_LO|0x53D6AC|Invalid shader|GATE-FORCE|0x283F6879" \
  /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp/runtime/ppu/ppu_loader.cpp | head -30
# lift
rg -n "void func_00168350|void func_001F2AD4|void func_001655F0|void func_0032109C" \
  /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp \
  /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp | head
```

Expected: linhas compatíveis com a tabela de âncoras (± drift de patches; se drift >50 linhas, **actualizar âncoras no plano** antes de patch).

- [ ] **Step 2: Boot A — estado actual (WAD-skip), probes leves**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
./build_macos.sh   # se binário stale
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto PS3_VDEC_ASYNC=1
  unset PS3_VDEC_FORCE_SEQDONE_MS
  export PS3_TRACE_SHADERMSG=1
  # opcional se patches aplicados: PS3_TRACE_SHADERSRC=1 PS3_TRACE_LDRSH=1
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/combo_bootA.log 2>&1 & BPID=$!
sleep 75; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null; wait $BPID 2>/dev/null
```

- [ ] **Step 3: Extrair métricas Boot A**

```bash
LOG=/tmp/combo_bootA.log
echo "invalid=$(grep -ac 'Invalid shader combination' "$LOG")"
echo "default_combo=$(grep -ac 'Default.ps3fx' "$LOG")"
echo "combo_sample:"; grep -a 'Invalid shader combination' "$LOG" | head -3
echo "FIXzero53=$(grep -ac 'blocked spurious zero at \[0x53D6AC\]' "$LOG")"
echo "r_lgl=$(grep -aciE \"R_LglScA\" "$LOG")"
echo "r_perm=$(grep -aciE \"R_PermA\" "$LOG")"
echo "st620:"; grep -a '\[MOVIEFSM\] st620' "$LOG" | tail -15
echo "openex=$(grep -ac 'cellVdec.*Open\|\[cellVdec\] Open' "$LOG")"
echo "force=$(grep -ac 'FORCE SEQDONE' "$LOG")"
echo "flips=$(grep -acE 'SetFlipCommand|flip' "$LOG" | head -1)"
echo "LDRSH=$(grep -ac '\[LDRSH\]' "$LOG")"
echo "SHADERSRC=$(grep -acE '\[SHADERSRC\]|\[SHADERSTREAM\]' "$LOG")"
```

Expected **documentado** (não “pass”): `r_perm==0` (skip), `invalid` alto, sample com `TEXTURE=0;CONSTCOLOR=1;HASCOLOR=0;TRANSFORM=0`, st620 → 11/Close.

- [ ] **Step 4: Commit opcional** — só note de medição (sem dados de jogo)

```bash
# se quiseres: append 10 linhas a gow2-recomp/notes/ com paths de log e contagens
# git add notes/... && git commit -m "diag(shader): baseline Boot A WAD-skip combination Default.ps3fx"
```

---

### Task 1: Localizar + documentar leituras de material (offline no lift)

**Files:**
- Read: `gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp:308336–308412`
- Optional note: `gow2-recomp/notes/2026-07-21-combo-material-offsets.md` (create)

**Interfaces:**
- Produz: mapa `mat+off → nome hipotético → arg sprintf` (abaixo, já preenchido; **confirmar** no teu ficheiro)

| Offset mat | Acesso | Arg para `func_001F2AD4` | Nome na string (hipótese → confirmar com probe) |
|------------|--------|--------------------------|--------------------------------------------------|
| `+0x90` | u8 | r6 | **TEXTURE** |
| `+0x99` | u8 | r7 | **HASCOLOR** |
| `+0x78` | u32 → sign-fold bit | r4 | **CONSTCOLOR** (ou TRANSFORM — ver ordem args) |
| sign-fold de word em r11 no path | r5 | **TRANSFORM** (ordem format: T,C,H,Tr) |
| `+0x7C` | u8 gate | — | escolhe path de bit |
| `+0x94` | u32 mode | — | variante de builder |

Ordem do format string `TEXTURE=%d;CONSTCOLOR=%d;HASCOLOR=%d;TRANSFORM=%d` ⇒ args sequenciais após format ptr:  
`func_00168350` passa r3=format, r4=…, r5=…, r6=`+0x90`, r7=`+0x99` em `loc_00168440` — **Task 2 valida** o mapeamento exacto dumpando os 4 inteiros + string.

- [ ] **Step 1: Diff visual do epílogo `loc_00168440` no teu lift**

```bash
sed -n '308390,308412p' recomp_macos_v2/ppu_recomp_000.cpp
```

Expected: `vm_read8(+0x99)`, `vm_read8(+0x90)`, `vm_read32(TOC-0x3ACC)`, `func_001F2AD4`, `func_001655F0`.

- [ ] **Step 2: Confirmar rodata no EBOOT (sem commitar ELF)**

```bash
python3 - <<'PY'
from pathlib import Path
d=Path('EBOOT.ELF').read_bytes()
for s in [b'TEXTURE=%d;CONSTCOLOR=%d;HASCOLOR=%d;TRANSFORM=%d', b'Invalid shader combination']:
    i=d.find(s); print(s[:40], 'off', hex(i), 'VA', hex(i+0x10000) if i>=0 else None)
PY
```

Expected: VA `0x4c61b0` e `0x4c5fbf`.

- [ ] **Step 3: Commit note (opcional)**

```bash
git add notes/2026-07-21-combo-material-offsets.md
git commit -m "diag(shader): mapa mat+0x90/99/78 -> Default.ps3fx combination"
```

---

### Task 2: Sonda `PS3_TRACE_COMBOPROP` (env-gated, OFF default)

**Files:**
- Create: `gow2-recomp/recomp_mid_v2/patch_comboprop_168350.py` (idempotente; aplicar em `recomp_macos_v2`)
- Modify (via script only): `recomp_macos_v2/ppu_recomp_000.cpp` entry de `func_00168350` e/ou imediatamente **antes** de `func_001F2AD4` em `loc_00168440` (~`:308403`)
- Optional mirror: `ps3recomp/games/gow2/recomp_mid_v2/patch_comboprop_168350.py` no monorepo se for o path canónico de patches

**Interfaces:**
- Consumes: material EA em `r29`/`r3`, TOC em `r2`
- Produces (stderr, max N=32):  
  `[COMBOPROP] #n mat=0x.. fmt=0x.. t= u8@+90 c_bit= h= u8@+99 tr_bit= mode@+94= toc53D6AC=0x..`
- Gate: `getenv("PS3_TRACE_COMBOPROP")` only

- [ ] **Step 1: Escrever patch idempotente**

```python
#!/usr/bin/env python3
"""PS3_TRACE_COMBOPROP — dump material props at CGOWShader combination build.

Anchor: func_00168350 loc_00168440, before func_001F2AD4.
Idempotent: marker COMBOPROP-PROBE.
"""
from pathlib import Path
import sys

MARKER = "COMBOPROP-PROBE"
PROBE = r'''
        { static int on=-1; if(on<0){extern char* getenv(const char*); on=getenv("PS3_TRACE_COMBOPROP")?1:0;}
          if(on){ static int n=0; if(n++<32){
            uint32_t mat=(uint32_t)ctx->gpr[29];
            uint32_t fmt=vm_read32((uint32_t)ctx->gpr[2] + (uint32_t)(-0x3ACC));
            uint8_t t=mat?vm_read8(mat+0x90):0xFF, h=mat?vm_read8(mat+0x99):0xFF;
            uint32_t m78=mat?vm_read32(mat+0x78):0, mode=mat?vm_read32(mat+0x94):0;
            uint8_t g7c=mat?vm_read8(mat+0x7C):0xFF;
            fprintf(stderr,"[COMBOPROP] #%d mat=0x%08X fmt=0x%08X TEXTURE@+90=%u HASCOLOR@+99=%u word@+78=0x%08X gate@+7C=%u mode@+94=%u\n",
              n,mat,fmt,(unsigned)t,(unsigned)h,m78,(unsigned)g7c,mode); fflush(stderr);} } }
'''

def main(path: Path) -> int:
    s = path.read_text(errors="ignore")
    if MARKER in s:
        print("already applied", path); return 0
    needle = "ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x3ACC);\n        ctx->gpr[4] = ctx->gpr[9] - ctx->gpr[4];"
    # Prefer unique site inside func_00168350 — if multiple fragments, replace_all with care
    if needle not in s:
        print("needle not found", path); return 1
    # Insert AFTER reading fmt into r3 is fine; insert BEFORE the read for true pre-state:
    insert_at = "        ctx->gpr[6] = vm_read8(ctx->gpr[29] + 0x90);\n"
    if insert_at not in s:
        print("insert point not found"); return 1
    # Only first occurrence in file that is in 00168350 family — use count
    parts = s.split(insert_at)
    if len(parts) < 2:
        return 1
    # patch first N fragment copies (cap 6) — all share same semantics
    out = parts[0]
    for i, p in enumerate(parts[1:]):
        if i < 6:
            out += insert_at + f"        /* {MARKER} */" + PROBE
        else:
            out += insert_at
        out += p
    path.write_text(out)
    print("patched", path)
    return 0

if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1] if len(sys.argv)>1 else
        "recomp_macos_v2/ppu_recomp_000.cpp")))
```

- [ ] **Step 2: Aplicar + rebuild chunk 000 + link boot**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
python3 recomp_mid_v2/patch_comboprop_168350.py recomp_macos_v2/ppu_recomp_000.cpp
# se fragments também em 002:
python3 recomp_mid_v2/patch_comboprop_168350.py recomp_macos_v2/ppu_recomp_002.cpp || true
./build_macos.sh
```

Expected: link OK; binário sobe.

- [ ] **Step 3: Boot A com sonda (OFF-default sanity: sem env = zero linhas)**

```bash
# sanity OFF
( set -a; . ./env_gow2.sh; set +a
  unset PS3_TRACE_COMBOPROP
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/combo_off.log 2>&1 & BPID=$!
sleep 15; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
test "$(grep -c COMBOPROP /tmp/combo_off.log || true)" -eq 0 && echo OFF_OK

# ON
( set -a; . ./env_gow2.sh; set +a
  export PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto PS3_VDEC_ASYNC=1
  export PS3_TRACE_COMBOPROP=1 PS3_TRACE_SHADERMSG=1
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/combo_propA.log 2>&1 & BPID=$!
sleep 60; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
grep -a '\[COMBOPROP\]' /tmp/combo_propA.log | head -10
grep -a 'Invalid shader combination' /tmp/combo_propA.log | head -3
```

Expected ON: `fmt=0x4C61B0` (não 0); `TEXTURE@+90` e `HASCOLOR@+99` batem com a string guest; `mat≠0`.

**Interpretação imediata:**

| Observação | Significado |
|------------|-------------|
| `fmt==0` | H0 — pointer system morto (apesar do guard?) |
| `fmt==0x4C61B0`, props = 0/1/0/0, mat heap válido | **leituras reais**, não “resolve lixo”; H3 enfraquecida |
| `mat` em stack com bytes todos 0 **excepto** o que a string mostra | material default residente — falta **variante CRC/registry**, não WAD field |
| props no mat **≠** dígitos na mensagem | H3 (sprintf/arg order/corruption) |

- [ ] **Step 4: Commit script only (não o .cpp liftado)**

```bash
git add recomp_mid_v2/patch_comboprop_168350.py
# se monorepo: games/gow2/recomp_mid_v2/patch_comboprop_168350.py
git commit -m "diag(shader): probe PS3_TRACE_COMBOPROP em func_00168350"
```

---

### Task 3: Boot B — discriminador WAD vs no-WAD

**Files:**
- Scripts: reutilizar `bt_intro_wads.sh` (Win) / criar `gow2-recomp/smoke_combo_discriminate.sh` (Mac)
- Depende: OpenEx+StartSeq já a correr no estado A3b (milestone 2026-07-21); se Open=0, **primeiro** fechar `2026-07-21-intro-vdec-open-force-wad.md`

**Interfaces:**
- Boot B MUST: `open 'R_PermA'` ≥1 e preferencialmente `bytes_read` → ficheiro completo (20 169 344) se fallthrough 2550C8 + stream OK
- Mesmas probes: `PS3_TRACE_COMBOPROP=1` (amostra pós-WAD)

- [ ] **Step 1: Recipe Boot B (Mac) — FORCE após Open, sem forjar st620**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1
  # DONE_MS curto o suficiente para producer, MAS arm policy exige st>=5
  # A3b já leva 3→4→Open; FORCE precisa de StartSeq:
  export PS3_MOVIE_DONE_MS=auto
  export PS3_VDEC_FORCE_SEQDONE_MS=3000   # afinar: deve logar FORCE e open R_*
  export PS3_TRACE_COMBOPROP=1 PS3_TRACE_SHADERMSG=1
  export PS3_TRACE_SHADERSRC=1 PS3_TRACE_LDRSH=1   # se patches no lift
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/combo_bootB.log 2>&1 & BPID=$!
sleep 90; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
```

Se `R_PermA` **não** abrir: documentar fail operacional (timing FORCE / auto-close ainda cedo) → **H4 sub-plano** (Task 5), **não** declarar H1/H2.

- [ ] **Step 2: Tabela de decisão (preencher com números reais)**

```bash
for L in A:/tmp/combo_bootA.log B:/tmp/combo_bootB.log; do
  tag=${L%%:*}; f=${L#*:}
  echo "=== Boot $tag ==="
  echo "invalid=$(grep -ac 'Invalid shader combination' "$f" 2>/dev/null || echo NA)"
  echo "sample=$(grep -a 'Invalid shader combination' "$f" 2>/dev/null | head -1)"
  echo "r_perm=$(grep -aciE 'R_PermA' "$f" 2>/dev/null || echo 0)"
  echo "comboprop=$(grep -ac '\[COMBOPROP\]' "$f" 2>/dev/null || echo 0)"
  echo "ldrsh=$(grep -ac '\[LDRSH\]' "$f" 2>/dev/null || echo 0)"
  echo "shadersrc_n_ok=$(grep -acE '\[SHADERSRC\].*N=[0-9]' "$f" 2>/dev/null || echo 0)"
done
```

- [ ] **Step 3: Aplicar critério de decisão (secção acima) — uma frase de veredito**

Template:

```text
VEREDITO: H1 {refutada|viva|inconclusiva} | H2 {reforçada|enfraquecida} | H0/H3 {…}
ROTA: { reabrir WAD estável | nested walk typemap | fix OPD/TOC | reforçar tblsize }
```

- [ ] **Step 4: Commit note de veredito**

```bash
# gow2-recomp/notes/2026-07-21-combo-discriminate.md com a tabela preenchida
git add notes/2026-07-21-combo-discriminate.md
git commit -m "diag(shader): discriminador WAD-skip vs registry Default.ps3fx"
```

---

### Task 4: Se H2 — sondas de registry (só após veredito)

**Não executar se Task 3 não fechou H1/H2.**  
**Files:**
- Existing: `patch_tymap_18e814.py`, `patch_1856a8_stream_opd.py`, `patch_14b1f0_opd.py`, `apply_all_patches.sh`
- Runtime: `ppu_loader.cpp` SHADERSTREAM / GATE-FORCE (**GATE-FORCE = diag only**)

- [ ] **Step 1: Boot com typemap/LDRSH (WAD full se possível)**

```bash
export PS3_TRACE_TYMAP=1 PS3_TRACE_LDRSH=1 PS3_TRACE_SHADERSRC=1
# recipe Boot B estável
# expected H2: TYMAP-171=0 ou rare; LDRSH=0 natural; SHADERSTREAM end>cur mas bytes@cur zeros / N=0
```

- [ ] **Step 2: Critério de progresso registry (honesto)**

| Sinal | Aceite parcial |
|-------|----------------|
| `[LDRSH]` natural `recognized=1` **sem** GATE-FORCE | in-boot path ICGLdr |
| `[SHADERSRC]` **N>0** e fonte não-vazia | defs presentes |
| 1× `Invalid` com combination **≠** só Default zeros **ou** invalid/min queda ≥50% **sem** CRC bypass | lookup a melhorar |
| FP de jogo em set_shader | **fora** deste plano (Plano 05); não forjar |

- [ ] **Step 3: Se só GATE-FORCE dá LDRSH=1** — documentar “path forçado ≠ natural”; **não** marcar parede D vencida.

---

### Task 5: Sub-plano H4 — reabrir caminho WAD (se Boot B falhou open)

**Files:**
- `docs/superpowers/plans/2026-07-21-intro-vdec-open-force-wad.md` (activo)
- `movie_eos_arm.c` (A3b + arm st≥5)
- **Não** forjar `+0x744` / st620

- [ ] **Step 1: Classificar por que WAD não abre no Mac pós-A3b**

Hipóteses ordenadas (operacionais):
1. Filme auto-Close (st620=11) **antes** de FORCE latch (StartSeq nunca / FORCE frio).
2. FORCE corre mas loader de asset pós-intro não pede `R_*` (FSM conteúdo).
3. Open path FIOS/psarc (gow2.psarc fd ok mas member name não).

Discriminador barato: logar `Open` / `StartSeq` / `FORCE` / `open R_` contagens em 45 s com `FORCE_MS` em grelha {2000,4000,8000}.

- [ ] **Step 2: Aceite só operacional** — `open 'R_PermA'` ≥1 in-boot; **não** confundir com pixels.

---

### Task 6: Aceite honesto do plano (fecho)

**PASS deste plano de diagnóstico** (qualquer um, medido in-boot):

1. **Veredito H1 vs H2 escrito** com tabela Boot A/B e contagens; **ou**
2. **Uma** combination Default.ps3fx **não-idêntica** à baseline **correlacionada** a WAD/registry **sem** forja; **ou**
3. `SHADERSRC` N>0 **pelo caminho natural** (não sintético); **ou**
4. `[LDRSH]` natural ≥1 pós-walk (não GATE-FORCE).

**FAIL / incompleto (ainda entrega válida se documentado):**
- Só Boot A sem discriminador.
- “Invalid parou” via CRC bypass / stub.
- Aceite baseado em `PS3_SHADER_DEMO`.

**Não é aceite deste plano:** frame com pixels de jogo (isso é Plano 05 / parede E).

- [ ] **Step 1: Actualizar índice de walls**

Em `docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md`, satélite:

```markdown
| [`2026-07-21-shader-combination-zeroed.md`](2026-07-21-shader-combination-zeroed.md) | 🟡 diag | H1 WAD-skip vs H2 registry (Default.ps3fx) |
```

- [ ] **Step 2: Actualizar CLAUDE.md / notes só com factos medidos** (não copiar hipóteses como factos).

- [ ] **Step 3: Commit docs**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp
git add docs/superpowers/plans/2026-07-21-shader-combination-zeroed.md \
        docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md
git commit -m "docs(plan): diagnostico Default.ps3fx combination H1 vs H2"
```

---

## Ordem de execução (checklist rápido)

```
Task 0  baseline Boot A (métricas)
Task 1  confirmar mapa mat+off no lift
Task 2  probe COMBOPROP + 1 boot
Task 3  Boot B discriminador → VEREDITO
Task 4  só se H2 → typemap/LDRSH/SHADERSRC
Task 5  só se WAD open falhou → timing FORCE
Task 6  fecho honesto + índice
```

## Relação com planos irmãos

| Plano | Relação |
|-------|---------|
| `2026-07-21-intro-vdec-open-force-wad.md` | pré-condição se Open/FORCE/WAD ainda vermelhos |
| `2026-07-14-gow2-intro-movie-fsm-wads.md` Task 5 | classificação Win a **revalidar** no Mac A3b, não contradizer sem medição |
| `2026-07-20-05-rsx-shaders-pixels.md` | **depois** de registry/combo; pixels ≠ este plano |
| `docs/gow2-asset-pipeline-map.md` | stream R_PermA full ≠ registry populado |

## Anti-padrões (checklist mental)

- [ ] Não declarar “WAD resolve shader” sem Boot B.
- [ ] Não declarar “registry ok” com só GATE-FORCE.
- [ ] Não tratar `TEXTURE=0;CONSTCOLOR=1;…` como “uninitialized zero” sem dump de mat.
- [ ] Não CRC bypass para “silenciar” o spam e chamar de fix.
- [ ] Não `git add` de `ppu_recomp_*.cpp` / WADs / EBOOT.
