# R1 — RE ICALL-BAD `0x40637488` + present pós-WAD (2026-07-23)

**Tipo:** diagnóstico RE (sem fix). **Gate A continua RED.**  
**Não claim:** menu / SetFlip_after_R_Perm≥1 / causalidade única do ICALL-BAD.

**Log base:** `/tmp/m3_baseline_norsx.log` (TIMEOUT=90, `PS3_NO_RSX=1`, menu-fast; Gate B GREEN).  
Réplicas idênticas nos 12×: `m0_menu_fast`, `m2_menu_fast_disc`, `m3_live_skip*`, `m3_yield*`.

---

## 1. Catálogo ICALL-BAD (in-boot)

`firstlog < 12` em `ps3_indirect_call` → **exactamente 12** linhas `[ICALL-BAD]`, todas **após** `GATE-FORCE R_PermA full`.

| Campo | Valor estável | Notas |
|-------|---------------|--------|
| **ctr** | `0x40637488` | único ctr nos 12 |
| **lr** | `0x00000000` | bctr/tail (sem link) |
| **r2** | `0x40637478` | = ctr − 0x10 (par OPD-shaped em heap) |
| **r3** | `0x400C3D88` | `this` estável (obj heap, **≠** TYPE15 `0x4066D7xx`) |
| **r11** | `0x400C3D88` (×9) ou `0` (×3) | |
| **r12** | `0x40004024` | **arena+4** (ver §2) |

**Únicos (ctr,lr,r2,r3,r11,r12):**

```
9×  (40637488, 0, 40637478, 400C3D88, 400C3D88, 40004024)
3×  (40637488, 0, 40637478, 400C3D88, 00000000, 40004024)
```

Dump de regs no 1º unresolved (mesmo hit):

```
r0=40637488  r1=0FEFF9F0  r2=40637478  r3=400C3D88
r4=40637AB4  r5=40637AC4  r7=40004020  r9=9102FFFF
r12=40004024  r29=40083D60  r31=00869570
```

- `r7=0x40004020` = **arena base** (mesma vista em BACE8 / TYPE15 writes).  
- `r12=arena+4`.  
- `r4/r5` ∈ banda freelist `0x40637xxx`.  
- `r31=0x00869570` = `ts=` do F2B-STREAM-ENSURE (objeto de stream/typemap).  
- `r29` ≈ base de fill stream (`base=0x40083D40` no log).  
- Imediatamente antes: `[vm] UNCOMMITTED read32 0x91030047` (tag freelist 0x91… — mesma família do hang FORCE-ICALL2).

Pós-cap de 12 `[ICALL-BAD]`, o run ainda emite unresolved **sem** o tag ICALL-BAD (ctr muda):

| ctr unresolved | r3 | r12 | Classe |
|----------------|----|-----|--------|
| `0xFB788082` | `0x400C3D88` | `0x40004024` / `0x40637AD4` | OOB / lixo |
| `0x4003ED70` | `0x400C3D88` | `0x40637AD4` | heap-as-code 0x40 |
| `0xFB810090` | 0 | `0x40637AD4` | OOB |
| `0x00881AA4` | `0x008695EC` | 0 | EA texto? / TOC residual |
| `0x4E800421` | `0x27182814` | `0x40004024` | **opcode bctrl** como alvo |
| depois | | | `[ICALL-HEAP] skip ctr=0x27182818 r3=0x400C3D88` (banda 0x20–0x3F) |

Mesmo `r3=0x400C3D88` liga ICALL-BAD → lixo → ICALL-HEAP 0x27182818 (já guardado).

---

## 2. Classificação de `0x40637488`

| Hipótese de banda | Match? | Evidência |
|-------------------|--------|-----------|
| **Freelist / heap main (arena `0x40004020`)** | **SIM — primária** | r7=arena; r12=arena+4; ctr/r2/r4/r5 em `0x40637xxx`; head histórico BACE8 `0x40637CC0` (ctr ≈ head−0x838) |
| Pin-zone TYPE15 `0x47D00000–0x47D00C00` | **NÃO** | |
| Product live `0x42F85AE4` | **NÃO** | product só aparece **depois** (CLOSE-PRESERVE) |
| TYPE15 `this` `0x4066D798/804` | **NÃO** | r3=`0x400C3D88`; mesma banda 0x406… mas objeto diferente |
| Guest text / função liftada | **NÃO** | `!ppu_lookup(0x40637488)`; text é `0x00xxxxxx` |
| `func_00406374` (EA `0x00406374`) | **NÃO confundir** | existe no mapa; **não** é `0x40637488` |
| Cookie ASCII / path string | **NÃO** | bytes não são 2–4 printables (ICALL-ASCII N/A) |
| OPD real (code+TOC em .opd) | **NÃO** | par r2/ctr são **dois ponteiros de heap** adjacentes (ctr=r2+0x10), típico de **dados de freelist lidos como OPD** |

**Veredito:** `0x40637488` é **endereço de bloco/dados no freelist da arena principal**, usado como **CTR de icall** (heap-as-code).  
**Não** é vtable legítima de TYPE15, **não** é pin-shell, **não** é entry de código.

`r12=0x40004024` é o **slot de controlo do alocador (arena+4)**, não um OPD/vtable de objeto de jogo — fingerprint de “chamar através do freelist”.

---

## 3. Quando o handler ICALL-BAD dispara

Site: `ps3recomp/runtime/ppu/ppu_loader.cpp` ~1985–2057 (`ps3_indirect_call`).

Ordem de filtros **antes** do log:

1. `addr==0` → return  
2. `addr==0xC0DE1111` → r3=0 return  
3. **`ICALL-HEAP`**: `0x20000000 ≤ addr < 0x40000000` → skip+r3=0 (**não cobre 0x40…**)  
4. **`ICALL-ASCII`**: 2–4 chars printables → skip  
5. lookup ok → call  
6. senão: **`[ICALL-BAD]`** se `firstlog++ < 12` e ctr≠0  
7. stuck-guard 2000× mesmo ctr → FATAL  
8. log unresolved + dump (≤8)  
9. **`ctx->gpr[3]=0`** e return (caller trata como “sem resultado”)

`0x40637488` passa (3) porque está em **0x40xxxxxx** (heap alto GoW2), fora da janela ICALL-HEAP pensada para `0x27182818`.

`lr=0` + fallback de `ps3_indirect_tail` → `ps3_indirect_call` é consistente com **bctr** (não bctrl).

---

## 4. Timeline vs present (Gate A)

| Ordem (m3_baseline) | Evento |
|--------------------:|--------|
| …5760 | **último** `SetFlipCommand` do boot |
| 5762 | `movieio open R_LglScA` |
| 5791 | open `R_PermA` |
| 5803 | R_Perm **full** 20169344 |
| **5808–5845** | **ICALL-BAD ×12** (+ UNCOMMITTED 0x91…) |
| 5871+ | unresolved lixo / OOB |
| 6122 | FREELIST-TAG-GUARD 262610 |
| 6134+ | ICALL-HEAP `0x27182818` r3=`0x400C3D88` |
| 6155+ | TYPE15 CB56C attach=full ×2, CLOSE-PRESERVE |
| 6209 | thr_auto_load start/end |
| resto | CC9D0 spin f4=0; st620 0→0; **zero** SetFlip |

**Consequência medida:**

1. Flip **já parou no open WAD** (2 linhas antes do R_Perm full) — **antes** do ICALL-BAD.  
2. ICALL-BAD **não pode ser a causa** do corte de flip em R_LglScA.  
3. Ainda pode ser **co-sinal / bloqueio de reentrada** (typemap/UI arm) **depois** do full — **não provado** nesta task (é R2).

YIELD / LIVE_SKIP (M3) não removem os 12 ICALL-BAD nem restauram flip.

---

## 5. Quem emite o icall (estático / dinâmico)

| Fonte | Achado |
|-------|--------|
| Log `host_ra` | `atos` aponta `func_003FB5C8+…` e `func_000BF004+…` |
| Disasm | **`func_003FB5C8` não tem `bl _ps3_indirect_call`** (só alloc/free `263C18`/`262FF8` + memcpy 0xC); host_ra **ambíguo** (tail/ASLR/nested) |
| `func_000BF004` | **tem** OPD-icall clássico: `vt=*(r3); opd=*(vt+0x48); ctr=*opd; r2=*(opd+4); bctrl` — candidato secundário |
| Contexto regs | path de **stream/typemap** (`r31=ts 0x00869570`) + **freelist** (r7/r12/r4/r5) **durante** pump R_Perm |
| Aberto | sítio exacto no lift que carrega OPD a partir de arena+4 / nó `0x40637xxx` — **não fechado** sem probe de memória no 1º hit |

Não re-litigar jumptable `2B11B8` (fix Task4 para `0x27182818`); este ctr é **outra banda** (0x40 freelist).

Probe opcional **não adicionado** (dados de log + dump de regs basam a classificação; default OFF).

---

## 6. Hipóteses ordenadas para R2 (matar primeiro)

| # | Hipótese | Como matar | Expectativa |
|---|----------|------------|-------------|
| **H1** | **Ponteiro de método/OPD corrompido: freelist/arena no slot de vtable/OPD do obj `0x400C3D88`** | Dump 16–32 words em `r3`, `r12`, `ctr-0x10` no **1º** ICALL-BAD; achar writer de `*(obj+vt_off)`; comparar com snapshot pré-WAD | Top — casa com r12=arena+4 e par heap OPD-shaped |
| **H2** | **Desync stream/typemap pós-R_Perm** produz records com FP lixo (UNCOMMITTED 0x91… + freelist walk) | Correlacionar cursor F2B / member parse com 1º ICALL; disc: stream ok vs ICALL count | Forte co-causa do *path* de parse; não explica stop de flip no open Lgl |
| **H3** | ICALL-BAD **causa** SetFlip_after=0 | Disc: skip/no-op alargado a `0x40–0x4B` (estilo ICALL-HEAP) **só gated**; medir SetFlip_after e Gate B | **Provável DEAD** (flip já 0 pré-ICALL); se skip→flip≥1, reabrir |
| **H4** | Expandir ICALL-HEAP a 0x40… “resolve” present | Mesmo disc que H3 | Silencia log; **não** é fix fiel se H1 true |
| **H5** | Ligado a TYPE15/CC9D0 f4=0 | Timeline: ICALL **antes** de CB56C attach; r3≠TYPE15 this | **Fraco** como causa directa do spin CC9D0 |
| **H6** | Confusão com `func_00406374` | lookup EA | **Morto** |

**Ordem R2 recomendada:** H3 disc barato (gated skip 0x40-heap-as-code) **só para causalidade flip** → se DEAD, H1 dump+writer (raiz) → H2 se writer for stream/type.

---

## 7. Aceite R1

| Critério | Status |
|----------|--------|
| Documentar o que é `0x40637488` | **PASS** — freelist/heap data, não código/OPD legítimo |
| r3/r11/r12 in-boot | **PASS** — §1 |
| Path guest (após thr/WAD) | **PASS parcial** — pós-R_Perm full, stream/typemap+freelist, **antes** thr/TYPE15 attach; sítio asm exacto aberto |
| Hipóteses ordenadas R2 | **PASS** — §6 |
| Sem theater / sem regredir Gate B | **PASS** (só nota) |

---

## 8. Artefactos

- Log: `/tmp/m3_baseline_norsx.log`  
- Handler: `ps3recomp/runtime/ppu/ppu_loader.cpp` ~1867–2057, 2096–2124  
- Prior: `notes/2026-07-23-m3-postb71-flip.md`, `notes/2026-07-22-postintro-wadld-sm-hang.md` (arena/head), ICALL-HEAP `0x27182818`  
- Plano: `ps3recomp/docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md` Task R1  
