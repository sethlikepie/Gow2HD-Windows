# Repor o conjunto completo (não peça a peça) — gate de 6 corridas, e a parede
# seguinte tem nome e evidência

**Data:** 2026-07-31 · **Medido 6/6 corridas**, `boot_gow2_type15_combined`
(binário de teste dedicado, clone `recomp_macos_v2.type15_combined`, nunca
`boot_gow2`/`recomp_macos_v2` de produção como binário final). Recipe:
`smoke_chain_gate.sh --bin ... 6` (usa `arm_menu_fast_recipe`, timeout 90s,
kill sempre por PID via `measure_one()`).

## Parte 1 — inventário completo (não só TYPE15/FACTORY)

`inventory_lift_markers.py` alargado (varredura 1: diff de TODO marcador
`[TAG]`/`TAG-COM-TRACOS` entre `recomp_macos_v2.pre_v4` e `recomp_macos_v2`,
não restrito a TYPE15/FACTORY) + varredura 2 real (`apply_all_patches.sh
recomp_macos_v2 --status`, aplicando de facto o catálogo completo de 100
`patch_*.py` sobre o lift de produção — a reaplicação pós-relift normal).

**Antes desta sessão fechar as 3 peças TYPE15** (commits `5d5e5e0`, `6e7be2f`,
`5a45b78`): 23 marcadores ausentes.
**Depois de aplicar o catálogo completo** (esta sessão, antes do gate): **13
ausentes** — nenhum com prefixo `TYPE15-`/`FACTORY-` (confirmado por grep
dedicado: `TYPE15*` ref=6 cur=14, zero só-na-referência; `FACTORY*` ref=1
cur=1, zero só-na-referência). **A alegação de "faltam 7 TYPE15 e 6 FACTORY"
estava desactualizada** — media o estado ANTES dos 3 commits de hoje, não o
estado corrente.

### Classificação dos 13 restantes (funcional vs diagnóstico) — MEDIDO por inspecção directa de código, não inferido

| Marcador | Classe | Evidência |
|---|---|---|
| `CC9D0-LIVE-SKIP`, `CC9D0-YIELD` | **DIAGNÓSTICO** (opt-in) | Bloco gated por `PS3_CC9D0_YIELD`/`PS3_CC9D0_LIVE_SKIP`, ambos OFF por default. O próprio `patch_cc9d0_live_yield.py` documenta: *"Diagnostic only; permanent skip of live product is NOT a product fix"*. classe=PROBE no catálogo. |
| `DONE-HEX` | DIAGNÓSTICO | `fprintf` puro de hex-dump de memória (`ppu_recomp_001.cpp:24291`), sem `vm_write`/mutação de estado. |
| `F2B-42A118-PROBE` | DIAGNÓSTICO | Nome diz PROBE; corpo é só `fprintf` de estado (`ppu_recomp_005.cpp:412875+`), a função continua com o corpo lifted normal por baixo. |
| `F2B-STREAM-SEED` | funcional mas **opt-in OFF** | Faz `vm_write32` real quando `PS3_FIOS_STREAM_SEED=1` (comentário: *"opt-in ... Default OFF"*). A recipe canónica não exporta esta env var — inerte no gate default. Não reposto agora (fora do escopo "o que o re-lift apagou E que corre por default"). |
| `FIOS-CLEAR-PROBE` (3 sites: `func_003062C8`, `func_0030633C`, `func_003063D8`) | DIAGNÓSTICO | Comentário + trace `fprintf` gated por `PS3_TRACE_FIOSSCHED`, prependido ao corpo REAL da função liftada. **Corpo funcional (as `vm_write32` que zeram campos) é idêntico byte-a-byte em `recomp_macos_v2` actual** — confirmado por diff directo das 3 funções. O marcador em falta é só o comentário/trace, não código. |
| `FIOS-FO-DUMP`, `FO-DUMP` | DIAGNÓSTICO | `fprintf` de dump de bytes do FO (`ppu_recomp_001.cpp:116282+`), sem mutação. |
| `FIOS-FREELIST-PROBE` | DIAGNÓSTICO | `fprintf` puro; o bloco funcional VIZINHO real (`FIOS-HOST-POP`, gated por `PS3_FIOS_HOST_POP`, **default ON**) está confirmado presente e intacto em `recomp_macos_v2` (`ppu_recomp_001.cpp:128561`) — o que importa não desapareceu, só o trace ao lado. |
| `FIOS-MEDIA16C-PROBE` (2 sites) | DIAGNÓSTICO | `{ fprintf(...); fflush(...); }` de uma linha, sem mutação; a `vm_write32(media+0x16C,...)` real na linha seguinte não depende do marcador. |
| `FIOS-SHUTDOWN-ENTER-002B4894`, `-0030E018`, `-0030E878` | DIAGNÓSTICO | Marcador de entrada de função + `fprintf`; corpo real (a sequência completa de `vm_write64`/chamadas `func_...`) inspeccionado nos 3 sites e presente sem alteração. |

**Conclusão da Parte 1: zero blocos funcionais adicionais por repor.** Os 13
marcadores restantes são todos trace/diagnóstico (comentário + `fprintf`
gated) cujo corpo funcional por baixo já está intacto no lift actual, ou são
opt-in OFF por default (`F2B-STREAM-SEED`, `CC9D0-*`). **O conjunto completo
já reposto nesta sessão (`TYPE15-FL-REHOME-FIXUP` + `TYPE15-FL-SHELL-REHOME`
+ `TYPE15-UNSTICK-SKIP`) é, de facto, o conjunto completo** face ao lift de
referência — não há mais nada a repor por este caminho de comparação.

## Parte 2 — gate de 6 corridas com o conjunto completo

Clone `recomp_macos_v2` (já com o catálogo inteiro reaplicado, incluindo as 3
peças TYPE15) para `recomp_macos_v2.type15_combined`; build dedicado
`OUT=boot_gow2_type15_combined ./build_macos.sh recomp_macos_v2.type15_combined`
(link ok, 115M, `recomp_macos_v2`/`boot_gow2` de produção não tocados como
binário). `smoke_chain_gate.sh --bin boot_gow2_type15_combined 6`.

```
run  st620  startseq  nopic  thr_end  r_perma  elo_stopped
1    11     2         4      0        1        AUTO_LOAD (thr_end)
2    11     2         4      0        1        AUTO_LOAD (thr_end)
3    1      0         0      0        0        intro (st620)
4    11     2         4      0        1        AUTO_LOAD (thr_end)
5    11     2         4      0        1        AUTO_LOAD (thr_end)
6    11     2         4      0        1        AUTO_LOAD (thr_end)
```

**5 de 6** atingem a MESMA parede (`AUTO_LOAD (thr_end)`), com as MESMAS
métricas intermédias (`nopic=4`, `r_perma=1`, `st620=11`, `startseq=2`) —
muito mais estável que o `UNSTICK` isolado testado ontem (**1 de 3**, as
outras 2 nem saíam da intro). A hipótese da nota anterior
(`2026-07-31-repor-peca-a-peca-nao-chega.md`) confirma-se: o `UNSTICK`
sozinho abria janelas de corrida; com as outras duas peças da mesma cadeia
(`FL-REHOME-FIXUP`, `FL-SHELL-REHOME`) a instabilidade desaparece quase por
completo.

**Critério do pedido (`thr_end>=1` em ≥4/6): NÃO cumprido** — `thr_end=0` em
6/6. Sinais substitutos pedidos (`cellPadGetData>0`, `SetFlip_after_R_Perm>0`)
também não dispararam (`pad_total=0`, `setflip_after_rperm=0` em todas). Isto
é dito sem inferência: o gate não fechou.

### A corrida 1 e a corrida 3 (as duas que fogem ao padrão)

- **Corrida 1** (a primeira do lote, cold-start): parou em `file_pos=5188048
  /20169344` do stream R_PermA — só 26% lido aos 90s. Não é stall: o log
  termina a meio de leituras activas (`F2B-STREAM-ENSURE`/`FILL` sucessivos,
  sem gap), consistente com arranque mais lento (cache de disco frio),
  morrendo ainda em progresso quando o timeout do gate corta.
- **Corrida 3**: presa em `st620=1` (intro), nunca sai — a flakiness já
  documentada (variabilidade alta entre corridas, conhecida desde antes desta
  sessão). Sem sinal de crash/fatal no log.

### A parede seguinte, com nome e evidência (não é mais NULL nem HANG genérico — é ESTE ponto exacto)

Nas 4 corridas que avançaram mais longe (2, 4, 5, 6), **todas** terminam na
MESMA linha, byte a byte:

```
[POSTINTRO] CB56C icall2 tab=0x00868D48 idx=0x0 ent=0x40637C08 code=0x00424048
[POSTINTRO] CB56C obj+8=product=0x4F72626F attach=full reused=0
```

seguida só de `[SPUJOB] spu job returned cleanly` e `[MOVIEFSM] st620 0 -> 0`
em loop até ao timeout — nenhuma outra linha de progresso depois disto em
nenhuma das 4 corridas.

Confirmado também (corrida 2, grep dedicado): **o stream R_PermA completa os
20.169.344 bytes inteiros** (`file_pos=20169344/20169344`) antes deste ponto —
a Parede [C] continua vencida de forma sólida no build combinado, o gargalo
NÃO é a leitura do WAD.

`product=0x4F72626F` é o literal ASCII `'Orbo'` — o MESMO valor identificado
em `notes/2026-07-31-type15-fl-slot-medido-fix-e-nova-parede.md` como o
produto (agora legítimo ou ainda suspeito — não distinguido aqui) devolvido
pela fábrica TYPE15 no caminho `CB56C`. O padrão — dispatch `CB56C icall2`
bem-sucedido, `attach=full`, e a seguir SILÊNCIO TOTAL de progresso (só
threads de fundo a tiquetaquear) — é qualitativamente o MESMO hang já
descrito em `notes/2026-07-31-type15-shell-rehome-fix-e-nova-parede-hang.md`
("HANG em vez de NULL": a chamada de construct real do guest nunca retorna).
**Aqui reproduz-se o mesmo sintoma de forma independente, pela recipe padrão
completa do gate (não a probe dedicada isolada), 4 de 4 vezes que o boot
chega a este ponto.**

Duas hipóteses não distinguidas (INFERÊNCIA, não medido):
1. É exactamente o mesmo hang do `vt[+0x14]` da nota anterior — o método real
   de construct espera uma free-list com mais de uma entrada / um `head` que
   avance, e a nossa REPLENISH sintética nunca cria essa 2ª entrada.
2. É um segundo objecto/caminho diferente (`CB56C icall2` com `idx=0x0`) que
   nunca tinha sido alcançado antes (por ficar atrás da instabilidade do
   `UNSTICK` isolado) — precisa de instrumentação própria para confirmar se é
   o MESMO ponto de código ou um vizinho.

## Estado dos artefactos

- `notes/2026-07-31-fase10-inventario-marcadores.md`: regravado pelo
  `inventory_lift_markers.py` corrido nesta sessão (13 ausentes, não 23).
- `recomp_macos_v2` (produção, gitignored): recebeu a reaplicação REAL do
  catálogo completo de patches (as 3 peças TYPE15 + tudo o resto que já
  estava a aplicar-se) — é a manutenção pós-relift normal, não um
  experimento novo; as 3 peças TYPE15 já estavam commitadas e validadas
  isoladamente antes de hoje. `boot_gow2` (binário) NÃO foi reconstruído
  nem tocado nesta sessão.
- `recomp_macos_v2.type15_combined` / `boot_gow2_type15_combined`: clone e
  binário de teste dedicados desta medição, preservados para referência.
- `/tmp/gate_type15_combined.tsv`, `/tmp/chain_gate_boot_gow2_type15_combined_run{1..6}_*.log`:
  TSV e logs brutos das 6 corridas (não commitados — `/tmp`).

## Próximo passo sugerido

Instrumentar especificamente `CB56C icall2 idx=0x0 ent=0x40637C08` /
`code=0x00424048` (dump do alvo do `icall2` e do `this` antes/depois, como a
probe `TYPE15-SHELL-PROBE` já fez para `func_0039E794`) para confirmar se é
o MESMO `vt[+0x14]` da nota do hang ou um segundo caminho. Distingue as duas
hipóteses acima sem precisar de outra rodada de gate completo.
