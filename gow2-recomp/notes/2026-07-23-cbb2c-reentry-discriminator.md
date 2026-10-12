# CBB2C reentry discriminator: H_A vs H_B resolvido (Row 4, não Row 2)

## Goal
Discriminar as duas hipóteses deixadas em aberto por
`2026-07-23-2b2e04-typewrites-diagnostic.md` para o `CC9D0` spin eterno
(`f4=f54=0`) em `0x4066D798`/`0x4066D804`:

- **H_A**: `CB56C` devia preservar `this+0x4`/`this+0x54` no caminho de reuse
  (o reset incondicional no prólogo é o próprio bug).
- **H_B**: `CBB2C`/`CB56C` estão a ser invocados com frequência errada (deviam
  correr uma vez; algo os re-invoca).

Plano (3ª consulta ao codex): probe novo `PS3_TRACE_CBB2C_DISC`, keyed por LR
guest, contando entradas de `CBB2C` por `base` e capturando `pre+4`/`pre+54`/
`pre+D8` em `CB56C` **antes** do reset.

**Nota:** desde a sessão anterior, outra sessão (concorrente) já enviou
`2026-07-23-type15-list-sanitize-fix.md` — `icall2`+`2A4FE4` correm por
default agora (`PS3_TYPE15_SKIP_ATTACH=1` é que é opt-out). Isto é ortogonal
ao reset de `+0x4`/`+0x54`, que acontece **antes** de qualquer icall1/icall2,
independente desse toggle — confirmado nesta sessão (ver log abaixo).

## Probe
`recomp_mid_v2/patch_cbb2c_reentry_disc.py` (novo, idempotente, default OFF,
env `PS3_TRACE_CBB2C_DISC`). Insere, sem tocar em nenhuma outra semântica:
- `func_000CBB2C` (`ppu_recomp_000.cpp:162081`): `base`, `lr` (guest,
  `ctx->lr` não modificado neste ponto — a cadeia `CBADC→CBB2C` é um
  trampolim, `lr` continua a ser o do chamador lógico original), e uma
  contagem por `base` (tabela linear de 16 slots).
- `func_000CB56C` (`ppu_recomp_000.cpp:161493`): `obj`, `lr`, e os valores
  **pré-reset** de `+0x4`/`+0x54`/`+0xD8` (lidos antes de qualquer mutação,
  logo à entrada da função).

## Resultado (1 run, 40s, recipe padrão + `PS3_TRACE_CBB2C_DISC=1`, SEM
## `PS3_TYPE15_SKIP_ATTACH`/`PS3_TYPE15_CB56C=0`/outros workarounds antigos)

```
[CBB2C-DISC] base=0x4066D798 lr=0x00000000 count_for_base=1
[CB56C-DISC] obj=0x4066D798 caller_lr=0x00000000 pre+4=0x40004020 pre+54=0x00 pre+D8=0x00000000
[CB56C-DISC] obj=0x4066D804 caller_lr=0x00000000 pre+4=0x00000000 pre+54=0x00 pre+D8=0x00000000
```

`lr=0x00000000` nos dois — não discrimina chamadores (toda esta cadeia é
despachada com LR sintético 0, consistente com "0 chamadores estáticos" já
visto para `CBB2C`/`CB56C`/`CC9D0`/`002B2E74` — provavelmente um helper de
dispatch tipo `ps3_call_opd(ctx, entry, /*lr=*/0)`). Não impede a conclusão:
o sinal decisivo é `count_for_base` + `pre+4`.

Non-regressão (mesmo run): `R_PermA full 20169344`, `B71B8 ... +0x64=1`,
`thr_auto_load() start`/`end` — todos presentes. `icall2`+`2A4FE4` completos
para os dois objectos (`attach=full`, `after 2A4FE4` ×2, sem hang) — a fix da
sessão concorrente aguenta.

## Classificação (tabela do codex)

| Observação | Bate? |
|---|---|
| Cada `base` entra em `CBB2C` uma única vez; N `CB56C` consecutivos | **Parcial** — `count_for_base=1` confirma entrada única (não 4, mas o código real só itera 2× por base, `0xD8/0x6C=2`; o "4" do codex era genérico) |
| Mesmo `base` reentra em `CBB2C` repetidamente, com `+4`/`+54` já povoados antes | **Não** — só 1 entrada observada em 40s |
| `CBB2C` ocorre uma vez, mas `CB56C` recebe os mesmos objectos de novo por outro `caller_lr` | **Não** — só 2 `CB56C` totais, ambos dentro da mesma entrada de `CBB2C`, `lr` idêntico (0, não discriminante mas sem sinal de 2ª chamada) |
| `CBB2C` ocorre uma vez por objecto, mas o caminho esperado preserva estado já-construído | **SIM** — `0x4066D798` já tinha `+0x4=0x40004020` (ponteiro válido, escrito por `func_002A6608`/alloc, ver sessão anterior) **antes** do reset de `CB56C` correr, na ÚNICA passagem observada |

**Veredito: Row 4, não Row 2.** `CBB2C`/`CB56C` NÃO são invocados com
frequência errada — correm exactamente uma vez, como seria de esperar de uma
função de construção. **H_B (frequência errada) está refutada** para esta
janela de 40s: não há reentrada para bloquear, logo o diagnóstico de
"bloquear a 2ª entrada" (passo 3 do plano) não se aplica — não há 2ª entrada.

O que sobra é uma variante mais precisa de **H_A**: não é "falta um branch que
existe mas não é tomado" (o codex já tinha confirmado estaticamente que não
há load/branch condicional antes do reset — reset é literalmente
incondicional na fonte liftada). É antes uma **ordem de operações**: algo
(`func_002A6608`→alloc `func_00263178`) escreve um ponteiro válido em
`this+0x4` **antes** de `CBB2C`/`CB56C` correrem sobre o mesmo objecto, e
`CB56C` — que se comporta como um construtor bruto/reinicializador fiel —
apaga esse valor sem que nada o reponha depois. Ou isto é o comportamento
correcto do jogo original (e falta descobrir quem deveria repor `+0x4` depois
de `CB56C`, ainda não observado nesta janela), ou o jogo original tem uma
guarda que o lift não capturou (ex.: um `cmp`/branch real no PPC original que
o lifter perdeu, não um bug de lógica nosso).

## Próximo experimento (não esta sessão)

Comparar o **disassembly PPC original** (via `research_result.json`/
`functions.json`/objdump do `EBOOT.ELF` no endereço guest de `CB56C`,
`0x000CB56C`) com o corpo liftado em `ppu_recomp_000.cpp:161493-161743`,
especificamente nas primeiras ~50 instruções (antes do reset de `+0x4`/
`+0x54`), à procura de um `cmp`/branch condicional em torno de `this+0x4` ou
`this+0x54` que o lift possa ter representado como incondicional por engano
(bug de lifter) — ou confirmar que o PPC original também é incondicional ali
(caso em que o guard/repopulate fica noutra função, ainda por achar, chamada
depois de `CB56C` no fluxo natural do jogo, possivelmente só alcançável
depois do gatilho "2º movie / title FSM" que os outros wall notes já
apontaram como o gargalo a montante).

Não tentar "fix" nenhum destes dois lados sem essa comparação — mudar `CB56C`
para preservar `+0x4` arrisca divergir do CELL original se o reset for
mesmo fiel (regra do projecto: fix fiel ao comportamento original).

## Code sites
| Piece | Onde |
|-------|------|
| Novo probe | `recomp_mid_v2/patch_cbb2c_reentry_disc.py` (aplica em `ppu_recomp_000.cpp`) |
| `CBB2C` loop (2 iters, stride 0x6C, limite `base+0xD8`) | `ppu_recomp_000.cpp:162081-162173` (`loc_000CBB5C`) |
| `CB56C` reset incondicional | `ppu_recomp_000.cpp:161540` (+0x4), `:161536`→ agora `:161536+15` linhas após patch (+0x54) |
| Writer real de `+0x4` (antes do reset, 1× observado) | `func_002A6608`→`func_00263178` (não localizado em detalhe) |
| Log completo desta sessão | `cbb2c_disc_run1.log` (scratchpad da sessão) |
