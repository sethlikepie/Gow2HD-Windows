---
phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline
plan: 02
subsystem: patch-scripts-opd
tags: [python, regex, patch_mid_v2, gow2, opd, tdd]

requires:
  - phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline
    provides: "diagnostico D-3.7 (03-CONTEXT.md): 12 conversoes OPD em falta em func_0014B1F0, causa = agulhas so' casam o formato antigo do restauro do TOC"
provides:
  - "patch_14b1f0_opd.py corrigido: aceita os dois formatos de restauro do TOC (vm_read64 antigo e TOCFIX novo), reemitindo o casado VERBATIM"
  - "convencao de rc CONVERTIDO(0)/JA-APLICADO(0)/SEM-EFEITO(3) -- fecha o exemplo concreto do achado sistemico do 03-CONTEXT.md para este script"
  - "test_patch_14b1f0_opd.py: suite dedicada com fixtures extraidas do lift real, RED congelado + 5 cenarios GREEN"
affects: ["03-03", "fase-4"]

tech-stack:
  added: []
  patterns:
    - "TOC_RESTORE com grupo nomeado (?P<toc>...) aceitando dois shapes e reemitido verbatim (copiado de patch_32e200_opd.py/patch_icg_ctor_opd.py, corrigidos na mesma sessao)"
    - "Teste RED contra snapshot CONGELADO do script pre-fix (git show HEAD embutido como string), nao contra o ficheiro mutavel -- sobrevive a correccao do proprio script sob teste"
    - "veredicto de 3 ramos com EXPECTED_SITES/RC_NO_EFFECT como constantes nomeadas"

key-files:
  created:
    - ../gow2-recomp/recomp_mid_v2/test_patch_14b1f0_opd.py
  modified:
    - ../gow2-recomp/recomp_mid_v2/patch_14b1f0_opd.py
    - games/gow2/recomp_mid_v2/patch_14b1f0_opd.py

key-decisions:
  - "Commit primeiro e principalmente em ../gow2-recomp/recomp_mid_v2/ (script que apply_all_patches.sh de facto corre); commit espelho no monorepo so' para preservar a igualdade byte-a-byte que ja existia, sem reconciliar os outros 47 ficheiros ja divergentes (fora de escopo, bloqueado por autorizacao)."
  - "Nao troquei p.write_text(..., newline='\\n') por open(..., newline='\\n') apesar do gotcha citado no prompt de execucao: apply_all_patches.sh ja resolve isto escolhendo um interprete >=3.10 (pick_python(), comentario explicito no proprio script), e 2 dos 3 modelos citados (patch_icg_ctor_opd.py, patch_39e5d8_opd.py) mantem write_text(newline=...) contando com essa guarda -- so' patch_32e200_opd.py usa open(). Mantive o padrao maioritario/ja sancionado pelo runner, fora do mandato especifico de D-3.7."

requirements-completed: [REQ-RDY0-4]

coverage:
  - id: D1
    description: "patch_14b1f0_opd.py converte os 12 sitios OPD remanescentes de func_0014B1F0, aceitando os dois formatos de restauro do TOC (antigo vm_read64 e TOCFIX actual), reemitindo o casado verbatim"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: unit
        ref: "test_patch_14b1f0_opd.py::test_green1_convertido_regiao_nova (TOCFIX) + test_green2_compat_regiao_antiga (vm_read64)"
        status: pass
      - kind: integration
        ref: "prova e2e contra copia scratch de recomp_macos_v2/ppu_recomp_000.cpp: delta +12 (10->22), agregado do projecto 35->47"
        status: pass
    human_judgment: false
  - id: D2
    description: "O script distingue CONVERTIDO/JA-APLICADO/SEM-EFEITO por rc -- rc=3 no caso sem efeito, nunca rc=0 falso-verde"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: unit
        ref: "test_patch_14b1f0_opd.py::test_red_bug_medido_no_script_congelado (rc=0 no script antigo, o bug) + test_green4_sem_efeito_rc3 (rc=3 no script corrigido)"
        status: pass
    human_judgment: false
  - id: D3
    description: "Aplicado contra copia scratch do lift real, ps3_call_opd(ctx, sobe de 35 para 47 no agregado do projecto"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: integration
        ref: "grep -c manual antes/depois: chunk 000 10->22 (delta 12); agregado 000+001+003: 35->47"
        status: pass
    human_judgment: false
  - id: D4
    description: "Corrido uma segunda vez sobre o lift ja convertido: rc=0, 0 conversoes novas, 12 ja-aplicados (idempotencia) -- e uma terceira corrida produz ficheiro byte-identico a' segunda"
    requirement: "REQ-RDY0-4"
    verification:
      - kind: unit
        ref: "test_patch_14b1f0_opd.py::test_green3_idempotencia_ate_terceira_corrida (hash/conteudo 2a==3a corrida)"
        status: pass
    human_judgment: false

duration: ~5min entre commits (mais tempo de leitura/medicao antes)
completed: 2026-07-26
status: complete
---

# Phase 3 Plan 02: Corrigir patch_14b1f0_opd.py (D-3.7) Summary

**`patch_14b1f0_opd.py` passou a aceitar os dois formatos de restauro do TOC (vm_read64 antigo e TOCFIX actual) e a distinguir CONVERTIDO/JA-APLICADO/SEM-EFEITO por rc — fechando as 12 conversões OPD que faltavam para `ps3_call_opd` chegar a 47 chamadas no projecto (35→47).**

## Estado

D-3.7 fechado. As 12 conversões OPD que faltavam em `func_0014B1F0` acontecem de verdade,
o script deixou de devolver `rc=0` quando não converte nada, e os dois `recomp_mid_v2/patch_14b1f0_opd.py`
(gow2-recomp e monorepo) continuam byte-a-byte idênticos entre si.

## Vermelho → Verde (contagens medidas)

**Vermelho** (script tal como estava antes desta correcção, contra a fixture real extraída
de `func_0014B1F0`, 12 sítios, os 12 já no formato TOCFIX):

```
replaced pat1=0 pat2=0 total_opd_sites=0
remaining indirect in region: 12
OK patch_14b1f0_opd
rc=0
```

0 conversões, `ps3_call_opd(ctx,` = 0 no corpo da função, e o script sai `rc=0` na mesma —
o falso-verde exacto medido em `03-CONTEXT.md` para este script.

**Verde** (mesmo script, corrigido, mesma fixture):

```
replaced pat1=11 pat2=1 total_opd_sites=12
remaining indirect in region: 0
OK patch_14b1f0_opd (CONVERTIDO)
rc=0
```

12 conversões (`pat1=11 pat2=1`, soma 12), `remaining indirect in region: 0`,
`ps3_call_opd(ctx,` passa a aparecer 12× no corpo de `func_0014B1F0`.

## Prova de idempotência (por hash)

Segunda corrida sobre o mesmo ficheiro já convertido:
```
replaced pat1=0 pat2=0 total_opd_sites=0
remaining indirect in region: 0
OK patch_14b1f0_opd (JA-APLICADO: 12 sitios ja em ps3_call_opd)
rc=0
```
0 conversões novas, 12 já-aplicados reportados, `rc=0` — mas agora pela via correcta
(`JA-APLICADO`, não o `SEM-EFEITO` disfarçado de antes). Terceira corrida sobre o
resultado da segunda: hash sha256 do ficheiro idêntico entre a 2ª e a 3ª corrida
(`35577ec1ed120c50cca94b5477ca693a6d5ba60e00510095bed346ab86c59a47` == mesmo valor) —
idempotência confirmada por conteúdo, não por inspecção.

## Comportamento contra o lift antigo (formato `vm_read64`)

Fixture `REGION_OLD` (derivada mecanicamente de `REGION_NEW` trocando as 12 ocorrências
TOCFIX pela forma antiga `vm_read64(ctx->gpr[1] + 0x28);`): o script corrigido converte
igualmente os 12 sítios, reemitindo o restauro do TOC **verbatim** na forma antiga —
`vm_read64(ctx->gpr[1] + 0x28);` aparece 12× no resultado, zero `TOCFIX` inventado. Prova
de que a correcção é um "aceita os dois", não um "troca um pelo outro" — o script continua
byte-compatível com `recomp_macos_v2.pre_v4`.

## rc no caso sem alvos (SEM-EFEITO)

Fixture onde `func_0014B1F0` existe mas nenhum dos dois formatos de TOC nem `ps3_call_opd`
pré-existente aparece (substituí as 12 ocorrências de `ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);`
por uma chamada hipotética de um terceiro formato):
```
replaced pat1=0 pat2=0 total_opd_sites=0
remaining indirect in region: 0
added BATCH entry probe
FALHA patch_14b1f0_opd (SEM-EFEITO): 0 conversoes e 0 sitios ja convertidos.
  As agulhas nao casam o shape actual do lift.
rc=3
```
`rc=3` (`RC_NO_EFFECT`), nunca `rc=0` — é este ramo que não existia antes e causava o
falso-verde.

## Prova e2e contra cópia scratch do lift real

Cópia scratch de `../gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp` (nunca o original):
`ps3_call_opd(ctx,` sobe de **10 para 22** nesse chunk (delta +12, exactamente os 12
esperados). Agregado do projecto (`ppu_recomp_000.cpp` + `001.cpp` + `003.cpp`): **35 → 47**
— o alvo histórico citado em `03-CONTEXT.md`. O ficheiro **real** de produção
(`../gow2-recomp/recomp_macos_v2/ppu_recomp_000.cpp`) permanece byte-a-byte intacto: sha256
antes e depois de toda a task, idêntico
(`f418d59dd6f9dc384348672762e95110588e7ae1635c1516b5be47ad5bef0b36` nos dois momentos).

## Onde e porquê commitei

Dois commits separados, em dois repositórios:

1. **`../gow2-recomp` (o script que `apply_all_patches.sh` de facto corre):**
   - `e42d11c` (Task 1, RED): `test_patch_14b1f0_opd.py` novo — fixture extraída literalmente
     do lift real, teste RED reproduzido contra um **snapshot congelado** do script
     pré-correcção (capturado por `git show HEAD:...` antes de qualquer edição, embutido
     como string no teste), não contra o ficheiro mutável — isto mantém o Teste 0 válido
     para sempre como prova do bug histórico, mesmo depois do script real ser corrigido
     na mesma sessão.
   - `e616c6f` (Task 2, GREEN): `patch_14b1f0_opd.py` corrigido + `test_patch_14b1f0_opd.py`
     ampliado com os 5 cenários GREEN (convertido/compat-antigo/idempotência/sem-efeito/e2e).
2. **`ps3recomp` (monorepo, mirror):** `add4936` — mesma edição byte-a-byte em
   `games/gow2/recomp_mid_v2/patch_14b1f0_opd.py`. Confirmado por `diff` que os dois
   ficheiros ficam idênticos depois da edição (eram idênticos antes da fase; continuam a
   ser). Este mirror não tem efeito de execução (o `recomp_macos_v2` real vive só no
   sibling); serve só para não introduzir uma divergência nova onde não existia, conforme
   instrução explícita do plano — os outros 47 ficheiros já divergentes entre os dois
   `recomp_mid_v2/` **não foram tocados**.

Nenhum `git push` em nenhum dos dois repositórios.

## Task Commits

1. **Task 1 (RED):** `e42d11c` (gow2-recomp) — `test_patch_14b1f0_opd.py` novo, fixture +
   teste RED congelado.
2. **Task 2 (GREEN):** `e616c6f` (gow2-recomp) — script corrigido + 5 testes GREEN;
   `add4936` (ps3recomp, monorepo) — mirror.

## Files Created/Modified

- `../gow2-recomp/recomp_mid_v2/test_patch_14b1f0_opd.py` (novo, 285 linhas) — fixtures
  `REGION_NEW`/`REGION_OLD` extraídas/derivadas do lift real, `OLD_SCRIPT_SRC` congelado,
  1 teste RED + 3 testes de extracção/formato + 5 testes GREEN.
- `../gow2-recomp/recomp_mid_v2/patch_14b1f0_opd.py` — `TOC_RESTORE` com grupo nomeado
  `toc` aceitando os dois shapes; `repl()` reemite `m.group("toc")` em vez de hardcodar a
  forma antiga; `ensure_decl()` copiado de `patch_icg_ctor_opd.py` (janela 80000);
  `EXPECTED_SITES=12`/`RC_NO_EFFECT=3`; veredicto de 3 ramos.
- `games/gow2/recomp_mid_v2/patch_14b1f0_opd.py` — mirror idêntico (confirmado por `diff`).

## Decisions Made

- Editar e commitar primeiro em `../gow2-recomp` (efeito real), mirror no monorepo depois —
  conforme a "Decisão operacional" já declarada no `<objective>` do plano.
- Manter `p.write_text(..., newline="\n")` em vez de trocar para `open(..., newline="\n")`:
  ver `key-decisions` acima — o `apply_all_patches.sh` já resolve a incompatibilidade
  py3.9/3.10 escolhendo um interpretador `>=3.10` (`pick_python()`), e 2 dos 3 modelos
  citados no prompt de execução (`patch_icg_ctor_opd.py`, `patch_39e5d8_opd.py`) já usam
  esse mesmo padrão `write_text(newline=...)` hoje, corrigidos na mesma sessão de hoje.
  Trocar para `open()` seria inconsistente com a maioria dos modelos e fora do mandato
  específico de D-3.7 (TOC + rc), por isso não troquei.

## Deviations from Plan

Nenhuma deviação de Regra 1-4 (nenhum bug extra encontrado, nenhuma funcionalidade crítica
em falta além do que o plano já mandava corrigir, nenhuma mudança arquitectural).

Uma nota de processo, não uma correcção: o "Teste 0 (RED)" do plano pedia para correr "o
script ACTUAL (ainda não corrigido)" — segui a sugestão do próprio action step 4 ("se a
Task 2 já correu nesta sessão, corra este teste contra uma cópia do script tal como estava
ANTES") e desenhei o Teste 0 desde o início contra um **snapshot congelado** (embutido como
string, capturado por `git show HEAD:...`) em vez do `PATCH_PATH` mutável. Isto foi uma
decisão de desenho deliberada, não um desvio: sem isto, reexecutar a suite de testes depois
da Task 2 corrigir o script faria o Teste 0 falhar (porque o script já converteria os 12
sítios) — o que seria uma falsa regressão do próprio fix, não do teste. O snapshot congelado
mantém o Teste 0 válido para sempre como evidência do bug histórico.

Também confirmado, e explicitamente fora de escopo: durante a execução, um outro checkout
de `../gow2-recomp` (o plano paralelo 03-01, Wave 1) fez commits concorrentes em
`build_macos.sh` no mesmo repositório partilhado — não toquei nesse ficheiro em momento
algum; `git log` confirma os dois históricos intercalados sem conflito
(`66f0670`/`64e6a98` do 03-01 entre `e42d11c` e `e616c6f` deste plano).

**Total deviations:** 0 (Regras 1-4); 1 nota de desenho de teste documentada acima.
**Impact on plan:** Nenhum — todos os critérios de aceitação das duas tasks fecham como
escrito no plano.

## Issues Encountered

Nenhum bloqueio. A única superfície de atenção foi confirmar, antes de escrever a fixture
sintética das GREEN, que o script corrigido chama `ensure_decl(s)` logo no arranque — as
fixtures usadas nos testes GREEN têm de incluir as duas declarações `extern "C"` no
preâmbulo (`ps3_indirect_call`/`ps3_call_opd`) para não abortar com `"no ps3_indirect_call
decl in 000"`; resolvido com um `_PREAMBLE` sintético de 2 linhas nas fixtures de teste
(o lift real já as tem, a offset 37408 — bem dentro da janela de 80000 caracteres usada
por `ensure_decl`).

## Known Stubs

Nenhum stub introduzido por este plano.

## Threat Flags

Nenhum novo — o `threat_model` do `03-02-PLAN.md` já cobria os três riscos relevantes
(T-03-05 delimitação de região, T-03-06 rc falso-verde, T-03-07 escrita acidental no lift
real) e todos têm mitigação testada: delimitação por `s.find(...)` explícita sem regex
(inalterada), veredicto de 3 ramos com `rc=3` testado por fixture dedicada (GREEN 4), e
todas as provas de conversão operam sobre `tempfile.TemporaryDirectory()` ou cópias scratch
explícitas — nunca o `ppu_recomp_000.cpp` real (confirmado por hash antes/depois).

## User Setup Required

None - nenhuma configuração de serviço externo necessária.

## Next Phase Readiness

- D-3.7 fechado: as 12 conversões OPD em falta em `func_0014B1F0` acontecem de verdade;
  `ps3_call_opd(ctx,` sobe de 35 para 47 no agregado do projecto quando o script corre
  contra o lift real (medido contra cópia scratch nesta sessão; a aplicação real ao lift
  novo `recomp_macos_v3` fica para o Plano 03-03, conforme escopo do plano).
- O achado sistémico do `03-CONTEXT.md` ("um patch que corre e não faz nada devolve rc=0")
  está corrigido neste exemplo concreto, com prova por fixture dedicada. Os outros ~29
  patches com o mesmo defeito (35 guardas `0x4F000000u`, 9 famílias de marcador) ficam para
  a Fase 4, como já decidido em D-3.7/03-CONTEXT.md.
- Plano 03-03 (aplicar a cadeia de patches sobre `recomp_macos_v3` + gate de baseline
  reformulado) pode contar com este script corrigido já disponível em
  `../gow2-recomp/recomp_mid_v2/patch_14b1f0_opd.py`.

---
*Phase: 03-re-lift-num-diret-rio-novo-com-gate-de-baseline*
*Completed: 2026-07-26*

## Self-Check: PASSED

Ficheiros confirmados no disco: `../gow2-recomp/recomp_mid_v2/test_patch_14b1f0_opd.py`,
`../gow2-recomp/recomp_mid_v2/patch_14b1f0_opd.py`, `games/gow2/recomp_mid_v2/patch_14b1f0_opd.py`,
este SUMMARY. Commits confirmados por `git log --oneline --all`: `e42d11c`/`e616c6f`
(gow2-recomp), `add4936` (ps3recomp). Suite de testes final: `.venv/bin/python3
../gow2-recomp/recomp_mid_v2/test_patch_14b1f0_opd.py` → `[test_patch_14b1f0_opd] OK`, rc=0.
