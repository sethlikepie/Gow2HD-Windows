---
phase: 10-a-parede-do-decodeau
plan: 02
subsystem: infra
tags: [ppu-lifter, patch-scripts, fios, movie-io, cellvdec, gow2, type15]

requires:
  - phase: 10-a-parede-do-decodeau
    provides: "AREAD-HLE reposto (10-01) — patch_2b3d1c_movie_io.py real, inventário completo"
provides:
  - "cellVdecDecodeAu confirmado a disparar 10x em 6/6 corridas (identico a referencia) — a parede do DecodeAu esta vencida, medido nao inferido"
  - "patch_f2ba9bc_stream_align_install.py — repõe F2B-BODY-CLAMP/F2B-STREAM-ALIGN/F2B-STREAM-RESYNC/F2B-STREAM-DESYNC-STOP em func_002BA9BC, elimina o abort FREELIST-TAG-GUARD need=0x31304350"
  - "veredicto REG-03 PARCIAL, honesto — nova parede TYPE15/func_000CB56C mapeada com log lado-a-lado (referencia vs teste)"
affects: [11-parede-type15-cb56c-nao-planeada]

tech-stack:
  added: []
  patterns:
    - "Binário de teste dedicado (boot_gow2_decodeau_test, gitignored) para medir sem tocar produção — mesma disciplina de smoke_chain_gate.sh --bin da Fase 7."
    - "Comparação log-a-log referência vs teste no MESMO ponto de execução para localizar a bifurcação exacta, em vez de inferir a partir de contadores agregados."

key-files:
  created:
    - ../gow2-recomp/recomp_mid_v2/patch_f2ba9bc_stream_align_install.py
    - ../gow2-recomp/notes/2026-07-31-fase10-decodeau-veredicto.md
  modified: []

key-decisions:
  - "Caso B seguido exactamente como pré-mapeado no 10-01: DecodeAu>=1 em 6/6 mas thr_end<1 em 6/6 -> instalar o bloco órfão de func_002BA9BC (F2B-STREAM-ALIGN/RESYNC/DESYNC-STOP), nunca misturar com o Caso C (que não se aplicava, pois DecodeAu nunca foi 0)."
  - "Antes de aceitar 'sem solução', corrida a 3/3 do binário de referência (boot_gow2.pre_v3) para confirmar que thr_end=1 é genuinamente alcançável — não uma meta hipotética."
  - "A nova parede (TYPE15/CB56C) foi caracterizada por comparação directa dos dois logs no mesmo ponto de execução, não por suposição — reconecta com o item já adiado desde a Fase 6 no CLAUDE.md (<deferred>: 'A parede TYPE15 e os probes 29AF0/CB56C... podem ser reavaliados')."

requirements-completed: []

coverage:
  - id: D1
    description: "cellVdecDecodeAu medido (nao inferido) em 6 corridas reais contra binario de teste dedicado, confirmando a parede do DecodeAu vencida (10x, identico a referencia)"
    requirement: "REG-03"
    verification:
      - kind: other
        ref: "grep -c '[cellVdec] DecodeAu #' em cada um dos 6 logs de /tmp/chain_gate_decodeau_test2.tsv -> 10 em 6/6"
        status: pass
    human_judgment: false
  - id: D2
    description: "REG-03 (thr_auto_load end>=1 em >=4/6) classificado honestamente como PARCIAL — nao fecha, com causa da nova parede identificada por comparacao directa de logs (TYPE15/func_000CB56C) e documentada para a proxima fase"
    requirement: "REG-03"
    verification:
      - kind: other
        ref: "../gow2-recomp/notes/2026-07-31-fase10-decodeau-veredicto.md — tabela medida completa + log lado-a-lado referencia vs teste"
        status: pass
    human_judgment: true
    rationale: "O veredicto classifica REG-03 como PARCIAL (nao PASS) — decisao de continuar ou nao para uma fase nova (TYPE15/CB56C) e do dominio do usuario/roadmap, nao uma verificacao automatizavel."

duration: 90min
completed: 2026-07-31
status: complete
---

# Phase 10 Plan 02: Build de teste, gate de 6, e veredicto honesto do REG-03 Summary

**`cellVdecDecodeAu` confirmado a disparar 10x em 6/6 corridas (idêntico à referência) — a parede do DecodeAu está vencida; `thr_auto_load` continua 0/6 porque existe uma parede nova, mapeada por comparação directa de logs: `TYPE15`/`func_000CB56C` toma um ramo `(match WAD path)` inválido que a referência não toma.**

## Performance

- **Duration:** ~90 min (2 builds ~2min cada + 2 gates de 6 corridas ~9min cada + 1 gate de referência de 3 corridas ~4min + investigação de log)
- **Started:** 2026-07-31T19:41:00Z (aprox.)
- **Completed:** 2026-07-31T21:10:00Z (aprox.)
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments
- Binário de teste `boot_gow2_decodeau_test` construído a partir do lift já patcheado no 10-01 (nunca sobre `boot_gow2` de produção), medido com `smoke_chain_gate.sh --bin`.
- **`cellVdecDecodeAu` medido directamente (não inferido) a disparar 10x por corrida em 6/6 corridas — número idêntico ao binário de referência.** A parede do DecodeAu, objectivo nomeado desta fase, está vencida.
- `REPLAY-NOPIC`/`PICOUT` skip confirmado 4x por corrida (consequência esperada, per CONTEXT.md — não investigado, por desenho).
- `thr_auto_load() end` continua 0 em 6/6: REG-03 não fecha na primeira medição pós-AREAD-HLE. Classificado como **Caso B** (parede quebrou, há outra a jusante) — nunca misturado com Caso A ou C.
- Investigação honesta do Caso B: o inventário do 10-01 já tinha mapeado o bloco órfão de `func_002BA9BC` (`F2B-BODY-CLAMP`/`F2B-STREAM-ALIGN`/`F2B-STREAM-RESYNC`/`F2B-STREAM-DESYNC-STOP`, 148 linhas). `patch_f2ba9bc_stream_align_install.py` (novo) repõe-no verbatim, extraído de `recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:31664-31811`.
- Rebuild + re-medição de 6 corridas: o patch **eliminou por completo** o abort `[FREELIST-TAG-GUARD] 262610 entry ... need=0x31304350` (header com tamanho absurdo perto do fim do R_PermA) — confirmado em 6/6 corridas, correcção real e medida, não um no-op. Mas `thr_end` continua 0/6.
- Antes de declarar a causa nova, corrida o binário de referência 3x para confirmar que `thr_end=1`/`elo_stopped=nenhum` é genuinamente alcançável (3/3 OK) — não uma meta hipotética.
- **Localizada a causa exacta da parede nova por comparação directa dos dois logs no mesmo ponto de execução:** logo após o R_PermA completo, a referência entra em `func_000CB56C` (#1 e #2), corre `POSTINTRO B70B0`/`B71`/`FACTORY REPAIR`, e chama `sys_ppu_thread_create name="AUTO_LOAD"`. O binário de teste toma um ramo diferente em `TYPE15`/`func_000CB56C` (anotado `(match WAD path)`, `idx=0x0/ent=0x40637C08` em vez de `idx=0x54/ent=0x47D00000`), produz um `product=0x4F72626F` claramente inválido, nunca atinge a 2ª entrada de `func_000CB56C`, e por isso nunca chama `sys_ppu_thread_create("AUTO_LOAD")` (confirmado ausente nos 6 logs).
- Esta nova parede reconecta com um item já adiado desde a Fase 6 (`CLAUDE.md <deferred>`: "A parede TYPE15 e os probes `29AF0`/`CB56C`... à espera de o boot chegar ao `R_PermA` — o que já acontece desde a Fase 9; podem ser reavaliados") — agora observável pela primeira vez.

## Task Commits

Cada task foi commitada atomicamente **no repositório `gow2-recomp`**:

1. **Task 1: build de teste + medição directa de DecodeAu + gate de 6 corridas** - sem commit próprio (artefactos são o binário gitignored `boot_gow2_decodeau_test` e TSVs em `/tmp`, ambos fora do controlo de versão por desenho)
2. **Task 2: acção honesta (Caso B) + veredicto escrito de REG-03** - `dc4c29f` (fix) — `patch_f2ba9bc_stream_align_install.py` + nota de veredicto

**Plan metadata:** este SUMMARY será commitado no repositório `ps3recomp`.

## Files Created/Modified
- `../gow2-recomp/recomp_mid_v2/patch_f2ba9bc_stream_align_install.py` - repõe o bloco órfão F2B-STREAM-ALIGN/RESYNC/DESYNC-STOP
- `../gow2-recomp/notes/2026-07-31-fase10-decodeau-veredicto.md` - veredicto completo com tabela medida e log lado-a-lado

## Decisions Made
- Seguido exactamente o Caso B (não misturado com A ou C) — a classificação estava inequívoca: `DecodeAu>=1` em todas as 6 corridas, `thr_end<1` em todas as 6.
- Corrida de referência (3x, `boot_gow2.pre_v3`) antes de fechar o veredicto, para confirmar que o alvo (`thr_end=1`) é real e não uma meta desactualizada.
- A causa da parede nova foi caracterizada por comparação directa de logs no mesmo ponto de execução (método mais rigoroso do que inferir a partir de contadores agregados) — decisão de não tentar corrigi-la nesta fase, por estar fora do âmbito nomeado da Fase 10 (CONTEXT.md) e por já estar identificada como trabalho adiado da Fase 6.

## Deviations from Plan

None - plan executado exactamente como escrito. O Caso B do plano previa possivelmente fechar com PASS após o patch F2B-STREAM-ALIGN; a medição honesta mostrou que não fechou — o próprio plano previa este resultado como válido ("Regista o resultado novo (PASS ou ainda PARCIAL) — nunca reivindica mais do que o medido").

## Issues Encountered
- Nenhum bloqueador técnico. A investigação da causa raiz da parede nova (TYPE15/CB56C) exigiu leitura cuidadosa de ~4150 linhas de log por corrida em 2 binários — resolvido por grep direccionado nos marcadores conhecidos (`POSTINTRO`, `TYPE15`, `FACTORY`) em vez de leitura linear.

## User Setup Required
None - no external service configuration required.

## Next Phase Readiness
- **REG-03 não fecha nesta fase** — declarado explicitamente PARCIAL, não forjado como PASS.
- A próxima fase tem um alvo concreto e medido: `TYPE15`/`func_000CB56C`, o ramo `(match WAD path)` com `idx=0x0/ent=0x40637C08` que produz `product=0x4F72626F` (inválido) em vez de `idx=0x54/ent=0x47D00000` (válido, `reused=1`). Os probes `29AF0`/`CB56C` da Fase 6 (adiados) são o ponto de partida natural.
- **REG-04** (sobrevivência a re-lift do zero) continua bloqueado — só avaliável depois de REG-03 fechar.
- Marco v1.1 não fecha nesta fase — a parede do DecodeAu (o objectivo nomeado da Fase 10) está vencida, mas o boot ainda não chega ao `thr_auto_load`.

---
*Phase: 10-a-parede-do-decodeau*
*Completed: 2026-07-31*

## Self-Check: PASSED

- FOUND: `../gow2-recomp/recomp_mid_v2/patch_f2ba9bc_stream_align_install.py`
- FOUND: `../gow2-recomp/notes/2026-07-31-fase10-decodeau-veredicto.md`
- FOUND commit: `dc4c29f` (Task 2)
- Binário de teste `boot_gow2_decodeau_test` confirmado gitignored (nunca commitado, nunca sobrepôs produção)
