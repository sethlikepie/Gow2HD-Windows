# Phase 3: Re-lift num diretório novo com gate de baseline - Context

**Gathered:** 2026-07-26
**Status:** Ready for planning
**Mode:** Autónomo — tudo abaixo é MEDIDO nesta sessão

<domain>
## Phase Boundary

Regenerar o lift a partir do EBOOT com um comando, sem arriscar o lift de produção, e
provar que o lift novo não perdeu comportamento.

**Herda da Fase 2** (gap declarado, migrado para aqui): provar por `nm` sobre um `boot_gow2`
real que `host_gow2_factory.o` e `host_gow2_f2b.o` entram no binário. Hoje o guard dá SKIP
nos três lifts porque todos têm as definições embutidas.
</domain>

<measured_state>
## Estado MEDIDO — não re-derivar

### Critério 1 — `RELIFT=1`: NÃO EXISTE

`grep RELIFT build_macos.sh` → 0 ocorrências, nos dois repos. O script aborta em
`build_macos.sh:46` com `no lift output in $LIFT -- run ppu_lifter.py first`. É trabalho real.

Existe um script de referência que já faz a cadeia inteira e funcionou três vezes hoje
(scratchpad, não versionado): lift → `apply_all_patches.sh` (2 passagens) → sanidade do
preâmbulo → `verify_lift_baseline.sh` → build. O comando do lifter é:

```
python3 $PS3/tools/ppu_lifter.py EBOOT.ELF --functions functions.json -o <dir> -j 4
```
15 s, 7 chunks, 4,03 M linhas. Imprime hoje:
`Truncated-bounds repair: extended 180 function(s), 18984 byte(s) of code recovered`
e `0 fallback stubs (every referenced target is defined)`.

### Critério 3 — o gate: 219/246, e o critério como está escrito é INATINGÍVEL

Evolução medida hoje: 211/244 → (Fase 2 acrescenta 2 PREAMBLE) 213/246 → (conversões OPD
repostas) **219/246**. Restam **2 AUSENTE** e **25 A MENOS**.

**Os 2 ausentes:**

| Marcador | Classificação | Prova |
|---|---|---|
| `[OPDISP]` | **OBSOLETO** | O lifter materializa sozinho a jump table de `func_002A209C`: emite `switch ((uint32_t)ctx->ctr)` cobrindo **54/54** alvos — exactamente `0x2A2134` + os offsets que o patch injectaria. Comportamento preservado nativamente; só a probe sumiu. O próprio `patch_jumptable_2a209c.py` já detecta e reporta isto. |
| `[WADLD-ALLOC]` | **ÓRFÃO REAL** | Nenhum `patch_*.py` o escreve. Busca exaustiva nos dois repos. |

**O critério 3 é insatisfazível por construção, e há prova disso:**

O marcador `PREAMBLE ps3_indirect_call` passou a aparecer como *A MENOS*
(`esperado >=15795, encontrado 15771`) **porque o lift MELHOROU**: as 24 conversões OPD
repostas transformaram 24 `ps3_indirect_call` em `ps3_call_opd`. Um limiar absoluto `>= N`
faz o gate **falhar quando o lift fica melhor**. Isto não é uma opinião — é a medição.

Somar a isso: o lift antigo tinha 31 chunks e o novo tem 7; 1919 das 4263 funções "perdidas"
caem **fora de qualquer secção executável** (o lift antigo desassemblou dados). Limiares
absolutos calibrados num lift de 31 chunks não são um critério válido para um de 7.

### Critério 4 — "nenhum marcador sem dono": também inatingível como escrito

O ROADMAP fala em "21 marcadores de comportamento"; o MANIFEST tem **246**, um por linha.
Os dois números nunca foram reconciliados. E há pelo menos um órfão real confirmado
(`[WADLD-ALLOC]`), mais o `[OPDISP]` que não deve ter dono porque é obsoleto.

### O que está por trás dos 25 "A MENOS" — e é o achado grave

Não é ruído de contagem. É o mesmo defeito sistémico que já custou 36 conversões OPD:

> **Um patch que corre e não faz nada devolve `rc=0`.** O `apply_all_patches.sh` classifica
> por `(rc, ficheiro mudou)`: `rc=0` + inalterado ⇒ `ALREADY-APPLIED`, que não conta para
> `n_failed` nem muda o exit. Não há nada no runner que distinga *"já aplicado"* de
> *"não casou nada"*.

Medido: `patch_14b1f0_opd.py` imprime `replaced pat1=0 pat2=0 / remaining indirect in
region: 12 / OK` e devolve `rc=0`. Spot-check de outros 5 patches: todos `rc=0`, ficheiro
inalterado, `ALREADY-APPLIED` — um chega a imprimir `score ok=21 fail=0` — enquanto blocos
funcionais inteiros continuam ausentes do lift.

Números que sobram, todos com dono identificado e todos com este defeito:

| Falta | Quantidade | Dono |
|---|---|---|
| conversões `ps3_call_opd` | 12 (35→47) | `patch_14b1f0_opd.py`, todas em `func_0014B1F0` |
| guardas `0x4F000000u` | 35 (76→111) | ~30 patches, 13 funções |
| famílias de marcador degradadas | 9 (eram 17; 6 repostas) | maiores: `[POSTINTRO]` 74→20, `[FIOSOPEN]` 70→35 |

**O `run_checks` do `apply_all_patches.sh` tem 5 verificações e nenhuma conta conversões
por função.** É por isso que um lift a 35/47 chamadas e a 76/111 guardas passa `--check`
a verde.
</measured_state>

<decisions>
## Decisões

### D-3.1 — O critério 3 é REFORMULADO, com justificação escrita
Não por conveniência: por ter prova de que o critério actual pune melhorias (o caso
`ps3_indirect_call` acima). O gate passa a exigir, em vez de `rc=0` contra limiares
absolutos:
- **zero marcadores AUSENTE** que não estejam declarados obsoletos com prova no MANIFEST;
- para os *A MENOS*, um **limiar por marcador revisto contra o lift actual**, com a
  diferença justificada por escrito (des-duplicação de chunks vs perda real);
- marcadores cuja contagem CAI porque o lift melhorou (conversão de `ps3_indirect_call`
  para `ps3_call_opd`) passam a ser medidos como **soma**, não isoladamente.

### D-3.2 — `[OPDISP]` sai do inventário como OBSOLETO, com prova no MANIFEST
Não se apaga uma linha em silêncio. A linha ganha um campo/comentário a dizer porquê e a
apontar para a evidência (o `switch` de 54/54 que o lifter emite). Um inventário que perde
entradas sem rasto é pior que um inventário desactualizado.

### D-3.3 — `[WADLD-ALLOC]` precisa de instalador, como o B71 e o BA808
Mesmo padrão da Fase 2. Se se provar obsoleto, aplica-se o D-3.2 em vez disto.

### D-3.4 — O critério 4 passa a "todo marcador tem dono OU justificação de obsolescência"
A formulação actual ("nenhum sem dono") é inatingível enquanto houver marcadores que
*não devem* ter dono por serem obsoletos. A reformulação mantém a fasquia — nada fica sem
explicação — sem exigir o impossível.

### D-3.5 — `RELIFT=1` NUNCA toca em `recomp_macos_v2`
Escreve sempre num directório novo. A promoção é um passo separado e explícito (critério 2).
Verificação por **conteúdo** (hash), não por mtime.

### D-3.6 — Fechar o gap herdado da Fase 2 aqui
Um lift acabado de gerar, ANTES da cadeia de patches, não tem as definições de
`host_gow2_factory` nem do F2B — é o único momento em que o guard dispara COMPILE. Provar
por `nm` sobre o `boot_gow2` resultante que os dois `.o` entram. Se o lift limpo não linkar
sem os patches, isso é em si um resultado a registar.

### D-3.7 — Corrigir `patch_14b1f0_opd.py` está DENTRO desta fase
São as 12 conversões que faltam para 47 e a causa está diagnosticada. As 35 guardas e as
9 famílias ficam para a **Fase 4**, que é literalmente sobre isto.
</decisions>

<code_context>
| Caminho | Papel |
|---|---|
| `games/gow2/build_macos.sh` | onde `RELIFT=1` entra; aborta hoje na linha 46 |
| `games/gow2/apply_all_patches.sh` | runner; classifica por `(rc, mudou)` — o defeito |
| `games/gow2/apply_all_patches.sh` `run_checks()` | 5 verificações, nenhuma conta conversões |
| `games/gow2/verify_lift_baseline.sh` | o gate; `gen_manifest.py --verify` |
| `games/gow2/lift_baseline/MANIFEST.tsv` | 246 marcadores |
| `games/gow2/recomp_mid_v2/patch_14b1f0_opd.py` | as 12 conversões em falta |
| `../gow2-recomp/recomp_macos_v2` | produção (regenerado + patches) |
| `../gow2-recomp/recomp_macos_v2.pre_v4` | referência histórica; dá 246/246 |

## Armadilhas verificadas nesta sessão

- **`-DCMAKE_BUILD_TYPE=Release` desactiva `assert()`** (injecta `-DNDEBUG`). Usar
  `-DCMAKE_BUILD_TYPE=` vazio. Build pronto em `build-tests/`. Baseline: **21/21**.
- **Nunca `re.S` num regex que apaga** — truncou 3 chunks do lift a 2 linhas hoje.
  Delimitar explicitamente o fim de uma região, nunca inferi-lo.
- **O `ctest` regenera** `runtime/spu/tests/out_*/spu_recomp.[ch]`. Restaurar com
  `git checkout --`, nunca commitar.
- **Working tree alheio:** `.planning/codebase/TESTING.md`, `scripts/test_python.sh`.
- **Matar o boot por PID** (TERM → 9). Nunca `pkill -f boot_gow2`.
- **Smoke M0:** `./smoke_m0_baseline.sh <bin>`; gate ≥4 de 6; produção dá 11 em 5–6/6.
  O run ocasional preso em 1 é flakiness de timing (log ~3180 vs ~3230 linhas, e faz o
  allocate de `0x7D00000`) — verificar isso antes de chamar regressão.
</code_context>

<specifics>
## Critérios, com o estado real

| # | Critério do ROADMAP | Estado |
|---|---|---|
| 1 | `RELIFT=1 ./build_macos.sh` gera `recomp_macos_v3` e compila; sem `RELIFT` nada muda | POR FAZER |
| 2 | `recomp_macos_v2` intacto após `RELIFT=1` (conteúdo, não mtime) | POR FAZER |
| 3 | `verify_lift_baseline.sh` sai 0 | **INATINGÍVEL como escrito** → D-3.1 |
| 4 | Cada marcador tem escritor; nenhum sem dono | **INATINGÍVEL como escrito** → D-3.4 |
| + | (herdado da Fase 2) provar por `nm` que os `.o` extraídos entram no binário | D-3.6 |
</specifics>

<deferred>
## Adiado para a Fase 4

- As 35 guardas `0x4F000000u` e as 9 famílias de marcador em falta (~30 patches).
- Fazer o `apply_all_patches.sh` distinguir "já aplicado" de "não casou nada" e falhar
  no segundo caso — a correcção de raiz do defeito sistémico.
- Acrescentar ao `run_checks` contagens por função-alvo.

## Bloqueado por autorização do utilizador

- `git subtree pull` para reconciliar `games/gow2/` com o irmão (exige `push`).
- Promover o gate de sincronização do `build_macos.sh` de WARN a FAIL (depende do acima).
</deferred>
