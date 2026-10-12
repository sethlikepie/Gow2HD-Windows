# O caminho da bala: quem não fazia null-check de func_00263040 — encontrado,
# corrigido, e a parede move-se (não fecha)

**Data:** 2026-07-31 · **Medido 6/6 corridas**, `boot_gow2_f263040guard2`
(clone de teste `recomp_macos_v2.f263040guard`, nunca `boot_gow2`/`recomp_macos_v2`
de produção como binário). Recipe: `smoke_chain_gate.sh --bin ... 6`
(`arm_menu_fast_recipe`, timeout 90s, kill sempre por PID). Diagnóstico com
`PS3_TRACE_MEMORY=1 PS3_TRACE_OOBRA=1` (evento canónico MEMORY/OOB,
`ps3_mem_oob_report`, resolve `<fn>` do chamador direto via `dladdr`).

## Onde a nota anterior parou

`notes/2026-07-31-repor-conjunto-completo-gate-6-corridas.md` fechou o 6.o
`FREELIST-TAG-GUARD` (`func_00263040`, commit `15797e9`): a 2.a chamada ao
construct TYPE15 deixa de ficar presa em loop infinito, mas o gate de 6
corridas media **5/6** a chegar a `AUTO_LOAD (thr_end)` — sempre na MESMA
linha: `[POSTINTRO] CB56C obj+8=product=...` seguido de silêncio total de
progresso (só `SPUJOB`/`MOVIEFSM` a tiquetaquear).

## Quem não fazia null-check (medido, não inferido)

Auditoria estática dos 365 call-sites diretos de `func_00263040` no lift:
`func_00263680` (o "alloc + stamp de cabeçalho" da chunk arena) é o único que
desreferencia r3 de forma incondicional logo a seguir à chamada — os outros
call-sites só guardam r3 num registo ou passam adiante sem o tocar de
imediato. Corpo relevante (antes do fix):

```c
ctx->lr = 0x002636D0; func_00263040(ctx); DRAIN_TRAMPOLINE(ctx);
...
vm_write32(ctx->gpr[3] + 0xC, ctx->gpr[29]);
vm_write32(ctx->gpr[3] + 0x0, ctx->gpr[0]);
vm_write32(ctx->gpr[3] + 0x10, ctx->gpr[28]);
vm_write16(ctx->gpr[3] + 0x16, ctx->gpr[27]);
vm_write32(ctx->gpr[3] + 0x8, ctx->gpr[0]);
vm_write16(ctx->gpr[3] + 0x14, ctx->gpr[31]);
```

Mesmo padrão já catalogado em `002550C8`/`002550E8` (`patch_fios_stream_guards_install.py`):
"o sub-alloc pode devolver 0, o código natural carimba tags sem verificar
null". `func_00263680` é o 3.o sítio da mesma família — não um bug novo, um
sítio que ainda não tinha o guard.

Confirmado dinamicamente pelo próprio log (run1 do gate, ANTES de qualquer
crash visível):

```
[FREELIST-TAG-GUARD] 263040 entry head=0x00000000 -> abort r3=0
[ALLOC-NULL-GUARD] 263680 sub-alloc r3=0 -> skip stamp@0     <- o fix a disparar
[FREELIST-TAG-GUARD] 263040 entry head=0x00000000 -> abort r3=0
[ALLOC-NULL-GUARD] 263680 sub-alloc r3=0 -> skip stamp@0
[FREELIST-TAG-GUARD] 263040 entry head=0x00000000 -> abort r3=0
[ALLOC-NULL-GUARD] 263680 sub-alloc r3=0 -> skip stamp@0
```

`func_00263680` É de facto exercitado no caminho TYPE15/CB56C, 3x por corrida,
sempre nas 5 corridas que chegam ao ponto.

## O que NÃO era o caminho da bala (refutado, não assumido)

Antes de decidir o sítio, medi qual função dominava os eventos MEMORY/OOB da
rajada (run2 do gate, sem o fix ainda, `PS3_TRACE_MEMORY=1`):

```
7281  func_0024C878+0x414   (read)
2253  func_0024C878+0x4F4   (write)
   8  func_002330F8+0x12C
   2  func_0039F82C+0x2CC
   2  func_002332B8+0x1B0
```

`func_0024C878` (uma rotina genérica de "notify de listeners", ~50 call-sites
espalhados pelo motor inteiro) responde por **>99% dos eventos MEMORY/OOB**
da rajada. Mas o seu próprio código FAZ null-check do ponteiro primário (salta
se nulo) — a divergência vem de percorrer um ponteiro/contador já corrompido
por outro lado, não de um null não-verificado. Não tem chamada nenhuma a
`func_00263040`/`func_00263178` no seu corpo (confirmado por leitura direta).
Era um sintoma a jusante, não o "chamador que não faz null-check" que a
tarefa pedia — por isso não recebeu guard nenhum aqui.

## O fix

`recomp_mid_v2/patch_263680_alloc_null_guard.py` (idempotente, aplica-se a
qualquer chunk que contenha `func_00263680`, marcador
`[ALLOC-NULL-GUARD] 263680`). Salta o carimbo do cabeçalho e propaga r3=0 ao
chamador de `func_00263680` via o próprio epílogo natural da função (restore
dos callee-saved + return) — não inventa nenhum valor plausível, é o "sem
memória" honesto que o CLAUDE.md permite explicitamente para este padrão.
Commit `4fe7fef`.

## Resultado medido: a rajada desaparece, a parede desloca-se (não fecha)

Gate de 6 corridas, `boot_gow2_f263040guard2` (mesmo clone `+` este fix,
`FORCE_REBUILD_LIFT=1`):

```
run  st620  startseq  nopic  thr_end  r_perma  elo_stopped
1    11     2         4      0        1        AUTO_LOAD (thr_end)
2    11     2         4      0        1        AUTO_LOAD (thr_end)
3    11     2         4      0        1        AUTO_LOAD (thr_end)
4    11     2         4      0        1        AUTO_LOAD (thr_end)
5    11     2         4      0        1        AUTO_LOAD (thr_end)
6    1      0         0      0        0        intro (st620)
```

**5 de 6** — idêntico ao gate anterior em `nopic`/`r_perma`/`st620` (nenhuma
regressão nessas métricas), a 6.a corrida presa na intro é a mesma
flakiness já documentada (não uma regressão deste fix).

Mas o COMPORTAMENTO na parede mudou, qualitativamente, nas 5 corridas que lá
chegam (idêntico nas 5, byte a byte na forma):

- `[ALLOC-NULL-GUARD] 263680` dispara exactamente 3x por corrida.
- `func_0024C878` deixa de aparecer em QUALQUER evento MEMORY/OOB — **0
  ocorrências em 5/5 corridas** (antes: >99% dos eventos, milhares por
  corrida). A rajada de OOB que dominava a janela de 90s desaparece por
  completo.
- O boot avança para marcadores NUNCA vistos antes nesta cadeia:
  `[TYPE15] product list CLOSE-PRESERVE prod=... nodes=12`,
  `[POSTINTRO] B71 after alloc0x2024`, `[POSTINTRO] B71 393E0 HLE-lite`,
  `[POSTINTRO] B71 icallA product r3=0x00000030 reused=0`,
  `[POSTINTRO] B71 skip icallB (null product)` — este último mostra o PRÓPRIO
  guest a fazer o SEU null-check correctamente e a saltar o passo seguinte,
  em vez de martelar lixo.
- Para numa parede NOVA, determinística e com nome: uma tempestade de
  chamada indirecta a alternar `0x40678C90`/`0x000B9354` que aciona o
  watchdog EXISTENTE do motor (`[ppu] FATAL: stuck calling 0x000B9354
  (2000 times) -- aborting run`) — um abort limpo e diagnosticável, não mais
  um silêncio indefinido de threads de fundo. `0x000B9354` não corresponde à
  entrada de nenhum `func_` lifted (provavelmente um alvo de chamada
  indirecta calculado sobre um ponteiro/tabela ainda inválido — não
  investigado a fundo, fica para a próxima sessão).

`thr_auto_load() end` continua em **0/6** — o critério do pedido (`>=1` em
`>=4/6`) **não fecha**. Este fix elimina um bug real e desloca a parede para
mais tarde e mais nomeável, mas não é o fecho do marco v1.1.

## Auditoria de fallthrough cross-fragment (pedida no CLAUDE.md, tentada e refutada como abordagem)

Tentei o heurístico sugerido — `g_trampoline_fn = func_X` onde o alvo tem EA
menor que o sítio — como proxy para "fallthrough para trás disfarçado" (o
padrão do fix `2550C8`→`255178`). Resultado: **52.560 de 168.696** sítios de
trampolim (~31%) batem o filtro (alvo menor E a menos de 0x400 bytes), mesmo
restringindo a trampolins na cauda do corpo da função. Isto é ruído, não uma
lista de defeitos: uma chamada/goto real para trás para uma sub-rotina
partilhada mais cedo no ficheiro é o padrão NORMAL de código PPC compilado
(loops, saída de erro comum, utilitários partilhados) — o bug real do
`2550C8` não era sequer "para trás" (o alvo errado, `2550E8`, era mais
PRÓXIMO e mais A FRENTE do que o correcto, `255178`, que ficava mais longe
ainda para a frente). Concluo, sem forjar um resultado: o heurístico
"EA do alvo < EA do sítio" não discrimina o bug procurado; uma auditoria
real precisaria de cruzar cada candidato com o disassembly PPC real do
binário original (para distinguir uma fallthrough implícita mal-computada
de uma `b`/tail-call explícita e legítima) — fora do orçamento desta sessão.
**Não fiz essa auditoria mais funda; fica registada como próximo passo, não
como resultado.**

## Estado dos artefactos

- `recomp_mid_v2/patch_263680_alloc_null_guard.py`: novo, idempotente,
  commitado (`4fe7fef`). Aplicado e verificado em
  `recomp_macos_v2.f263040guard` (clone de teste) E em `recomp_macos_v2`
  (produção, gitignored — aplicação normal pós-relift, `boot_gow2` binário
  de produção **NÃO reconstruído nem tocado** nesta sessão).
- `boot_gow2_f263040guard2` / `recomp_macos_v2.f263040guard` (com o fix):
  clone e binário de teste desta medição, preservados.
- `/tmp/gate_263680_guard.tsv`, `/tmp/chain_gate_boot_gow2_f263040guard2_run{1..6}_*.log`,
  `/tmp/oob_probe*_f263040guard_*.log`: TSV e logs brutos das corridas
  (não commitados — `/tmp`).

## Próximo passo sugerido

Instrumentar especificamente o icall storm `0x40678C90`/`0x000B9354`
(dump do que está em `0x40678C90` e de quem definiu esse valor como alvo de
chamada — provavelmente outro ponteiro/tabela ainda não inicializado
correctamente na cadeia B71/TYPE15) com o mesmo método já usado para
`func_0039E794` (`TYPE15-SHELL-PROBE`). É a parede seguinte, com nome e
evidência — não mais um "hang silencioso".
