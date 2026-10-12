# Auditoria same-class — F2B stream / WADLD waits (2026-07-22)

Pedido: *sempre que achar um bug, investigar bugs parecidos*.

Bug-mãe: body multi-MB esgota ring 256 KiB → wait sem refill → header lixo →
`need=0x687DD790` / freelist hang; depois platô 1.5 MB (BA9BC sem ensure);
depois state=3 rem=0 (EOF residual).

## Método

1. Censo de funções no range WADLD (`0x2BA000–0x2BC000`) e ring ops (`0x2E1xxx`)
   que tocam `+0x1A8` (stream), `+0x1AC` (rem), `+0x1CC` (state), `+0x1D4` (body).
2. Classificar: **wait/dispatch**, **consume**, **produce/init**, **só probe**.
3. Fallthrough-backward trampoline (`g_trampoline_fn` para EA menor, Δ&lt;0x200):
   **0** no lift actual (padrão 2550C8 não reaparece no range WAD).
4. Fix mínimo + rebuild + smoke in-boot; regressões da própria auditoria
   tratadas como same-class (EOF demasiado agressivo).

## Matriz (censo)

| Função | Papel | Hook |
|--------|--------|------|
| `func_002BA76C` | SM dispatch | ensure + eof |
| `func_002BA9BC` | state=2 header wait | ensure |
| `func_002BAB88` | state=3 body step | ensure + eof |
| `func_002BA9F0` | body wait | ensure + eof |
| `func_002BA9F4` | body residual sibling | ensure + eof (**audit**) |
| `func_002E11EC` | ring advance/skip | pre_consume (**audit**) |
| `func_002E1228` | cursor skip | pre_consume (**audit**) |
| `func_002E1290` | ring copy | pre_consume (**audit**) |
| `func_002E1480` | ring copy | pre_consume |
| `func_002E1254` | **produce** (avail +=) | sem pre_consume (ok) |
| `func_002E13C8` / `1424` | ring **init** | sem pre_consume (ok) |
| `func_002BA990` / `BAD10` / `BAD78` / `BABD8`… | set state / reset / accounting | despacham para sites com ensure |
| `func_002BACE8` | alloc body size | freelist guard (já) |

Verificador idempotente: `recomp_mid_v2/patch_f2b_multimb_stream.py` (score 21/21).

## Helpers

| Helper | Contrato |
|--------|----------|
| `f2b_stream_fill` | loop até ring cheio / min_need |
| `f2b_stream_ensure(ts)` | top-up se `avail < 0x20` ou `avail < rem1D4` |
| `f2b_stream_pre_consume` | refill + clamp `r4=need` a avail (EMPTY→0) |
| `f2b_stream_eof_try_complete` | ver guards abaixo |

### Guards EOF-DONE (aprendizado da auditoria)

Disparar idle **só** se:

1. `file_pos >= size`
2. `rem (1AC) == 0`
3. `state ∈ {2,3}`
4. **`body1D4 != 0`** — hang real (R_Perm residual `0x23DB43D0`); **não** fim limpo do Lgl
5. **`avail == 0`** — ring vazio (não abortar com bytes ainda legíveis)

Regressões medidas **antes** dos guards:

| Smoke | Sintoma |
|-------|---------|
| audit2 | EOF em Lgl com ring cheio → sem POSTINTRO / R_Perm |
| audit3 | EOF em Lgl body=0 após membros → sem R_Perm |

## Smoke aceite (`/tmp/f2b_audit_smoke4.log`, ~22 s até FULL)

| Métrica | Valor |
|---------|--------|
| st620 | 0→1→3→11→0 (EOS) |
| POSTINTRO | 23 (incl. `00041D5C`, **`000E4950`**, `0010F5E8`, `000C992C`) |
| R_LglScA | T1 + membros |
| R_PermA | open + `file_pos=20169344/20169344` FULL |
| FILL / ENSURE | 64 / 200 (cap log) |
| CLAMP / EMPTY | 0 / 0 |
| `need=0x687DD790` | **0** |
| EOF-DONE | **1×** `was_state=3 body1D4=0x23DB43D0` FULL |
| FREELIST-TAG-GUARD | 3 aborts (need lixo / walk) — não hang; r3=0 |

## Ainda aberto (fora do mesmo class wait/refill)

- Residual `body1D4` em R_Perm (EOF-DONE ainda HLE de fecho; ideal reduzir).
- FREELIST need lixo ocasional (`0x26262700`) — guard evita hang; root size/header.
- Wall D typemap walk natural; binds UI / menu pixels.

## Política daqui para a frente

Ao achar um bug de path (wait, clamp, fallthrough, guard):

1. Nomear a **classe** (ex.: “wait sem refill em consumer de ring”).
2. Censar irmãos (mesmo offset, mesmo SM state, sibling EAxxx).
3. Fix partilhado (helper) em vez de patch one-off.
4. Smoke de não-regressão do baseline anterior **e** do sítio novo.
5. Nota curta em `notes/` + markers no `patch_*.py`.
