# Sessão de RE — **E5**: aceite do critério 3 do ROADMAP (Fase 11) — 2026-08-04

Continuação directa de [`2026-08-04-E4-fix-verification-parede-4.md`](2026-08-04-E4-fix-verification-parede-4.md)
(fix candidato B v2, verificado in-boot 3/3 **com os gates 1-3 ON**). Conduzida com
[`docs/RE_PLAYBOOK_GOW2.md`](../RE_PLAYBOOK_GOW2.md). Corresponde ao `11-04-PLAN.md`
(Wave 3 da Fase 11) — a medição de aceite, não mais uma rodada de fix. Ao contrário do
E4, esta sessão não corrige nada: mede honestamente o critério 3 e devolve um veredicto
único, sem gates, contra a letra exacta do `ROADMAP.md`.

---

## Pergunta (uma frase)

> **Com o fix v2 da parede 4 (`patch_outer_subtag_prune.py`) aplicado e os três gates de
> diagnóstico (`PS3_LIST254_EMPTY_IF_NULL`, `PS3_POOL_NULL_IF_BAD`, `PS3_LIST547_EMPTY_IF_BAD`)
> OFF, `sys_ppu_thread_create name="AUTO_LOAD"` acontece ≥1 vez em ≥4 de 6 corridas — o
> critério 3 do ROADMAP, na sua letra exacta?**

---

## Leitura obrigatória feita ANTES de correr (Task 1 do plano)

`../gow2-recomp/lib_boot_chain_metrics.sh`, linhas 63-118 (comentário "AVISO MAIOR"): a
thread `AUTO_LOAD` só pode ser criada por `func_00146CB8`, chamada de dois sítios, **ambos
a jusante de `func_00242C94` (o loop principal do jogo) RETORNAR** em `REQUEST_EXITGAME`.
Um jogo que continua a correr (`SetFlipCommand` repetido, sem retornar do loop) tem
`thr_created=0` **correctamente** — não é uma parede, é a prova de que o loop não terminou.
O comentário mede isto numa corrida saudável: `SetFlipCommand ×2619`, `linhas após o loop
principal ×0`, `degraus da cadeia AUTO_LOAD ×0`.

Consequência para esta sessão: `thr_created=0` num smoke de 90s **não distingue**, por si
só, "o loop nunca é alcançado" de "o loop é alcançado e corre bem, mas não sai do jogo
dentro da janela". É por isso que este ledger regista `[STATE] frame=1` como sinal
complementar — é o marcador que TESTA directamente "o loop principal despachou o primeiro
estado", sem depender de o jogo terminar.

---

## Âncoras (reaproveitadas de E1/E3/E4)

| âncora | valor |
|---|---|
| fix sob teste | `games/gow2/recomp_mid_v2/patch_outer_subtag_prune.py` (v2, 11-03) |
| lift usado | `../gow2-recomp/recomp_macos_e4fix2/` (o mesmo lift já verificado in-boot pelo E4, `verify_lift` PASS) |
| binário sob teste | `../gow2-recomp/boot_gow2_f11accept` (dedicado, distinto de `boot_gow2`) |
| ferramenta de medição | `../gow2-recomp/smoke_chain_gate.sh --bin` (GATE-01/GATE-02, Fase 7) — não reimplementada |
| gates de diagnóstico (OFF nesta sessão) | `PS3_LIST254_EMPTY_IF_NULL`, `PS3_POOL_NULL_IF_BAD`, `PS3_LIST547_EMPTY_IF_BAD` |
| parede 1 (inventário ROADMAP) | `func_002545B0` — lista circular em `arg+0x7C`, `head=0`, gate `PS3_LIST254_EMPTY_IF_NULL` |

---

## Construção do binário sob teste (Task 1)

`apply_all_patches.sh recomp_macos_e4fix2` (o lift já usado pelo E4): `rc=0`,
`lift_parity PASS`, `MANIFEST PASS`, `audit_boundaries PASS`; `patch_outer_subtag_prune.py`
listado em `UNVERIFIED (delta) / NOVO` — sem `CONTRACTS.tsv` próprio, o mesmo padrão que
`patch_freelist_guard_262610.py` já tinha antes desta fase (não é uma regressão introduzida
aqui). `OUT=boot_gow2_f11accept ./build_macos.sh recomp_macos_e4fix2` — build limpo, 0 erros.

Confirmação (Task 1, mandatória) de que os hooks de sonda não vazam efeito para esta
medição: `nm boot_gow2_f11accept | grep -c gow2_midasm` → **9** símbolos, todos read-only e
gated por `getenv()` (`PS3_TYPEWALK_TRACE`, `PS3_OUTER_SUBTAG_CENSUS`) — confirmado por
`grep -n getenv hooks/gow2_midasm_hooks.cpp`. Nenhum dos três gates de diagnóstico foi
exportado nesta shell (`env | grep -c 'PS3_LIST254_EMPTY_IF_NULL\|PS3_POOL_NULL_IF_BAD\|PS3_LIST547_EMPTY_IF_BAD'`
→ `0`, confirmado imediatamente antes de lançar `smoke_chain_gate.sh`).

Produção (`recomp_macos_v2/ppu_recomp_001.cpp`) intocada durante esta sessão: hash
`136ab046d1a0ced07fc5bda330eba8263783e7732434bb5a8253e7db15d0a989`, idêntico ao registado
pelo E4.

---

## Medição (Task 2) — 6 corridas, `smoke_chain_gate.sh --bin boot_gow2_f11accept 6`

`pgrep -fl boot_gow2` **vazio** antes e depois das 6 corridas — zero órfãos (higiene G6).

| corrida | `st620` | `startseq` | `nopic` | `thr_created` (AUTO_LOAD) | `thr_end` | `r_perma` | `[STATE] frame=1` | `elo_stopped` |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 11 | 2 | 4 | **0** | 0 | 1 | **0** | AUTO_LOAD (nunca criada) |
| 2 | 11 | 2 | 4 | **0** | 0 | 1 | **0** | AUTO_LOAD (nunca criada) |
| 3 | 11 | 2 | 4 | **0** | 0 | 1 | **0** | AUTO_LOAD (nunca criada) |
| 4 | 11 | 2 | 4 | **0** | 0 | 1 | **0** | AUTO_LOAD (nunca criada) |
| 5 | 11 | 2 | 4 | **0** | 0 | 1 | **0** | AUTO_LOAD (nunca criada) |
| 6 | 11 | 2 | 4 | **0** | 0 | 1 | **0** | AUTO_LOAD (nunca criada) |

`smoke_chain_gate.sh`'s próprio sumário: `elo_stopped=nenhum em 0 de 6 (limiar: 4)`,
`elo_stopped mais frequente: AUTO_LOAD (nunca criada)`. TSV completo em
`/tmp/f11_accept.tsv` (reproduzido acima, colunas `run/st620/startseq/nopic/thr_created/
thr_end/r_perma/setflip_after_rperm/pad_total/elo_stopped/class`).

**`thr_created` (`sys_ppu_thread_create name="AUTO_LOAD"`) = 0 em 6/6 corridas.**
**`[STATE] frame=1` = 0/6 corridas** (`grep -c '\[STATE\] frame=' <log>` para as 6 = `0`
nas 6). Os dois sinais concordam: sem os gates, o boot não chega nem à leitura mais fraca
("entrou no loop e despachou o 1º estado") nem à mais forte ("saiu do jogo").

### Onde o boot pára, medido — não é silêncio, é um abort explícito

As 6 corridas abortam de forma **idêntica** (mesmo `ctr`, mesmo `lr`, mesma cadeia de
retorno):

```
[ICALL-BAD] ctr=0x00514E80 lr=0x0024E2D4 r2=0x407808B0 r3=0x00000000 r11=0x00000000 r12=0x88004044
[ppu] unresolved indirect call -> 0x00514E80
[ppu] FATAL: stuck calling 0x00514E80 (2000 times) -- aborting run
[ppu] rbp frame walk:
  #0 rbp=0x16f1a02d0 ret=_Z13func_002545D4P11ppu_context+0xC58
  #1 rbp=0x16f1a0480 ret=ps3_indirect_call+0xB60
  #2 rbp=0x16f1a0730 ret=_Z13func_0039D764P11ppu_context+0x4A0
  #3 rbp=0x16f1a0850 ret=ps3_indirect_call+0xB10
  #4 rbp=0x16f1a0b00 ret=_Z13func_0039D428P11ppu_context+0x2A8
  #5 rbp=0x16f1a0b30 ret=ps3_indirect_call+0xB10
  #6 rbp=0x16f1a0de0 ret=_Z13func_0024E270P11ppu_context+0x1030
  #7 rbp=0x16f1a1040 ret=_Z13func_0024F028P11ppu_context+0x8F8
  #8 rbp=0x16f1a1130 ret=_Z13func_0010F5E8P11ppu_context+0x27C
  #9 rbp=0x16f1a1210 ret=_Z13func_000B71B8P11ppu_context+0x53DC
  #10 rbp=0x16f1a1cf0 ret=_Z13func_002B2E74P11ppu_context+0x4FC
```

**Medido, não inferido:** `python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out
0x002545D4 --no-c` responde `(0x002545D4 esta 36 bytes dentro de 0x002545B0)`.
`func_002545D4` **é a mesma função** `func_002545B0` — a própria **parede 1** do
inventário de `.planning/ROADMAP.md` ("`func_002545B0`: lista circular em `arg+0x7C` com
`head=0`; gate `PS3_LIST254_EMPTY_IF_NULL`"). `0x00514E80` (o alvo do `ICALL` que nunca
resolve) **não cai em nenhuma função Ghidra conhecida** (`ghidra_lookup.py` — "NAO cai em
nenhuma funcao conhecida ... provavel: dados, regiao nao analisada, ou fronteira em
falta"), a mesma assinatura de "ponteiro de função lido de dados corrompidos" que a
família de bugs desta fase já mediu para a parede 1 e a parede 4.

A pilha de retorno bate, EA a EA, com a cadeia que o próprio `ROADMAP.md` (secção Fase 11)
já tinha medido a 2026-08-01: `main() → func_002B2E74 (7ª de 9) → func_000B71B8 (B71, 9ª
de 11) → não retorna` — aqui, sem os gates, o `B71` chama a parede 1
(`func_002545B0`/`func_002545D4`) e essa aborta por `ICALL` irresolúvel antes sequer de
regressar ao `B71`, muito menos ao loop principal.

---

## Veredicto: **critério 3 NÃO satisfeito — 0 de 6 corridas** (não forjado, não retentado com gates)

| # | Critério | Resultado medido |
|---|---|---|
| 3 | `thr_created(AUTO_LOAD) >= 1` em `>=4` de 6 corridas, gates OFF | **0/6** — `thr_created=0` nas seis, idêntico |
| sinal complementar | `[STATE] frame=1`, gates OFF | **0/6** — nunca alcançado |

O fix da parede 4 (`patch_outer_subtag_prune.py` v2) permanece correcto **para o seu
alvo** — o E4 já tinha medido isso, com os gates ON, e esta sessão não o desfaz nem o
re-testa (fora de âmbito: esta sessão só corre com os gates OFF, que é precisamente a
condição em que o fix da parede 4 nunca chega a ser exercitado, porque o boot aborta
ANTES, na parede 1). O que esta sessão mede, pela primeira vez nesta fase, é que **as
paredes 1-3 continuam de pé sem os gates** — exactamente a dependência que o 11-03/E4 já
tinha avisado ("TODAS as corridas desta sessão correram COM os gates 1-3 ON... O 11-04
precisa de medir se isso ainda se aplica sem o fix aplicado a mudar essa dependência").
Aplica-se: mede-se agora, de forma directa e reprodutível (6/6 idêntico), que sim.

### Sobre o comentário do `lib_boot_chain_metrics.sh` (reconciliação, não contorno)

O comentário "AVISO MAIOR" avisa que `thr_created=0` pode ser **falso alarme** — um jogo
saudável que ainda não saiu. **Não é o caso aqui.** Duas provas de que este `0/6` é uma
parede real, não um "ainda a jogar":

1. **`[STATE] frame=1` também é 0/6** — se o loop principal tivesse sido alcançado e
   despachado, mesmo sem sair do jogo, esse marcador teria disparado. Não disparou.
2. **As 6 corridas abortam com um `[ppu] FATAL` explícito** dentro da PRÓPRIA função da
   parede 1 (`func_002545B0`), antes de o `B71`/loop principal serem sequer alcançados —
   não é um timeout silencioso do smoke, é um abort medido e localizado.

O comentário do `lib_boot_chain_metrics.sh` continua correcto como aviso geral (não
perseguir `AUTO_LOAD` como se fosse "o loop despachou um frame") — mas para ESTA medição
específica, com `[STATE] frame=1` também ausente e um abort nomeado dentro da parede 1, a
leitura correcta é "a parede 1 continua a bloquear sem gate", não "o jogo ainda está a
correr bem, só não saiu a tempo".

---

## O MAPA (D-11.5) — critério 4 do ROADMAP, entrega honesta de fase não fechada

### Já eliminado por medição (fases anteriores desta sessão de trabalho — não voltar a perseguir)

| item eliminado | onde/como medido |
|---|---|
| Registry de tipos despovoado | 26 entradas, tags 0..32, fábrica válida, vtable viva (ROADMAP, medido 2026-08-01/02) |
| `tab[low16] != tab[subtag]` | `diff_mgr=0`, 6 corridas, ~885M visitas (E1, `docs/re_sessions/2026-08-03-E1-typewalk-trace.md`) |
| Recursão exponencial em `func_0041F700` | refutada como causa raiz da explosão — a causa era o walk exterior (`func_0041FF70`) enumerar o registo WAD, não a recursão em si (E1/E3/E4) |
| Falta de pop / assimetria push-pop | refutadas (ROADMAP "já eliminado por medição") |
| `hflags & 4` como poda | refutada (ROADMAP) |
| `0x47D00000` como referência válida | era o paliativo TYPE15, removido 2026-08-01 com prova de não-regressão |
| Laço sem-fim do walk exterior (`no_rep` crescendo) | **fechado por esta sessão de trabalho**: `no_rep=0` em 3/3 corridas com o fix v2, gates ON (E4) |
| Mecanismo v1 do fix candidato B (mutar `gpr[25]` antes do compare `iVar6==iVar2`) | REFUTADO por medição in-boot — zero efeito (E4, Ronda 1) |
| `h_child` (`gpr28+0x88`) como classificação relevante do nó empurrado | REFUTADO — é lixo (`0x3F800000`, float de matriz); a classificação real vem de `gpr28+4`, o próprio nó de topo (E4) |

### Novo, medido por ESTA sessão (11-04/E5)

| item | estado |
|---|---|
| Fix da parede 4 (v2) sem os gates 1-3 | **NÃO CHEGA A SER EXERCITADO** — o boot aborta na parede 1 antes de alcançar o walk exterior `func_0041FF70` |
| Parede 1 (`func_002545B0`) sem `PS3_LIST254_EMPTY_IF_NULL` | **CONFIRMADA COMO BLOQUEIO ACTIVO** — `[ppu] FATAL: stuck calling 0x00514E80` dentro da própria função, 6/6 corridas, idêntico |
| Alvo do `ICALL` irresolúvel `0x00514E80` | não é uma função Ghidra válida — mesma classe de corrupção (ponteiro lido de dados/offset errado) já medida para as paredes 1/4 |
| `AUTO_LOAD`/`REQUEST_EXITGAME` (aviso do `lib_boot_chain_metrics.sh`) | lido e reconciliado — não é a explicação deste `0/6`: `[STATE] frame=1` também ausente e há um abort nomeado antes do loop principal, não um "jogo ainda a correr" |

### O que falta, por ordem, para quem retomar

1. **Parede 1** (`func_002545B0`, gate `PS3_LIST254_EMPTY_IF_NULL`) — mesma classe de bug
   já resolvida para a parede 4 (um consumidor de lista a ler um campo de dados como
   ponteiro/sentinela). É o PRÓXIMO bloqueio linear: sem ela corrigida, as paredes 2 e 3
   nunca são sequer alcançadas sem gate, e o fix da parede 4 (já correcto) nunca chega a
   ser exercitado pelo caminho natural.
2. Paredes 2 (`func_002182A4`) e 3 (`func_002547AC`) — inventariadas, ainda não atacadas.
3. Só depois de 1-3 corrigidas faz sentido re-correr esta mesma medição de aceite
   (`smoke_chain_gate.sh --bin <binário com 1-3 corrigidas> 6`) — reusar este ledger como
   modelo, não reimplementar a ferramenta.

---

## O que esta sessão NÃO afirma

- **Não** afirma que o fix da parede 4 está errado ou incompleto — o E4 já o verificou
  correcto para o seu alvo, com os gates ON, e esta sessão não o retesta nessa condição.
- **Não** reabre a decisão D-11.2 (B) nem o mecanismo v2 do fix candidato B.
- **Não** trata `thr_created=0` como "o jogo ainda está a correr bem" sem prova — o
  `[STATE] frame=1` ausente e o abort nomeado dentro da parede 1 são as duas provas de que
  isto é uma parede, não o aviso do `lib_boot_chain_metrics.sh` a aplicar-se.
- **Não** marca REG-03 como fechado — fica `[ ]` em `.planning/REQUIREMENTS.md`, com nota
  datada.
- **Não** fecha a janela #19 de `.planning/WINDOWS.md` — a explosão sem gates continua por
  reproduzir de facto, precisamente porque o boot nem chega lá sem as paredes 1-3
  corrigidas. Abre-se a janela #21, nova, com este achado específico.
- **Não** toca em pixels, shaders ou `SHADERSRC`.
