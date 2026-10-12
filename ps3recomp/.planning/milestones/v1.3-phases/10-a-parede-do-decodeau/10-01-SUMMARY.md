---
phase: 10-a-parede-do-decodeau
plan: 01
subsystem: infra
tags: [ppu-lifter, patch-scripts, fios, movie-io, gow2]

requires:
  - phase: 09-a-cadeia-fios
    provides: "cadeia FIOS reposta (7 patches), startseq=2, R_PermA lido inteiro (20169344 bytes)"
provides:
  - "inventory_lift_markers.py — inventário reprodutível (3 varreduras) de marcadores apagados pelo re-lift, patches NO-MATCH/UNVERIFIED e patches sem escritor"
  - "patch_2b3d1c_movie_io.py reescrito — repõe AREAD-HLE (early-out de func_002B3D1C) de forma real, não apenas documental"
affects: [10-a-parede-do-decodeau/10-02]

tech-stack:
  added: []
  patterns:
    - "Inventário-antes-de-código: uma ferramenta reutilizável que corre 3 varreduras baratas antes de qualquer patch novo, evitando descoberta peça-a-peça (lição custosa da Fase 9)."
    - "Patch idempotente com recusa atómica (rc=2) quando âncora ausente/duplicada ou pré-requisito (preâmbulo F2B) ausente do mesmo chunk — nunca escreve às cegas."

key-files:
  created:
    - ../gow2-recomp/inventory_lift_markers.py
    - ../gow2-recomp/notes/2026-07-31-fase10-inventario-marcadores.md
  modified:
    - ../gow2-recomp/recomp_mid_v2/patch_2b3d1c_movie_io.py

key-decisions:
  - "O bloco AREAD-HLE foi extraído verbatim de recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:23876-23900 — nunca reescrito de memória, seguindo o método já validado na Fase 9."
  - "A ancoragem usa o fecho do bloco AREAD-PROBE (14 linhas), confirmado byte-idêntico entre o lift de referência e o lift actual — reduz o risco de o patch aplicar no sítio errado."
  - "O patch recusa (rc=2) se o preâmbulo F2B (f2b_fo_mfd_get) não estiver no mesmo chunk que func_002B3D1C, em vez de assumir que os símbolos existem — protege contra migração de chunk num re-lift futuro."

requirements-completed: [REG-03]

coverage:
  - id: D1
    description: "inventory_lift_markers.py corre as 3 varreduras (diff de marcadores, patches NO-MATCH/UNVERIFIED, patches sem escritor) num único comando reprodutível e confirma AREAD-HLE/func_002B3D1C como o marcador sem escritor mais ligado à cadeia movie→DecodeAu"
    requirement: "REG-03"
    verification:
      - kind: other
        ref: "python3 inventory_lift_markers.py | tee /tmp/inventory_fase10.log — grep -q AREAD-HLE && grep -q patch_2b3d1c_movie_io.py && grep -qE 'F2B-STREAM-ALIGN|F2B-STREAM-RESYNC|F2B-STREAM-DESYNC-STOP' → INVENTORY_OK"
        status: pass
    human_judgment: false
  - id: D2
    description: "patch_2b3d1c_movie_io.py deixa de ser um script que só imprime instruções — repõe de facto o early-out AREAD-HLE em func_002B3D1C, idempotente e com compile-check isolado sem erro de símbolo em falta"
    requirement: "REG-03"
    verification:
      - kind: other
        ref: "1a corrida: APPLIED; 2a corrida: ALREADY; grep -c AREAD-HLE ppu_recomp_001.cpp = 1; clang++ -std=c++20 -O0 -c ppu_recomp_001.cpp (mesmas flags do build_macos.sh) rc=0"
        status: pass
    human_judgment: false

duration: 45min
completed: 2026-07-31
status: complete
---

# Phase 10 Plan 01: Inventário completo e reposição do AREAD-HLE Summary

**`inventory_lift_markers.py` (3 varreduras reutilizáveis) confirma o `AREAD-HLE` de `func_002B3D1C` como o marcador apagado sem escritor mais ligado à cadeia movie→DecodeAu; `patch_2b3d1c_movie_io.py` deixou de ser um script que só imprime e agora repõe o bloco verbatim, idempotente e compilando limpo.**

## Performance

- **Duration:** ~45 min
- **Started:** 2026-07-31T18:50:00Z (aprox.)
- **Completed:** 2026-07-31T19:39:00Z
- **Tasks:** 2
- **Files modified:** 3 (1 criado + 1 nota + 1 reescrito)

## Accomplishments
- Inventário completo (não só `FIOS-*`) reprodutível por um único comando: diff de marcadores `pre_v4` vs actual, patches `NO-MATCH`/`UNVERIFIED` do gate (excluindo `PROBE`), e patches `patch_*.py` sem escritor (`write_text`/`.write`/`open(...'w')`).
- Confirmado ao vivo: `AREAD-HLE` (o early-out de `func_002B3D1C` que liga o read assíncrono FIOS ao `movie_io` do `.m2v`) está ausente do lift actual e o `patch_2b3d1c_movie_io.py` que devia repô-lo estava na lista de "sem escritor" — a armadilha #2 da CONTEXT.md (mesma classe do `patch_fios_f2a_f2b_wad.py` da Fase 9).
- Confirmado ao vivo: `F2B-STREAM-ALIGN`, `F2B-STREAM-RESYNC` e `F2B-STREAM-DESYNC-STOP` (órfãos em `func_002BA9BC`, self-declarados "órfão e por instalar" em `patch_f2b_multimb_install.py`) aparecem no diff de marcadores — candidato de reserva já mapeado para o Plano 10-02 (Caso B).
- `patch_2b3d1c_movie_io.py` reescrito: localiza `func_002B3D1C` por assinatura (via `resolve_lift_paths`, imune à mudança de 31→7 chunks), ancora no fecho verbatim do bloco `AREAD-PROBE` (confirmado byte-idêntico entre referência e actual), insere o bloco `AREAD-HLE` extraído verbatim de `recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:23876-23900`, e recusa (rc=2) se a âncora não for única ou se o preâmbulo F2B não estiver no mesmo chunk.
- Medido: 1ª corrida `APPLIED`, 2ª corrida `ALREADY` (idempotente); compile-check isolado do chunk (`clang++ -std=c++20 -O0`, mesmas flags do `build_macos.sh`) termina com rc=0, sem erro de símbolo em falta.

## Task Commits

Cada task foi commitada atomicamente **no repositório `gow2-recomp`** (onde vivem os artefactos físicos do lift — ver estrutura de monorepo do CLAUDE.md):

1. **Task 1 (tracer): inventário reprodutível** - `63dccad` (feat) — `inventory_lift_markers.py` + nota datada
2. **Task 2: repor AREAD-HLE em func_002B3D1C** - `a809bc0` (fix) — `patch_2b3d1c_movie_io.py` reescrito

**Plan metadata:** este SUMMARY será commitado no repositório `ps3recomp` (monorepo de planeamento), separado dos commits físicos acima.

## Files Created/Modified
- `../gow2-recomp/inventory_lift_markers.py` - ferramenta de inventário reutilizável (3 varreduras)
- `../gow2-recomp/notes/2026-07-31-fase10-inventario-marcadores.md` - relatório datado com os achados
- `../gow2-recomp/recomp_mid_v2/patch_2b3d1c_movie_io.py` - reescrito: escritor real do AREAD-HLE

## Decisions Made
- Extracção verbatim do bloco AREAD-HLE (25 linhas) directamente do lift de referência, nunca reescrita de memória — mesma disciplina que resolveu os 7 patches FIOS da Fase 9.
- Âncora escolhida (fecho do bloco AREAD-PROBE) por ser byte-idêntica entre `pre_v4` e o lift actual, confirmado por diff directo antes de escrever o patch.
- Contrato de recusa atómica estendido para verificar o pré-requisito de runtime (preâmbulo F2B no mesmo chunk), não só a unicidade da âncora — protege contra uma classe de falha silenciosa que ainda não tinha ocorrido nesta investigação, mas que o padrão de migração de chunks (31→7) torna plausível.

## Deviations from Plan

None - plan executado exactamente como escrito. A regex inicial do inventário (varredura 1) só capturava tokens em comentários `/* */`, perdendo tokens dentro de literais `fprintf` fora de comentários (ex. `F2B-STREAM-ALIGN`, `F2B-STREAM-DESYNC-STOP` completos). Corrigido inline durante a Task 1 (Regra 1 — bug de extracção, não uma mudança de âmbito) antes do commit: a extracção de tokens "nus" passou a varrer o ficheiro inteiro em vez de só comentários, seguro porque hífens não são válidos em identificadores C++ (só podem vir de comentários ou literais). Também corrigido `write_text(..., newline="\n")`, incompatível com o `python3` de sistema 3.9.6 da Apple (kwarg só existe a partir do 3.10) — removido, já que `\n` explícito no join basta em POSIX.

## Issues Encountered
- `apply_all_patches.sh --status` demora ~90-120s (corre o catálogo completo de 71+ patches, `run_checks` e `verify_lift.sh`) — a varredura 2 do inventário herda esse custo. Corrido em background com poll, sem impacto no resultado.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- O Plano 10-02 pode agora construir o binário de teste a partir do lift já patcheado com `AREAD-HLE` e medir se `cellVdecDecodeAu` passa a ser chamado.
- Se a medição do 10-02 mostrar que a parede quebrou mas há outra a jusante (Caso B), o inventário já mapeou o candidato exacto: o bloco órfão de `func_002BA9BC` (`F2B-STREAM-ALIGN`/`RESYNC`/`DESYNC-STOP`), a partir da linha 31581 de `recomp_macos_v2.pre_v4/ppu_recomp_001.cpp`.
- Nenhuma medição de `DecodeAu`/gate foi feita neste plano — por desenho (ver `<success_criteria>` do 10-01-PLAN.md), é o objectivo do 10-02.

---
*Phase: 10-a-parede-do-decodeau*
*Completed: 2026-07-31*

## Self-Check: PASSED

- FOUND: `../gow2-recomp/inventory_lift_markers.py`
- FOUND: `../gow2-recomp/notes/2026-07-31-fase10-inventario-marcadores.md`
- FOUND: `../gow2-recomp/recomp_mid_v2/patch_2b3d1c_movie_io.py`
- FOUND commit: `63dccad` (Task 1)
- FOUND commit: `a809bc0` (Task 2)
