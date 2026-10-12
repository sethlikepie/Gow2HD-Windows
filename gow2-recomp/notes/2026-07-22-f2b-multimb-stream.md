# F2B multi-MB stream refill — pós-SBP unblocked (2026-07-22)

## Problema

Após `WADLD-BODY #1` (`SBP_general`, ~1.16 MB) com ring F2B de **256 KiB**:

1. Body multi-pass esgota `avail` → `BA9F0` **WAIT** sem refill.
2. Contabilidade de `rem`/header avança; o ring fica vazio/desync.
3. Próximo header lido do payload a meio → size lixo → `BACE8 need=0x687DD790`.
4. Hang em `00262610` / freelist (ou rem=0 state=3).

## Fix (lift `recomp_macos_v2`)

| Peça | Onde |
|------|------|
| `f2b_stream_fill` em loop até ring cheio / need | `ppu_recomp_001.cpp` |
| `f2b_stream_ensure(type_sys)` | export C, refill se `avail==0` |
| Call ensure | `func_002BAB88` (001), `func_002BA9F0` (002) |
| Clamp need≤avail | `func_002E1480` pre-hook |

Script: `recomp_mid_v2/patch_f2b_multimb_stream.py` (verificação de markers).

## Evidência in-boot (`/tmp/f2b_multimb.log`, PS3_TRACE_TYMAP=1)

| Antes | Depois |
|-------|--------|
| BODY = 1 (`SBP_` only) | **BODY ≥ 2** |
| `SBP_general2` | **T1 size=2996** (match ficheiro LE) |
| `need=0x687DD790` | **0** |
| FREELIST-TAG-GUARD spin | **0** |
| file_pos | **1.5 MB+** a subir (multi-window) |
| ENSURE | multi-pass `rem1D4` 0xDC830→…→0x1C830 |

T1 após SBP (amostra): `SBP_general2`, `SBI_Hero` (99200 B), `GFX_decorChest01_gol`, `PAL_decorChest01_gol`, …

## Platô ~1.5 MB → fix state-2 ensure (mesma data)

**Causa:** `func_002BA9BC` (state=2 header) esperava com `avail≤0x1F` **sem** `f2b_stream_ensure` (só BAB88/BA9F0 tinham). Stream parava após `~010decorchest01_gol`.

**Fix extra:** ensure em BA9BC + BA76C; ensure top-up se `avail < 0x20`.

### Smoke `/tmp/plat2.log` (pós-fix)

| Métrica | Valor |
|---------|--------|
| file_pos max | **20 169 344 / 20 169 344 FULL** |
| ENSURE | 200 (cap log) |
| bad_need / freelist | **0** |
| BODY / T1 (probes) | 2 / 73 (caps; últimos: SBP*, SBI_Hero, decorChest, ~010) |
| SM rem min | **0** |
| SM late | state=**3** rem=0 (spin; não limpa state) |
| B71B8 exit | **ainda False** |

Interpretação: **stream R_Perm inteiro consumido pelo F2B**. O hang 0x687DD790 e o platô 1.5 MB estão **vencidos**.

## state=3 rem=0 → EOF-DONE (mesma data)

**Causa residual:** file_pos FULL e rem=0, mas SM ficava em **state=3** com `body1D4` residual (medido `0x23DB43D0` — body “a meio” sem mais bytes). `BA76C` despacha sempre BAB88 em state=3 → wait eterno; `41D5C` não sai.

**Fix:** `f2b_stream_eof_try_complete(ts)` — se `file_pos>=size` e `rem==0` e state∈{2,3}: zera `1D4`, **state=0** (idle). Chamado de BA76C/BAB88 após ensure.

### Smoke `/tmp/eof_done.log`

| Sinal | Valor |
|-------|--------|
| `F2B-STREAM-EOF-DONE` | was_state=3 body1D4=0x23DB43D0 → state=0 |
| file_pos | FULL |
| bad need | 0 |
| Após EOF | **`[POSTINTRO] enter func_000E4950`** (nunca antes) |
| Continua | `0010F5E8`, `000C992C`, `002A6608` |

**41D5C desbloqueado** — boot sequence avança além do wait WAD.

## Same-class audit (mesma data) — ver `2026-07-22-same-class-audit-f2b-stream.md`

| Extra | Onde |
|-------|------|
| ensure+eof | `BA9F4` (irmão de BA9F0) |
| `f2b_stream_pre_consume` | `002E11EC`, `1228`, `1290`, `1480` |
| EOF guards | `body1D4!=0` **e** `avail==0` (senão mata Lgl cedo) |

Smoke revalidado: `/tmp/f2b_audit_smoke4.log` — R_Perm FULL, EOF-DONE 1× residual, `000E4950`+.

## Ainda aberto

- Menu UI binds / Wall D (typemap walk) / conteúdo completo do R_Perm (EOF-DONE é HLE de fecho SM; body residual implica membros após ~010 podem não ter expandido todos).
- Ideal: reduzir body1D4 residual (size/stream ainda imperfectos) para EOF-DONE ser raro.

## Aceite desta tarefa

- [x] Multi-pass body com ENSURE (não wait eterno em avail=0)
- [x] `SBP_general2` processado com size correcto
- [x] Sem need 0x687DD790 / freelist hang no path medido
- [x] file_pos R_Perm **FULL** (20 169 344)
- [x] SM state=3 rem=0 → **EOF-DONE** → pós-41D5C (`000E4950`+)
- [x] Auditoria same-class (BA9F4 + E11EC/1228/1290 + EOF guards) + smoke
