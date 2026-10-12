---
phase: 01-emiss-es-do-lifter-e-teste-ouro-do-pre-mbulo
verified: 2026-07-26T07:33:52Z
status: passed
score: 4/4 must-haves verified
behavior_unverified: 0
overrides_applied: 0
---

# Phase 1: Emissões do lifter e teste-ouro do preâmbulo — Verification Report

**Phase Goal:** Um lift gerado hoje pelo `tools/ppu_lifter.py` contém tudo que os scripts
de build consomem, e uma regressão nessas emissões é detectada por teste em vez de por
link quebrado horas depois.

**Verified:** 2026-07-26T07:33:52Z
**Status:** passed
**Re-verification:** No — initial verification

**Metodologia:** goal-backward. Nenhum comando foi confiado ao texto do SUMMARY — todos
os comandos abaixo foram re-executados nesta sessão, com a saída real colada. Onde a
metodologia pediu prova adicional (compilar um chunk real, medir `nm -u`, forçar o gate
de sync com irmão ausente), essa prova foi obtida de forma independente, não copiada do
SUMMARY.

## Goal Achievement

### Observable Truths (4 critérios de sucesso do ROADMAP)

| # | Truth (critério ROADMAP) | Status | Evidência |
|---|---------------------------|--------|-----------|
| 1 | Um lift recém-gerado contém as 4 emissões (`stdlib.h`, ramo `__APPLE__` do TLS, `ppu_stubs.cpp` gerado, contrato `ppu_timebase_now`) e elas casam com o que `build_macos.sh:92,106,192` compila e com um definidor versionado (`sys_timer.c`) | ✓ VERIFIED | Corrida real do golden (`.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py`) mostra as 4 emissões PASS; ancora D-05 confirma `ppu_stubs.cpp` referenciado 3x em `games/gow2/build_macos.sh`; `grep -n ppu_timebase_now runtime/syscalls/sys_timer.c` mostra o definidor real (linha 99); lift ponta-a-ponta contra o EBOOT real do irmão reproduzido nesta sessão: `extended 180 function(s), 18984 byte(s)`, `0 fallback stubs`, 7 chunks — idêntico ao esperado |
| 2 | O teste-ouro está registrado em `scripts/test_python.sh` e no CTest, passa hoje, na CI do macOS, e **falha** quando cada uma das 4 emissões é removida isoladamente (mutação) | ✓ VERIFIED | `./scripts/test_python.sh` → `PASS: 7 SKIP: 2 FAIL: 0`, `test_ppu_lifter_emit.py` contado como PASS (não SKIP); `ctest --test-dir build-tests` → 21/21 (2 skips esperados por falta de SDK); 9 linhas de mutação PASS na corrida direta (4 para as emissões do critério 1 + 5 para os atômicos do critério 3); `.github/workflows/ci.yml` roda os dois scripts no job `macos` sem `continue-on-error` (bloqueante), reproduzindo localmente o mesmo caminho |
| 3 | A mina do re-lift está desarmada: os 4 `ppu_res_*` chamam `ps3_mem_oob_report` (report-only), com prova por mutação e não só por texto | ✓ VERIFIED | Harness C++ compilado e executado nesta sessão (parte da mesma corrida acima): 21 asserções PASS (controle dentro-dos-limites bit-a-bit igual, 4 casos OOB reportam sem abortar com `(len,op)` correto, fronteira exata `>` não `>=`); `git status --porcelain tools/ppu_lifter.py` confirmado vazio após a corrida — mutação nunca tocou disco |
| 4 | Nenhum token proibido (`<intrin.h>`, `Sleep(`, `__declspec`, `GetModuleHandleA`) fora de guarda `_MSC_VER` OU `_WIN32` no que o lifter emite; gate tem controle negativo | ✓ VERIFIED | `[PASS] G4: nenhum token proibido ... (0 diagnostico(s), profundidade residual = 0)` e `[PASS] controle negativo G4: token 'Sleep(' injetado ... foi detectado (1 violacao)` na corrida direta desta sessão — o gate reage exatamente 1x ao token injetado, prova de dentes reais |

**Score:** 4/4 truths verified (0 present-but-behavior-unverified)

### Verificações cépticas específicas pedidas

**1. As provas por mutação têm dentes e nunca escrevem em disco?**
CONFIRMADO. Antes de rodar qualquer teste: `git status --porcelain` estava limpo (só os
dois diretórios pré-existentes `build-tests/` e `runtime/spu/tests/out_spu_channel_blocking/`,
já untracked antes desta sessão). Depois de rodar `test_ppu_lifter_emit.py` diretamente e
via `test_python.sh`/`ctest` várias vezes nesta sessão, `git status --porcelain
tools/ppu_lifter.py runtime/ppu/tests/test_ppu_lifter_emit.py runtime/syscalls/sys_timer.c
games/gow2/build_macos.sh` voltou vazio — nenhuma mutação persistiu no disco. As 9 linhas
`[PASS] mutacao ...` na saída real (4 do critério 1, 5 do critério 3) provam que os checks
falham quando a emissão correspondente é removida em cópia de memória. `ctest`/`test_all.sh`
regeneraram fixtures SPU (`runtime/spu/tests/out_*/spu_recomp.[ch]`, 12 arquivos) — a
armadilha documentada no CLAUDE.md/SUMMARY —, restaurados com `git checkout --` antes de
qualquer outra coisa; não commitados.

**2. G4 e o gate de sync são auto-invalidáveis (passam sempre)?**
NÃO — ambos têm controle negativo obrigatório em todo run, confirmado nesta sessão:
- G4: `check_g4_negative_control()` injeta `Sleep(1000);` fora de guarda numa cópia em
  memória e exige exatamente 1 violação — reproduzido, `[PASS] controle negativo G4 ...`.
- Ancora D-05: `check_build_macos_anchor("echo sem ppu_stubs aqui\n")` tem de falhar —
  reproduzido, `[PASS] ancora D-05 (dentes): texto sem ppu_stubs.cpp falha o check`.

**3. Gate de sync com irmão AUSENTE não pode virar SKIPPED (regex desancorada `SKIP:`)?**
CONFIRMADO nesta sessão com o comando exato pedido:
```
PS3_SYNC_GATE_SIBLING=/nao/existe/build_macos.sh .venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py
```
→ `rc=0`; stdout contém `[PULADO] sub-gate de sincronizacao build_macos.sh: irmao ausente
em /nao/existe/build_macos.sh`. `grep -c "SKIP:" <log>` = **0** (desancorado) e
`grep -cE '^SKIP:' <log>` = **0** (ancorado, o padrão do `scripts/test_python.sh`). Como o
`PS3RECOMP_SKIP_REGEX` do `CMakeLists.txt:298` é literalmente `"SKIP:"`, a saída acima NÃO
o casaria — o CTest classificaria PASSED, não Skipped, e os dois classificadores
concordam. O desvio da letra de D-05 (marcador sem a substring `SKIP:`) está documentado
em bloco de comentário no próprio arquivo (linhas ~1281-1294) e é exatamente o que evita a
classificação Skipped em CI.

**4. O preâmbulo instrumentado compila num chunk real, não só na fatia isolada?**
RECONFIRMADO de forma independente nesta sessão (não copiado do SUMMARY): regenerei o
lift completo contra o EBOOT real do repositório-irmão
(`python3 tools/ppu_lifter.py EBOOT.ELF --functions functions.json -j 4`), medi
`ppu_recomp_006.cpp` com **438744 linhas** (bate com "438k linhas, 0 erros" citado), e
compilei esse arquivo com
`clang++ -std=c++20 -O0 -w -c -I <lift> -I include -I runtime/ppu ppu_recomp_006.cpp -o
ppu_recomp_006.o` → **exit 0, sem erros**, `.o` de 17MB gerado. `nm -u ppu_recomp_006.o |
grep -E "ppu_vm_size|ps3_mem_oob_report"` mostra `_ppu_vm_size` e `_ps3_mem_oob_report`
como símbolos indefinidos no objeto (esperado — são `extern`). Fui além do pedido e
verifiquei que ambos TÊM definidor real disponível para o link: `_ps3_mem_oob_report` é
`T` (definido) em `build-macos/libps3recomp_runtime.a`; `_ppu_vm_size` é `S` (definido) em
`ppu_loader.o` do checkout irmão (`../gow2-recomp/recomp_macos_v3/ppu_loader.o`) —
consistente com a nota do CLAUDE.md de que `runtime/ppu/` é excluído da `.a` e linkado à
parte. Não fiz um link completo de `boot_gow2` nesta verificação (fora do escopo/tempo
razoável de uma verificação); a compilação isolada do chunk + a presença comprovada dos
dois definidores é a evidência MEDIDA, distinta de um link E2E completo que eu não corri.

**5. `REQ-RDY0-1` `[COMPLETO]` — o critério de aceite literal está satisfeito?**
Citação literal (`.planning/REQUIREMENTS.md:87-89`): *"aceite: o teste-ouro do preâmbulo
está registrado em `scripts/test_python.sh` e no CTest, passa hoje, e falha comprovadamente
quando cada emissão é removida (mutação, uma de cada vez). evidência: offline-unit."*
JULGAMENTO: **satisfeito**, com uma ressalva menor (ver Gaps/Observações abaixo) — todas as
partes da frase foram medidas de forma independente nesta sessão: registrado em ambos os
locais (confirmado por grep + pela corrida do `ctest`/`test_python.sh`), passa hoje
(21/21 e 7 PASS/2 SKIP/0 FAIL, medidos), e falha comprovadamente sob mutação (9 linhas de
mutação PASS reproduzidas). A ressalva: a mutação da "quarta emissão" (`ppu_timebase_now`)
cobre com dentes provados o lado do **definidor** (`sys_timer.c`), mas não tem uma mutação
dedicada e isolada para o lado da **declaração** no preâmbulo do lifter (`NEEDLE_TIMEBASE_DECL`
é checado por presença em `check_preamble_emissions()`, mas nunca passa por
`assert_mutation_breaks()` como `NEEDLE_STDLIB` e `NEEDLE_APPLE_TLS` passam). Isso não
invalida o critério — a função de checagem (`check_preamble_needle`) é a mesma já provada
com dentes pelas outras duas emissões — mas é uma lacuna de rigor formal frente à letra
"cada uma das quatro emissões... removida isoladamente" do critério 2 do ROADMAP. Não
bloqueia o veredicto desta fase; registrado como observação, não como gap.

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `tools/ppu_lifter.py` | 4 emissões do preâmbulo + `undefined_targets()`/`emit_fallback_stubs()` + instrumentação `ppu_res_*` | ✓ VERIFIED | Medido: diff `8d98216..1876e70` restrito a essas adições, 0 remoção de linha pré-existente das emissões 1/2 (só reestruturação aditiva do CAS em stwcx/stdcx) |
| `runtime/ppu/tests/test_ppu_lifter_emit.py` | Golden + mutação + gates G4/sync, registrado em ambos os runners | ✓ VERIFIED | 805 linhas adicionadas; corrida direta produz todas as seções esperadas; registrado em `scripts/test_python.sh:110` e `CMakeLists.txt:327` (confirmado por grep) |
| `runtime/syscalls/sys_timer.c` | Definidor `ppu_timebase_now` casando com o contrato do lifter | ✓ VERIFIED | `grep -n ppu_timebase_now` mostra o definidor real na linha 99, comentário explicando o contrato nas linhas 80-88 |
| `games/gow2/build_macos.sh` | Ancora dos 3 sítios que compilam `ppu_stubs.cpp` | ✓ VERIFIED | Ancora D-05 confirmada por execução: 3 sítios |

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `tools/ppu_lifter.py` (preâmbulo) | `runtime/syscalls/sys_timer.c` | declaração `extern "C" uint64_t ppu_timebase_now(void);` casando com definidor real | ✓ WIRED | Confirmado por grep + pelo compile real do chunk `ppu_recomp_006.cpp` (0 erros) |
| `tools/ppu_lifter.py` (`ppu_res_*`) | `include/ps3_mem_trace.h` / `runtime/trace/ps3_mem_trace.c` | forward-declaration `ps3_mem_oob_report` chamada inline, sem `#include` novo | ✓ WIRED | Confirmado por harness compilado+executado (21 asserções PASS) e por `nm -u`/`nm` no chunk real + no `.a` |
| `test_ppu_lifter_emit.py` | `scripts/test_python.sh` + `CMakeLists.txt` (CTest) | registro em ambos os runners | ✓ WIRED | `./scripts/test_python.sh` e `ctest --test-dir build-tests` executados nesta sessão, ambos contam o teste como PASS |
| Gate de sync | `games/gow2/build_macos.sh` vs `../gow2-recomp/build_macos.sh` | `difflib.unified_diff`, WARN-only | ✓ WIRED | Irmão presente nesta máquina: `[AVISO] ... DIVERGENTE (2 hunks)`, exit 0; irmão ausente (via env var): `[PULADO]`, exit 0, sem colidir com `SKIP:` |

### Behavioral Spot-Checks (executados nesta sessão, não copiados do SUMMARY)

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Golden + mutação completos | `.venv/bin/python3 runtime/ppu/tests/test_ppu_lifter_emit.py` | rc=0, todas as seções PASS (ver saída colada acima) | ✓ PASS |
| Runner de projeto | `./scripts/test_python.sh` | `PASS: 7 SKIP: 2 FAIL: 0` | ✓ PASS |
| CTest (armadilha `-DNDEBUG` evitada — `build-tests/` já configurado com `CMAKE_BUILD_TYPE=` vazio) | `ctest --test-dir build-tests --output-on-failure` | 21/21 passam, 2 skips esperados (`py.lift_selftest`/`py.spu_lift_selftest`, falta SDK PS3) | ✓ PASS |
| Gate de sync — irmão ausente não gera "SKIP:" | `PS3_SYNC_GATE_SIBLING=/nao/existe/build_macos.sh .venv/bin/python3 ...` | rc=0; `grep -c "SKIP:"` = 0 (desancorado) e `grep -cE '^SKIP:'` = 0 (ancorado) | ✓ PASS |
| Lift ponta-a-ponta contra o EBOOT real | `python3 tools/ppu_lifter.py EBOOT.ELF --functions functions.json -j 4` (em `../gow2-recomp`) | `extended 180 function(s), 18984 byte(s)`; `0 fallback stubs`; 7 chunks; `ppu_recomp_006.cpp` = 438744 linhas | ✓ PASS |
| Compilação de chunk real com o preâmbulo instrumentado | `clang++ -std=c++20 -O0 -w -c -I ... ppu_recomp_006.cpp -o ppu_recomp_006.o` | exit 0, `.o` de 17MB, `nm -u` mostra `_ppu_vm_size`/`_ps3_mem_oob_report` como indefinidos (esperado, resolvem contra `ppu_loader.o`/`.a`) | ✓ PASS |
| Higiene do repositório após todas as corridas | `git status --porcelain` | limpo (só os 2 diretórios pré-existentes não relacionados a esta fase) | ✓ PASS |

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| `REQ-RDY0-1` | 01-01, 01-02, 01-03 | Emissões do lifter + teste-ouro com mutação | ✓ SATISFIED | Ver julgamento na verificação céptica #5 acima — critério de aceite literal medido e confirmado, com uma ressalva menor documentada (não bloqueante) |

Nenhum requisito órfão detectado para a Fase 1 em `.planning/REQUIREMENTS.md`
(`REQ-RDY0-1` é o único mapeado; linha 381 confirma "Completo (2026-07-26)").

### Anti-Patterns / Observações

| Item | Severidade | Detalhe |
|------|------------|---------|
| Tabela "Progress" no fim do `ROADMAP.md` (linha 128) ainda mostra `1/3, In Progress` para a Fase 1, contradizendo o checkbox `[x]` da linha 36 e a seção "Phase Details" (que diz "3/3 plans executed — CONCLUÍDA") | ℹ️ Info (não bloqueante) | Bookkeeping desatualizado; `STATE.md` está correto (100%, Fase 1 concluída). Não afeta o código nem os testes. Sugiro corrigir a tabela numa próxima edição do ROADMAP. |
| Mutação da "quarta emissão" (`ppu_timebase_now`) só cobre o lado do definidor (`sys_timer.c`), não o lado da declaração no preâmbulo do lifter | ℹ️ Info (não bloqueante) | Ver julgamento #5 acima. A função de checagem reutilizada já tem dentes provados via as outras duas emissões; a lacuna é de rigor formal, não de cobertura funcional real. |
| Nenhum `TBD`/`FIXME`/`XXX`/`TODO`/`HACK`/`PLACEHOLDER` genuíno introduzido nesta fase | — | Grep nos arquivos modificados só encontrou ocorrências pré-existentes de `TODO` (fallback catch-all de opcode, já documentado como padrão antigo) e falsos positivos da palavra portuguesa "todo" (= "every/all"). Confirmado que nenhuma linha adicionada por esta fase (diff `8d98216..1876e70`) contém marcador de dívida real. |

### Human Verification Required

Nenhuma. Todas as truths desta fase são verificáveis por execução automatizada (golden,
mutação, harness compilado, gate G4, gate de sync com e sem irmão) e foram de fato
executadas nesta sessão de verificação, não apenas lidas do SUMMARY.

### Gaps Summary

Nenhum gap bloqueante encontrado. Os quatro critérios de sucesso do ROADMAP para a Fase 1
estão implementados, testados e comprovados por execução independente nesta sessão:

1. As quatro emissões do critério 1 estão presentes e casam com o que o build consome.
2. O teste-ouro está registrado nos dois runners, passa, e falha sob mutação para cada
   emissão (com a ressalva menor documentada sobre a declaração de `ppu_timebase_now`).
3. A mina do re-lift (`ppu_res_*` sem bounds check) está desarmada, com prova em execução
   (harness compilado, 21 asserções).
4. O gate G4 rejeita tokens proibidos fora de guarda `_MSC_VER`/`_WIN32`, com controle
   negativo provado.

O repositório ficou limpo (`git status --porcelain`) após todas as corridas de teste desta
verificação; as fixtures SPU regeneradas pelo `ctest` foram restauradas com
`git checkout --` e não foram commitadas.

---

_Verified: 2026-07-26T07:33:52Z_
_Verifier: Claude (gsd-verifier)_
