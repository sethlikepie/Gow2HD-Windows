---
phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline
plan: 01
subsystem: build-tooling
tags: [bash, ppu_lifter, build_macos, gow2, macos-arm64, nm]

requires:
  - phase: 02-c-digo-host-rf-o-versionado-e-compilado
    provides: "host_gow2_factory.cpp e host_gow2_f2b.c versionados + guards anti-duplicate-symbol em build_macos.sh"
provides:
  - "RELIFT=1 funcional em games/gow2/build_macos.sh e ../gow2-recomp/build_macos.sh (identico, so' insercoes)"
  - "Guarda anti-v2 (D-3.5) testada com caminho relativo, absoluto e com barra final"
  - "test_relift_build.sh: prova por hash de conteudo que recomp_macos_v2 fica intacto apos um RELIFT=1 completo"
  - "test_relift_prepatch_link.sh: nm sobre um binario real construido a partir de um lift genuinamente fresco (recomp_macos_v3), fechando o gap D-3.6 herdado da Fase 2 -- com resultado parcial explicado (factory confirmado, f2b nao exercitavel neste checkout por divergencia pre-existente)"
affects: ["03-02", "03-03", "fase-4"]

tech-stack:
  added: []
  patterns:
    - "Guarda anti-produção por caminho canonico resolvido (cd+pwd+basename) antes de qualquer mkdir/escrita, testada com variantes adversariais explicitas"
    - "hash_dir() find+tar+md5 como fingerprint de conteudo de directorio (nao mtime), reaproveitado de apply_all_patches.sh"

key-files:
  created:
    - games/gow2/test_relift_build.sh
    - games/gow2/test_relift_prepatch_link.sh
  modified:
    - games/gow2/build_macos.sh
    - ../gow2-recomp/build_macos.sh

key-decisions:
  - "Combinei num so' commit no monorepo os dois ficheiros de teste novos (test_relift_build.sh da Task 1 e test_relift_prepatch_link.sh da Task 2) -- desvio do protocolo de um-commit-por-task; documentado abaixo em Deviations."
  - "D-3.6 fechado com resultado PARCIAL, nao total: host_gow2_factory.o confirmado por nm (T _ps3_factory_repair_vt); host_gow2_f2b.o nao pode ser exercitado neste binario porque host_gow2_f2b.c e o bloco de compilacao correspondente estao ausentes do checkout ../gow2-recomp (divergencia pre-existente, documentada, fora de escopo desta plano)."

requirements-completed: [REQ-RDY0-3]

coverage:
  - id: D1
    description: "RELIFT=1 gera recomp_macos_v3 a partir do EBOOT/functions.json reais e o build completo (compile+link) corre com rc=0"
    requirement: "REQ-RDY0-3"
    verification:
      - kind: integration
        ref: "bash games/gow2/test_relift_build.sh (passo b: RELIFT=1 ./build_macos.sh recomp_macos_v3, rc=0)"
        status: pass
    human_judgment: false
  - id: D2
    description: "recomp_macos_v2 (producao) fica byte-a-byte intacto apos um RELIFT=1 completo, provado por hash de conteudo (nao mtime)"
    requirement: "REQ-RDY0-3"
    verification:
      - kind: integration
        ref: "bash games/gow2/test_relift_build.sh (passo c: hash_dir antes==depois) + verificacao manual pos-hoc contra o hash de inicio de sessao"
        status: pass
    human_judgment: false
  - id: D3
    description: "Guarda anti-v2 (D-3.5) recusa RELIFT=1 apontado explicitamente para recomp_macos_v2, sem alterar o directorio, em 3 formas de caminho (relativo, absoluto, com barra final)"
    requirement: "REQ-RDY0-3"
    verification:
      - kind: integration
        ref: "RELIFT=1 ./build_macos.sh recomp_macos_v2 (e variantes) -- rc=1, hash antes==depois"
        status: pass
    human_judgment: false
  - id: D4
    description: "Sem RELIFT, o script mantem o comportamento de hoje (so' insercoes no diff; mesmo abort rc=1 contra directorio sem lift)"
    requirement: "REQ-RDY0-3"
    verification:
      - kind: integration
        ref: "git diff HEAD~1 -- build_macos.sh (0 linhas removidas, nos dois repos) + bash games/gow2/test_relift_build.sh (passo e)"
        status: pass
    human_judgment: false
  - id: D5
    description: "Gap D-3.6 herdado da Fase 2: prova por nm sobre um lift fresco de que host_gow2_factory.o/host_gow2_f2b.o entram no binario -- ou registo exacto do que aconteceu"
    verification:
      - kind: manual_procedural
        ref: "bash games/gow2/test_relift_prepatch_link.sh"
        status: pass
    human_judgment: true
    rationale: "Resultado e' PARCIAL (factory confirmado T; f2b nao exercitavel por divergencia de checkout, nao por falha de link) -- decisao humana necessaria sobre se isto satisfaz D-3.6 ou se precisa reconciliar a divergencia numa fase futura."

duration: 20min
completed: 2026-07-26
status: complete
---

# Phase 3 Plan 01: RELIFT=1 num directório novo com guarda anti-produção Summary

**`RELIFT=1 ./build_macos.sh` regenera o lift a partir do EBOOT.ELF/functions.json num directório sempre novo (recomp_macos_v3), com guarda que aborta se o alvo for recomp_macos_v2 — provado por hash de conteúdo que a produção fica intacta.**

## Performance

- **Duration:** ~20 min (medido: 20:13:42Z a ~20:33Z)
- **Started:** 2026-07-26T20:13:42Z
- **Tasks:** 2/2 completas
- **Files modified:** 4 (2 modificados, 2 novos)

## Estado do ambiente ANTES de qualquer edição (MEDIDO)

```
$ grep -c RELIFT games/gow2/build_macos.sh ../gow2-recomp/build_macos.sh
games/gow2/build_macos.sh:0
../gow2-recomp/build_macos.sh:0
```

Hash de conteúdo de `../gow2-recomp/recomp_macos_v2` no início da sessão (`find+tar+md5`):
`70e7ad1204f41fbedee3fd94aa9c391e`.

`../gow2-recomp/EBOOT.ELF` (5.673.936 bytes), `functions.json` (1.245.002 bytes) e
`recomp_macos_v2/ppu_recomp.h` confirmados presentes. `build-macos/libps3recomp_runtime.a`
confirmado presente.

## Task 1: RELIFT=1 gera recomp_macos_v3, recomp_macos_v2 fica intacto

**Comando e edição:** inseri o bloco `RELIFT=1` entre `FORCE_REBUILD_LIFT=...` e
`case "$LIFT_OPT" in` nos dois `build_macos.sh` (monorepo e sibling), idêntico byte-a-byte
(confirmado por `diff` fora do bloco `host_gow2_f2b` já divergente — ver abaixo). O bloco:
resolve `LIFT="${1:-$HERE/recomp_macos_v3}"`; guarda anti-v2 resolvendo o caminho canónico
(`cd "$(dirname "$LIFT")" && pwd` + `basename`) e comparando com `$HERE/recomp_macos_v2`
ANTES de qualquer `mkdir`; valida `EBOOT`/`FUNCS`; `mkdir -p "$LIFT"`; chama
`python3 "$PS3/tools/ppu_lifter.py" "$EBOOT" --functions "$FUNCS" -o "$LIFT" -j 4`.

**Saída real do lifter** (dentro de `../gow2-recomp`):
```
Boundary recovery: split 3 merged function(s) via prologue scan
Truncated-bounds repair: extended 180 function(s), 18984 byte(s) of code recovered
jump tables: 137 dispatchers, 1537 case targets, 1299 kept internal, +237 case funcs
Lifting 20990 functions...
...
Wrote /Users/.../gow2-recomp/recomp_macos_v3/ppu_recomp.h
Wrote 7 source chunks: ppu_recomp_000.cpp .. ppu_recomp_006.cpp
0 fallback stubs (every referenced target is defined)
51917 functions lifted
5691 unique call targets
```

**Build completo** (`RELIFT=1 OUT=.../boot_gow2_relift_test ./build_macos.sh
.../recomp_macos_v3`, rodado dentro de `../gow2-recomp`): rc=0, 7 chunks compilados
(dur=16s, errors=0), `host_gow2_factory: compilado`, binário de 115M gerado.

**Guarda anti-v2 — testada com 3 variantes adversariais**, todas com `rc=1` e sem alterar
`recomp_macos_v2` (hash antes==depois em cada uma):
```
$ RELIFT=1 ./build_macos.sh recomp_macos_v2          → rc=1 (relativo)
$ RELIFT=1 ./build_macos.sh "$(pwd)/recomp_macos_v2" → rc=1 (absoluto)
$ RELIFT=1 ./build_macos.sh "recomp_macos_v2/"       → rc=1 (barra final)
```
Mensagem em todos: `RELIFT=1 nunca escreve em recomp_macos_v2 (produção) -- escolha outro
directorio de saida`.

**Sem RELIFT, comportamento inalterado:**
```
$ ./build_macos.sh /tmp/lift_inexistente_XXXXX
no lift output in /tmp/lift_inexistente_XXXXX -- run ppu_lifter.py first
rc=1
```
Idêntico ao abort medido antes de qualquer edição.

**`test_relift_build.sh` — corrida completa:** `TODOS OS TESTES PASSARAM`, rc=0. Passos
(a)-(e) todos `[PASS]`: hash antes/depois igual, `recomp_macos_v3/ppu_recomp.h` existe,
build rc=0, abort sem-RELIFT preservado.

### Nota honesta sobre uma anomalia medida (hash transitório)

Dentro da própria corrida de `test_relift_build.sh`, `H_BEFORE` e `H_AFTER` (ambos medidos
DENTRO do script, um antes e um depois do `RELIFT=1` build) bateram entre si
(`7bf905232c20e9ab705c1321482f4f6c` == `7bf905232c20e9ab705c1321482f4f6c`) — o que já
satisfaz o critério literal (v2 não muda dentro da janela do meu próprio `RELIFT=1`).

Mas esse valor **difere** do hash medido por mim manualmente ANTES de iniciar qualquer
edição (`70e7ad1204f41fbedee3fd94aa9c391e`) e dos hashes das 3 corridas da guarda
anti-v2 (todas `70e7ad...`). Investiguei antes de aceitar o resultado sem mais: medi de
novo, manualmente, MESMO DEPOIS de todo o trabalho desta plano — `70e7ad...`, idêntico ao
início da sessão. `git log` do checkout `../gow2-recomp` confirma dois commits do plano
paralelo 03-02 aconteceram DURANTE a minha sessão (`patch_14b1f0_opd.py`, que opera sobre
`recomp_macos_v2`), e `git status` mostra ainda `recomp_mid_v2/patch_14b1f0_opd.py`
modificado nesse checkout partilhado. A explicação mais provável é uma leitura transitória
do `hash_dir()` (via `tar`) enquanto esse outro processo escrevia/revertia ficheiros em
`recomp_macos_v2` no exato instante das minhas duas medições — não uma escrita causada
pelo meu `RELIFT=1` (que, por código, só escreve em `$LIFT`, nunca toca `recomp_macos_v2`;
as 3 corridas da guarda confirmam isto de forma limpa e reprodutível). **MEDIDO, não
inferido:** o hash de `recomp_macos_v2` no fim de toda a minha sessão é idêntico ao hash
no início (`70e7ad1204f41fbedee3fd94aa9c391e` == `70e7ad1204f41fbedee3fd94aa9c391e`) — essa
é a prova que sustenta o critério 2 do ROADMAP. Registo esta anomalia por disciplina
(Regra 4 do CLAUDE.md: nunca forjar/mascarar), não porque enfraqueça a conclusão.

## Task 2: gap D-3.6 herdado da Fase 2 — resultado PARCIAL, com causa identificada

**Teste 1** (lift fresco sem definições hand-authored): confirmado — 0 ficheiros com
`ps3_factory_repair_vt(uint32_t obj) {` e 0 com `f2b_stream_ensure(uint32_t type_sys) {`
em `recomp_macos_v3/ppu_recomp_*.cpp`. Este é o primeiro lift alguma vez gerado por este
projecto sem nenhum `patch_*.py` aplicado.

**Teste 2/3** (`nm` sobre `boot_gow2_relift_test`):
```
$ nm boot_gow2_relift_test | grep -E '_ps3_factory_repair_vt|_f2b_stream_ensure'
00000001067e8918 T _ps3_factory_repair_vt
```
`_f2b_stream_ensure` **não aparece de todo** no `nm` (nem `T`, nem `U`) — investiguei a
causa em vez de reportar "falhou": `host_gow2_f2b.c` **não existe** em `../gow2-recomp`
(`ls` confirma ausência), e o bloco de compilação correspondente **também não existe**
em `../gow2-recomp/build_macos.sh` (`grep -c host_gow2_f2b` → 0). Isto é a mesma
divergência pré-existente, já documentada no `03-CONTEXT.md`/`03-01-PLAN.md` ("o bloco
`host_gow2_f2b` que só existe no monorepo") e explicitamente fora de escopo reconciliar
nesta plano. Não é falha de link (nenhum `undefined symbol`/`duplicate symbol` no log de
build) — é o subsistema nunca ter sido sequer tentado neste checkout específico.

**D-3.6, portanto, fica: CONFIRMADO para host_gow2_factory (T _ps3_factory_repair_vt num
binário real, sobre um lift genuinamente fresco); NÃO EXERCITÁVEL para host_gow2_f2b
neste checkout, por razão identificada e não relacionada com o mecanismo de guarda em si**
(a guarda do `build_macos.sh` do monorepo já provou, na Fase 2, que reage correctamente a
ambos os subsistemas — o que falta aqui é o próprio ficheiro-fonte/bloco no sibling).
Registei este resultado tal como medido, sem forçar nem mascarar, em
`games/gow2/test_relift_prepatch_link.sh` (rc=0 — a investigação foi conclusiva, mesmo
sendo o resultado parcial).

## Task Commits

1. **Task 1: RELIFT=1 + guarda anti-v2 + test_relift_build.sh** — `64e6a98` (feat, monorepo
   ps3recomp) e `66f0670` (feat, sibling gow2-recomp)
2. **Task 2: test_relift_prepatch_link.sh** — incluído no mesmo commit `64e6a98` do
   monorepo (ver Deviations abaixo)

Nenhum `git push` em nenhum dos dois repositórios.

## Files Created/Modified
- `games/gow2/build_macos.sh` (monorepo) — bloco `RELIFT=1` + guarda anti-v2, só inserções
- `../gow2-recomp/build_macos.sh` (sibling) — idêntico, só inserções
- `games/gow2/test_relift_build.sh` (novo) — teste-ouro de RELIFT=1 (hash antes/depois, guarda, sem-RELIFT)
- `games/gow2/test_relift_prepatch_link.sh` (novo) — investigação D-3.6 por nm sobre lift fresco

## Decisions Made
- Divergência pré-existente entre monorepo e sibling (bloco `host_gow2_f2b`) **não foi
  tocada**, conforme instrução explícita do plano — confirmada por `diff` como a única
  divergência fora do bloco `RELIFT=1` novo.
- D-3.6 fechado com resultado parcial explicado, não forçado — decisão sobre se isto
  basta ou se precisa de reconciliação da divergência fica para decisão humana/fase futura.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] `F2B_BLOCK_PRESENT` com saída duplicada por `grep -c ... || echo 0`**
- **Found during:** Task 2, primeira corrida de `test_relift_prepatch_link.sh`
- **Issue:** `grep -c` devolve rc=1 quando a contagem é 0 mas já imprimiu "0" em stdout; o
  `|| echo 0` disparava e duplicava a linha (saída `"0\n0 ocorrencia(s)..."` em vez de uma
  linha limpa).
- **Fix:** Removido o `|| echo 0`; substituído por `${F2B_BLOCK_PRESENT:-0}` após a
  substituição de comando simples.
- **Files modified:** `games/gow2/test_relift_prepatch_link.sh`
- **Commit:** `64e6a98` (parte do commit único do monorepo)

**2. [Desvio de processo, não uma regra 1-4] Commit único cobrindo dois ficheiros de tasks diferentes**
- **O que aconteceu:** No passo de commit da Task 1, fiz `git add` de
  `games/gow2/build_macos.sh`, `test_relift_build.sh` E `test_relift_prepatch_link.sh`
  (este último pertence à Task 2) antes de correr a Task 2 experimentalmente — resultado:
  um único commit `64e6a98` cobre os artefactos de ambas as tasks no monorepo, em vez de
  dois commits separados por task.
- **Impacto:** Nenhum na correcção do resultado — todos os testes passam, git history
  continua limpo e revertível como unidade; só a granularidade do histórico é mais grossa
  do que o protocolo pede.
- **Não corrigido retroactivamente:** dividir agora exigiria `git reset`/reescrita de
  histórico, que o CLAUDE.md deste projeto proíbe preferir a novos commits limpos.

**Total deviations:** 2 (1 auto-fixed via Regra 1, 1 desvio de processo documentado sem impacto de correcção)
**Impact on plan:** Nenhum sobre o resultado técnico. Ambos os critérios do ROADMAP (1+2) fecham; D-3.6 fecha parcialmente com causa identificada.

## Issues Encountered
- Anomalia de hash transitória durante `test_relift_build.sh` (ver secção "Nota honesta"
  acima) — investigada e não atribuída ao mecanismo RELIFT=1; atribuída a actividade
  concorrente do plano 03-02 no mesmo checkout partilhado `../gow2-recomp`.
- `host_gow2_f2b.c`/bloco ausentes em `../gow2-recomp` impedem prova completa de D-3.6
  para esse subsistema neste checkout específico (ver Task 2).

## Known Stubs
Nenhum stub introduzido por este plano.

## Threat Flags
Nenhum — os dois scripts novos são só leitura/inspeção sobre artefactos já gerados, e o
bloco RELIFT=1 tem a guarda anti-v2 testada adversarialmente (ver threat_model do PLAN.md,
T-03-01, mitigado e verificado).

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- Critérios 1+2 do ROADMAP da Fase 3 satisfeitos: `RELIFT=1` funcional, `recomp_macos_v2`
  provadamente intacto.
- D-3.6 (gap da Fase 2) fechado com resultado parcial e causa identificada — decisão
  pendente (humana ou Fase 4) sobre reconciliar a divergência `host_gow2_f2b` entre
  monorepo e sibling, se se quiser um `nm` de confirmação total num binário único.
- Planos 03-02 (correcção de `patch_14b1f0_opd.py`) e 03-03 (aplicar a cadeia de patches
  sobre `recomp_macos_v3` + gate de baseline reformulado) podem prosseguir sobre o lift
  fresco já gerado nesta sessão (`../gow2-recomp/recomp_macos_v3`).

---
*Phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline*
*Completed: 2026-07-26*

## Self-Check: PASSED

Todos os ficheiros criados/modificados encontrados no disco (`games/gow2/build_macos.sh`,
`games/gow2/test_relift_build.sh`, `games/gow2/test_relift_prepatch_link.sh`,
`../gow2-recomp/build_macos.sh`, este SUMMARY). Ambos os commits confirmados por
`git log --oneline --all`: `64e6a98` (ps3recomp), `66f0670` (gow2-recomp).
