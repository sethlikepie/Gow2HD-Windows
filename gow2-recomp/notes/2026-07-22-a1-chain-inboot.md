# A1 chain in-boot (2026-07-22) — medida pós-WAD

## Recipe

```
PS3_NO_RSX=1 PS3_TRACE_A1CHAIN=1 PS3_TRACE_TYMAP=1 PS3_TRACE_LDRSH=1
# sem PS3_GATE_FORCE
./boot_gow2 EBOOT.ELF   # ~28s, R_Perm full @t≈20s + 8s pós
```

Log: `/tmp/vdec_a1chain.log` (3171 linhas).
Probes: `recomp_mid_v2/patch_a1_chain_probe.py` (gated, OFF default).

## Contagens (boot natural, R_Perm full + SHGX expand)

| Tag | N | Significado |
|-----|--:|------------|
| `R_PermA full` | 1 | bytes_read=20169344 GREEN |
| `WAD_R_Perm` / `SHGX_R_Perm` T1SZ | ≥1 | BODY + SHGX expand GREEN |
| `WADLD-GEND` / `WADLD-FIN` / `WADLD-CALL` | 80 / … | GroupEnd + type dispatch vivos |
| `TYMAP-GS` | 1 | GroupStartCtor uma vez (cedo) |
| **`[A1-468C3C]`** | **0** | `func_00468C3C` **nunca entra** |
| `[A1-468-EX1/EX2]` | 0 | early DF8 n/a (sem entry) |
| `[A1-CALL30D54]` / `B` | 0 | pré-chamada 00330D54 n/a |
| **`[A1-30D54]`** | **0** | `func_00330D54` nunca |
| **`[A1-32DF98]`** | **0** | gate `*r5` nunca exercitado |
| `[CMP-ENTER]` / `TYMAP-171` / `LDRSH` | 0 | walk / ICGLdr natural 0 |
| `[ICG-PATH-A-OPD]` / `[ICG-VCALL]` | 0 | micro-ctor path (ascendente) também 0 |

Probes **confirmados no binário** (`strings boot_gow2 | grep A1-468C3C`).
Não é falha de instrumentação.

## Veredito (refina A1)

Duas camadas independentes, ambas fechadas:

1. **In-boot (esta medida):** a subárvore inteira
   `00468C3C → 00330D54 → 0032DF98 → 0032E200` **não corre** no boot natural
   pós-R_Perm (intro → legal screen WAD → R_Perm full → SHGX TOC expand).
   O "nunca alcançado" de A1 aplica-se **acima** de 0032E200: o portão estático
   `00468C3C` e o micro-ctor `00329490` (`[ICG-PATH-A-OPD]`) estão a zero.

2. **Estático (Task 5 + indirect-dispatch):** *mesmo se* `00468C3C` corresse, o
   branch para `0032E200` está morto por construção (`li r30,0` → `sp+0x7C` →
   `*r5==0` no gate de `0032DF98`). E o censo de 47 `ps3_call_opd` confirma que
   não há outro site capaz de chamar OPD `0x522E70` (walk).

Offline extra (esta sessão): no `EBOOT.ELF`, OPD `0x522E70` aparece **1×**
(file+0x5030C0); code EA `0x0032E200` **0×** como word OPD — coerente com
"só `bl` directo a partir de 0032DF98", sem vtable alternativa.

## O que isto **não** é

- Não é "WAD incompleto": R_Perm full + SHGX expand medidos.
- Não é "typemap inexistente": GATE-FORCE (diag) já provou `vt=0x5130B8` vivo.
- Não é aceite de [D]: `[LDRSH]=0` / `[CMP-ENTER]=0` no natural.

## Próximo (discriminador)

A cadeia 00468C3C é utilitário de *hashmap set* que só toca vt+0x8 no
**replace** de entrada antiga (`notes/2026-07-21-registry-indirect-dispatch.md`).
No boot actual **nem o set corre**. Duas hipóteses a ordenar:

| # | Hipótese | Experimento |
|---|----------|-------------|
| H1 | O jogo ainda não chega à fase de cena/component que constrói via `00329490`/`00468C3C` (menu/gameplay mais tarde) | Boot mais longo + pad/input; ou forçar progressão de FSM de menu se conhecida |
| H2 | O registo natural de SHADERSRC **não passa** por esta família — falta outro entry para o walk (ex.: path de GroupEnd/HOSTRES/EFCT que ainda não materializa microcódigo) | RE do handler SHGX/SHADERSRC type-id no WADLD (type table @ `0x868D48`) e do `WADLD-FIN` OPD `0x51B1C0` |
| H3 | ICG-CTOR / componente typemap nunca é *instalado* no natural (só visto sob GATE-FORCE fallback `0x4306ADF0`) | Probe entry em `func_0014A01C` / slots ICG-CTOR; dump `component+0xDC` pós-R_Perm **sem** GATE-FORCE |

Prioridade recomendada: **H3 barato** (1 probe + 1 dump read-only pós-full) para
separar "typemap object never built" de "built but never walked". H2 se H3 mostrar
objeto vivo sem walk.

## H3 medido (mesma data, `/tmp/vdec_tymap_dump.log`)

`ps3_dump_post_wad_typemap` em `ppu_loader.cpp` (gated `PS3_TRACE_TYMAP_DUMP`,
sem OPD call, sem stream sintético):

```
[TYMAP-DUMP] base=0x00700DF8 slot=0x00835778
  comp=0x4306ADF0 tm=0x4306B160 vt=0x005130B8 vt+8=0x00522E70
  fb_comp=0x4306ADF0 fb_tm=0x4306B160 fb_vt=0x005130B8
```

| Campo | Valor | Leitura |
|-------|-------|---------|
| `comp` no slot canónico | `0x4306ADF0` | ICG component **instalado naturalmente** |
| `tm` | `0x4306B160` | typemap vivo |
| `vt` | `0x5130B8` | vtable certa |
| `vt+8` | `0x522E70` | OPD do walk (`func_00171244`) correcta |
| A1 chain | 0 | walk **não invocado** |

**H3 fechado: built but never walked.** O objecto e o slot OPD estão prontos;
falta o *caller* natural do walk. H1 (fase de cena mais tarde) e H2 (outro path
que não 00468C3C — ex. WADLD-FIN / HOSTRES / SHADERSRC material) ficam em aberto;
a cadeia 00468C3C continua refutada como via activa neste boot.

## H2 (follow-up, mesma data)

`notes/2026-07-22-h2-hostres-shadersrc.md`: HOSTRES gzip no Mac **activado**
(23/23, incl. `gowshader.cfx`). SHADERSRC passa de 18×N=0 → **ΣN=889**.
Walk/LDRSH/A1 **continuam 0** — SHADERSRC ≠ typemap walk.
