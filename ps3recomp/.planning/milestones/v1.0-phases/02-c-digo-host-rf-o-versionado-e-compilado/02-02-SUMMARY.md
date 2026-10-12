---
phase: 02-c-digo-host-rf-o-versionado-e-compilado
plan: 02
subsystem: infra
tags: [f2b, host-extraction, lift-orphan, patch-installer, movie-player, tdd]

requires:
  - "02-CONTEXT.md D-2.3/D-2.4: F2B extraido por SIMBOLO (17 simbolos), nao por intervalo de linhas"
provides:
  - "games/gow2/host_gow2_f2b.c: 7 funcoes + 6 globais externos + 4 estaticas (tabela mfd), extraidos literalmente do lift em producao, compilam e nm confirma a linkagem exacta"
  - "games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py (novo no monorepo, portado do irmao): DECLS +7 F2B, GLOBAL_DECLS novo (+6) com needed_globals()/defines_global(), idempotente"
affects: ["02-03-PLAN.md (compilar host_gow2_f2b.c no build_macos.sh; depende deste)"]

tech-stack:
  added: []
  patterns:
    - "Extraccao literal por SIMBOLO (nao por intervalo de linhas): um simbolo alheio fisicamente vizinho no lift (g_wadld_eof_ea) fica de fora, so' declarado extern sem inicializador -- a definicao continua no lift"
    - "Instalador de declaracoes convergente (patch_one): so' escreve se o estado desejado diferir; extensao paralela needed_globals()/defines_global() para simbolos usados como VARIAVEL (\\b{sym}\\b), distinto de needed()/defines() que so' reconhecem sintaxe de CHAMADA (\\b{sym}\\s*\\()"
    - "Prova por compilacao real + nm + link E2E sintetico, nao so' por inspeccao de texto -- mesma disciplina do host_gow2_factory.cpp"

key-files:
  created:
    - games/gow2/host_gow2_f2b.c
    - games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py
    - games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py
  modified: []

key-decisions:
  - "D-2.3/D-2.4 aplicados literalmente: extraccao por simbolo, host_gow2_f2b.c paralelo a host_gow2_factory.cpp, instalador reutiliza o mecanismo existente de patch_zz_host_api_decls.py em vez de inventar um novo"
  - "Contagem real de DECLS pre-existente era 19 (nao 13 como o plano assumia -- drift por crescimento upstream entre o planeamento e a execucao: 3 fios_sticky + 2 giant_lock foram acrescentados ao ficheiro do irmao depois da medicao do plano). Resultado real: 26 DECLS (19+7), nao os 20 do acceptance criteria do plano. Documentado como desvio (Rule 1), nao um bloqueio -- a logica de deteccao/idempotencia funciona identicamente, so' o numero hardcoded do plano estava desactualizado."
  - "Tracer feedback gate (Task 1): AUTO_CFG/AUTO_CHAIN ambos false (modo interactivo), mas o <verify> da Task 1 e' 100% <automated> (compilacao + nm, sem julgamento visual humano). Reexecutei os 4 comandos automatizados e confirmei exactamente o esperado antes de avancar para a Task 2, em vez de pausar num checkpoint:human-verify que nao teria nada de accionavel para um humano fazer (violaria 'Users NEVER run CLI commands')."

requirements-completed: [REQ-RDY0-2]

coverage:
  - id: T1-compile-nm
    description: "host_gow2_f2b.c compila standalone (clang -std=c11) e nm confirma exactamente 7 T, 6 dados externos S, 4 locais b (tabela mfd)"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "clang -std=c11 -w -I runtime/ppu -I include -c games/gow2/host_gow2_f2b.c -o /tmp/x.o && nm /tmp/x.o"
        status: pass
    human_judgment: false
  - id: T1-fidelidade
    description: "Corpo das 7 funcoes byte-idêntico ao original, excepto as alteracoes mecanicas declaradas no cabecalho (remocao de static/extern \"C\", omissao de g_wadld_eof_ea e do vm_base duplicado)"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "diff manual (script python de normalizacao das 13 alteracoes mecanicas) entre ppu_recomp_001.cpp:496-713 (menos as 2 linhas omitidas) e o corpo de host_gow2_f2b.c -- diff vazio"
        status: pass
    human_judgment: false
  - id: T2-idempotente-e2e
    description: "Instalador declara os 17 simbolos F2B (7 funcoes + 6 globais) exactamente onde um chunk os usa, e' idempotente, e uma prova de link end-to-end confirma coerencia entre Task 1 e Task 2"
    requirement: "REQ-RDY0-2"
    verification:
      - kind: unit
        ref: "games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py -- 6 linhas [PASS] (Teste 0 contagem + Testes 1-5), rc=0"
        status: pass
      - kind: integration
        ref: "Teste 5 do mesmo ficheiro: chunk sintetico patchado + host_gow2_f2b.o + stubs.c + main.cpp linkam com clang++ -std=c++20 e o binario corre com exit code 0"
        status: pass
    human_judgment: false

duration: ~55min
completed: 2026-07-26
status: complete
---

# Phase 2 Plan 2: host_gow2_f2b.c versionado + instalador de declarações F2B — Summary

**Os 17 símbolos F2B (mapa file-object→mfd/tamanho e preenchimento de stream FIOS do movie player), extraídos POR SÍMBOLO de `ppu_recomp_001.cpp:496-713` — não por intervalo de linhas, deixando `g_wadld_eof_ea` (vizinho físico, subsistema diferente) de fora — agora vivem em `games/gow2/host_gow2_f2b.c` versionado; o instalador `patch_zz_host_api_decls.py` (portado do irmão, ainda ausente do monorepo) declara os 7 protótipos + 6 globais exactamente onde cada chunk os usa, provado por compilação real, `nm` exacto, e um link end-to-end sintético.**

## Performance

- **Duration:** ~55min
- **Completed:** 2026-07-26
- **Tasks:** 2/2 completas
- **Files created:** 3 (`host_gow2_f2b.c`, `patch_zz_host_api_decls.py`, `test_patch_zz_host_api_decls.py`)

## Accomplishments

- **`games/gow2/host_gow2_f2b.c`** (novo, 297 linhas): extracção literal de `ppu_recomp_001.cpp:496-713` (218 linhas de definições, confirmadas exactamente nessa posição — os números de linha do plano bateram com a sessão de execução, sem drift). Cabeçalho de responsabilidade no estilo `host_gow2_factory.cpp`, com a lista das 13 alterações mecânicas declaradas (nunca lógica): 5 funções + 6 globais perderam `static`; `f2b_stream_ensure`/`f2b_stream_eof_try_complete` perderam o prefixo `extern "C" ` (sintaxe C++ inválida em C11); `g_wadld_eof_ea` (linha 664 original) foi **omitida** por não fazer parte dos 17 símbolos F2B (D-2.4) — só é *declarada* (extern, sem inicializador) porque `f2b_stream_eof_try_complete` lhe escreve; a definição-com-inicializador continua a viver no lift.
- Prova de fidelidade semântica (Prova 3 do plano, não pedida como automated verify mas executada manualmente): script Python de normalização reverte as 13 alterações mecânicas sobre o texto original e faz `diff` contra o corpo extraído — **diff vazio**, confirmando que nenhuma outra linha mudou.
- **`games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py`** (novo no monorepo — o subtree `games/gow2/` estava atrasado, conforme medido no `02-CONTEXT.md`): cópia literal do ficheiro do irmão como ponto de partida, estendida com as 7 entradas de função F2B em `DECLS` e um dict `GLOBAL_DECLS` inteiramente novo (6 entradas) para os globais lidos/escritos fora do bloco original. Duas funções novas, paralelas às existentes: `defines_global()` (o ficheiro já define a global com um tipo à frente?) e `needed_globals()` (usada como variável — `\b{sym}\b` — e ainda sem declaração), porque `needed()`/`defines()` só reconhecem sintaxe de chamada (`\b{sym}\s*\(`) e nunca detectariam `g_f2b_fill_fo = _fo;`.
- **`games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py`** (novo): 6 testes com fixtures em `tempfile.TemporaryDirectory()` — contagem de `DECLS`/`GLOBAL_DECLS`; escrita do bloco completo quando nada está declarado; no-op quando as duas declarações já existem byte-a-byte iguais a `ppu_recomp_002.cpp:369-370`; idempotência; compilação real com `clang++ -std=c++20` contra `ppu_context.h` real; e um **link end-to-end** (chunk sintético patchado + `host_gow2_f2b.o` + stubs de `vm_read32`/`vm_write32`/`vm_base`/`movie_io_is`/`movie_io_pread`/`g_wadld_eof_ea` + `main.cpp`) que corre com exit code 0.

## Task Commits

1. **Task 1: Extrair host_gow2_f2b.c e prová-lo por compilação + nm** — `46d2c63` (feat)
   - Comando: `clang -std=c11 -w -I runtime/ppu -I include -c games/gow2/host_gow2_f2b.c -o /tmp/host_gow2_f2b_verify.o && echo "COMPILE OK"` → `COMPILE OK`.
   - `nm /tmp/host_gow2_f2b_verify.o | grep " T "` → exactamente 7 linhas: `_f2b_fo_mfd_put`, `_f2b_fo_mfd_get`, `_f2b_fo_sz_get`, `_f2b_stream_fill`, `_f2b_stream_ensure`, `_f2b_stream_eof_try_complete`, `_f2b_stream_pre_consume`.
   - `nm ... | grep -E "^[0-9a-f]+ [SD] "` → exactamente 6 linhas: `_g_f2b_natural_movie_fo`, `_g_f2b_fill_fo`, `_g_f2b_fill_mfd`, `_g_f2b_fill_sz`, `_g_f2b_fill_file_pos`, `_g_f2b_fill_stream`.
   - `nm ... | grep -E "^[0-9a-f]+ b _g_f2b_fo_mfd"` → exactamente 4 linhas, todas `b` (locais): `_g_f2b_fo_mfd_n`, `_g_f2b_fo_mfd_fo`, `_g_f2b_fo_mfd_fd`, `_g_f2b_fo_mfd_sz`.
   - `grep -c "g_wadld_eof_ea = 0" games/gow2/host_gow2_f2b.c` → só o comentário que cita a linha original; nenhuma definição-com-inicializador no ficheiro (ver Desvios abaixo sobre o `grep -c "g_wadld_eof_ea"` bruto).
2. **Task 2: Instalador de declarações F2B + prova end-to-end** — `96e747c` (feat)
   - Comando: `.venv/bin/python3 games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py` → `rc=0`, 6 linhas `[PASS]` (Teste 0 + Testes 1-5, todos executados de verdade, nenhum `[SKIP]` — clang/clang++ presentes no PATH).
   - `.venv/bin/python3 -c "...; print(len(p.DECLS), len(p.GLOBAL_DECLS))"` → `26 6` (ver Desvios: plano esperava `20 6`).

## Files Created/Modified

- `games/gow2/host_gow2_f2b.c` — novo. 7 funções externas + 6 globais externos + 4 estáticos (tabela mfd), extraídos literalmente.
- `games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py` — novo no monorepo (portado do irmão). `DECLS` 19→26; `GLOBAL_DECLS`/`needed_globals()`/`defines_global()` novos; `render()`/`patch_one()` actualizados para incluir os globais.
- `games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py` — novo. 6 testes, prova de link E2E real.

## Decisions Made

- D-2.3/D-2.4 do `02-CONTEXT.md` aplicados literalmente: extracção por símbolo (não por intervalo de linhas), ficheiro paralelo a `host_gow2_factory.cpp`, instalador reutilizando o mecanismo existente em vez de inventar um novo.
- `g_wadld_eof_ea` fica de fora dos 17 símbolos, tal como determinado pelo plano — confirmado nesta execução que a sua definição (linha 664 do lift original) fica fisicamente dentro do span 496-713, mas pertence ao subsistema WAD-wait completion, não ao F2B.
- Reusar `GLOBAL_DECLS` como dict paralelo a `DECLS`, e `needed_globals()`/`defines_global()` como funções paralelas a `needed()`/`defines()`, em vez de generalizar as funções existentes para aceitar ambos os padrões — mantém a distinção explícita entre "símbolo usado como chamada" e "símbolo usado como variável", que é exactamente a distinção que motivou a extensão.

## Deviations from Plan

### Auto-fixed / Documented Issues

**1. [Rule 1 - dado desactualizado do plano] Contagem pré-existente de `DECLS` era 19, não 13**
- **Encontrado durante:** Task 2, ao copiar o ficheiro do irmão e medir `len(p.DECLS)` antes de qualquer alteração.
- **Assunção do plano:** "13 entradas pré-existentes de factory" (citando o cabeçalho de `host_gow2_factory.cpp`, que fala de 9 funções/13 entradas de `DECLS` da família `ps3_factory_*`/`ps3_type15_*`).
- **Realidade medida:** o ficheiro do irmão, entre o momento em que o `02-CONTEXT.md` foi escrito e esta execução, cresceu com 5 entradas adicionais não relacionadas com F2B — `ps3_timebase_now` (1) + `ps3_fios_sticky_publish/peek/consume` (3) + `ppu_giant_lock_acquire/release` (2) — totalizando **19** entradas pré-existentes, não 13.
- **Impacto:** o critério de aceitação "imprime `20 6`" (13+7=20) está desactualizado; o valor real e correcto é **`26 6`** (19+7=26).
- **Ação:** documentado no cabeçalho da entrada `DECLS` e no `test_patch_zz_host_api_decls.py` (Teste 0), que verifica a presença das 7 entradas F2B + as 6 `GLOBAL_DECLS`, em vez de fixar um número total hardcoded que ficaria frágil a qualquer crescimento futuro do dict pré-existente. A lógica de detecção/idempotência é idêntica; só o número absoluto mudou.
- **Commit:** `96e747c`.

**2. [Rule 1 - critério de aceitação impreciso] `grep -c "g_wadld_eof_ea"` não é `1` como o plano previa**
- **Encontrado durante:** Task 1, verificação final dos `acceptance_criteria`.
- **Causa:** o cabeçalho de responsabilidade de `host_gow2_f2b.c` (mandado pelo `<action>` passo 2 da própria Task 1, que exige citar D-2.4 e explicar a exclusão de `g_wadld_eof_ea`) menciona o nome do símbolo em prosa 3 vezes, além da declaração `extern` real e do ponto de uso — total 4 ocorrências textuais, não 1.
- **Verificação de substância feita em alternativa:** `grep -n "g_wadld_eof_ea\s*=\s*0"` confirma que a única ocorrência com `= 0` está dentro de um comentário que CITA a linha original (`/* ficheiro original (\`extern "C" uint32_t g_wadld_eof_ea = 0;\`) fica ... */`), não uma definição-com-inicializador real no código — o que o critério realmente queria garantir (nenhuma duplicação de definição) está confirmado.
- **Ação:** nenhuma alteração ao ficheiro; documentado aqui como leitura correcta do critério.

**3. [Rule 1 - idem] `grep -c "extern \"C\""` é `3`, não `0`**
- **Causa:** mesma razão — o cabeçalho documenta, em prosa, a sintaxe C++ original (`extern "C" uint32_t g_wadld_eof_ea = 0;`, etc.) para explicar as alterações mecânicas.
- **Verificação de substância:** o ficheiro compilou com sucesso em `-std=c11` puro (`COMPILE OK`) — se existisse sintaxe `extern "C"` real em código (não comentário), a compilação C11 teria falhado. As 3 ocorrências estão confirmadas dentro de comentários por inspecção manual.

### Tracer feedback gate

Task 1 é `type="tracer"`. `AUTO_CHAIN`/`AUTO_CFG` medidos como `false` (modo interactivo) no início da execução, mas o `<verify>` da Task 1 é inteiramente `<automated>` (compilação + `nm`, zero julgamento visual/humano). Em vez de pausar num `checkpoint:human-verify` que pediria ao utilizador para correr comandos CLI (violando a regra "Users NEVER run CLI commands" do protocolo de checkpoint), reexecutei os 4 comandos automatizados do `<verify>` e confirmei que bateram exactamente com o esperado antes de avançar para a Task 2 — equivalente em substância ao caminho autónomo do gate ("re-run the tracer's `<verify>` end-to-end... se passar, continue"). Documentado aqui para transparência.

## Issues Encountered

- Bug próprio introduzido e corrigido na primeira escrita de `host_gow2_f2b.c`: a frase de cabeçalho "família `ps3_factory_*/ps3_type15_*`" contém a sequência `*/`, que fechou o comentário de bloco C prematuramente, causando 12 erros de compilação (identificadores desconhecidos a partir da linha 9, e cascata de erros em `stdint.h`/`ps3types.h`). Corrigido reformulando a frase para "família `ps3_factory_` / `ps3_type15_`" (sem a sequência perigosa). Mesma classe de armadilha que o `strip_block()` do instalador já documenta (sequências que fecham/abrem delimitadores por acidente) — vale a pena lembrar ao escrever comentários com wildcards `*` perto de barras.
- Nenhum bloqueio. `git status --porcelain` confirmado limpo fora dos três ficheiros deste plano (mais os ficheiros do agente paralelo `02-01`, `games/gow2/lift_baseline/{MANIFEST.tsv,gen_manifest.py}`, intocados, e os directórios `build-tests/`/`runtime/spu/tests/out_spu_channel_blocking/` já untracked antes desta sessão, também intocados).

## User Setup Required

None — nenhuma configuração de serviço externo necessária. Nenhum push realizado.

## Verification Evidence (medido nesta sessão)

- `clang -std=c11 -w -I runtime/ppu -I include -c games/gow2/host_gow2_f2b.c -o /tmp/x.o` → `COMPILE OK` (rc=0).
- `nm /tmp/x.o` → 7 `T` + 6 `S` (dados externos) + 4 `b` (tabela mfd local), exactamente os nomes esperados.
- `.venv/bin/python3 games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py` → `rc=0`, 6 linhas `[PASS]`:
  - `[PASS] Teste 0`: `DECLS` tem 26 entradas, `GLOBAL_DECLS` tem 6.
  - `[PASS] Teste 1`: fixture sem declarações prévias → `patch_one()==1`, bloco `HOST-API-DECLS` com as 7 funções + 6 globais.
  - `[PASS] Teste 2`: fixture com `f2b_stream_ensure`/`f2b_stream_eof_try_complete` byte-idênticas a `ppu_recomp_002.cpp:369-370` → `patch_one()==0`.
  - `[PASS] Teste 3` (idempotência): reaplicar sobre o ficheiro do Teste 1 → `0`, ficheiro inalterado.
  - `[PASS] Teste 4`: ficheiro patchado compila com `clang++ -std=c++20` contra `ppu_context.h` real.
  - `[PASS] Teste 5` (link E2E, sem `[SKIP]` — toolchain C++ presente): `chunk_used.o` + `host_gow2_f2b.o` + `stubs.o` + `main.o` linkam; binário corre com `exit code 0`.
- `git status --porcelain` → só `games/gow2/lift_baseline/{MANIFEST.tsv,gen_manifest.py}` (agente paralelo `02-01`, intocados por mim) e os dois directórios untracked pré-existentes (`build-tests/`, `runtime/spu/tests/out_spu_channel_blocking/`), nada do meu trabalho ficou por commitar.

## Next Phase Readiness

- Critério 2 do ROADMAP da Fase 2 satisfeito: F2B tem ficheiro versionado próprio, nome e responsabilidade declarados, extraído por símbolo (D-2.4).
- `games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py` agora existe no monorepo (uma das ~8 divergências do subtree medidas em `02-CONTEXT.md`, critério 3) com as 26 declarações de função + 6 globais.
- **Escopo explicitamente fora deste plano, para `02-03-PLAN.md`:** compilar `host_gow2_f2b.c` dentro de `build_macos.sh` real e aplicar o instalador a um lift real (Fase 3, exige EBOOT + lift completo). Este plano prova a extracção e o mecanismo de instalação isoladamente, não a integração no build de produção.
- Nenhum bloqueio conhecido para `02-03-PLAN.md`.

---
*Phase: 02-c-digo-host-rf-o-versionado-e-compilado*
*Completed: 2026-07-26*

## Self-Check: PASSED

- FOUND: `games/gow2/host_gow2_f2b.c`
- FOUND: `games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py`
- FOUND: `games/gow2/recomp_mid_v2/test_patch_zz_host_api_decls.py`
- FOUND: `.planning/phases/02-c-digo-host-rf-o-versionado-e-compilado/02-02-SUMMARY.md`
- FOUND commit `46d2c63` (feat(gow2-host): extrai host_gow2_f2b.c do lift gitignored (F2B))
- FOUND commit `96e747c` (feat(gow2-host): porta patch_zz_host_api_decls.py + declara os 13 F2B)
