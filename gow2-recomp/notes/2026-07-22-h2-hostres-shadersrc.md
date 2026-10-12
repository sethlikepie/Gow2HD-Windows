# H2 — HOSTRES Mac + SHADERSRC N>0 natural (2026-07-22)

## Contexto

Após A1/H3 (`notes/2026-07-22-a1-chain-inboot.md`):
- typemap **vivo** pós-R_Perm (`vt=0x5130B8`, `+8=0x522E70`)
- cadeia `00468C3C→0032E200→walk` **0**
- SHADERSRC histórico: **18× N=0** (N1 e boots pré-HOSTRES)

H2 perguntava se o registo natural passa por WADLD-FIN / HOSTRES / outro path.

## Achado 1 — HOSTRES estava morto no Mac

| Antes | Depois |
|-------|--------|
| `apply_all_patches.sh` **SKIP** `patch_host_res_inflate.py` no Darwin | SKIP removido (só `wad_tex` fica) |
| `host_res_inflate.c` fora do link (`runtime/ppu` excluído da `.a`) | `build_macos.sh` compila + linka `host_res_inflate.o` |
| `[HOSTRES]=0` | **23/23 inflate OK** |

Log: `/tmp/vdec_hostres.log`.

Inclui `gowshader.cfx` (`outlen=1485120`), `default.cfx`, `movie.cfx`, `legalscreen720.ctxr` (CONTENT hold 1280×720), etc.

## Achado 2 — SHADERSRC N=0 era sintoma de gzip falho, não “catálogo vazio”

| Boot | SHADERSRC | Σ N |
|------|-----------|----:|
| `/tmp/vdec_a1chain.log` (sem HOSTRES) | 18× **N=0** | 0 |
| `/tmp/vdec_hostres.log` (com HOSTRES) | 18× **N>0** | **889** |

Correlação temporal: cada `.cfx` HOSTRES precede um `[SHADERSRC]` com N coerente;
`gowshader.cfx` → `[SHADERSRC] #12 N=776`.

**Isto é aceite parcial de fonte real in-boot** — não forjado, não GATE-FORCE.

## Achado 3 — walk / ICGLdr / A1 continuam 0

Com ΣN=889 e typemap vivo:

| Tag | N |
|-----|--:|
| `[A1-468C3C]` / 30D54 / 32DF98 | 0 |
| `[CMP-ENTER]` / `[TYMAP-171]` / `[LDRSH]` | 0 |
| `[TYMAP-DUMP]` vt/OPD correctos | 1 |

**SHADERSRC (func_003CC208) e o walk typemap (func_00171244 / ICGLdr 0032109C)
são caminhos distintos.** Popular defs via HOSTRES+CFX **não** invoca o walk
nem o corpo ICGLdr instrumentado. A1 permanece: o único site estático do walk
nunca corre.

## WADLD-FIN (ramo paralelo)

`WADLD-GEND`/`FIN` correm (SHGX expand 48B stubs). FIN OPDs ≠ `0x522E70`.
SHGX WAD **não** alimenta SHADERSRC N; microcódigo de engine está no EBOOT gzip.

## Veredito H2

| Hipótese | Estado |
|----------|--------|
| H2a: WADLD-FIN deveria chamar o walk | **Refutada** (corre sem walk; OPD diferente) |
| H2b: HOSTRES/gzip bloqueava fonte | **Confirmada e corrigida no Mac** |
| H2c: SHADERSRC N>0 ⇒ ICGLdr/walk | **Refutada** (N=889, LDRSH=0) |
| H3 (typemap built) | Mantém-se |
| A1 (caller do walk nunca corre) | Mantém-se |

## Mudanças de código

- `recomp_mid_v2/patch_host_res_inflate.py` — hook + `[HOSTRES-ENTER]` gated
- `apply_all_patches.sh` — deixa de SKIP host_res no Darwin
- `build_macos.sh` — build/link `host_res_inflate.o`

## Próximo

1. ~~**O que faz SHADERSRC com N>0?**~~ → **respondido** em
   `notes/2026-07-22-shadersrc-loop.md`: grow+loop completo, **i=N=889**,
   materializa slots 0x34 via `003C3598` (não via insert `003C8578`).
2. **Consumer:** lookup CGOWShader / combination usa as 889 entradas?
3. **A1/walk** continua aberto (ICGLdr nested ≠ este path).
