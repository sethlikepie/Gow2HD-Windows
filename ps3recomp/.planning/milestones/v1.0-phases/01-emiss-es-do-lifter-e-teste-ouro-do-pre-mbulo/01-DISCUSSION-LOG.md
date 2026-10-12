# Phase 1: Emissões do lifter e teste-ouro do preâmbulo - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-07-26
**Phase:** 1-Emissões do lifter e teste-ouro do preâmbulo
**Areas discussed:** Mina dos 4 atômicos, Fonte de verdade do build, Prova por mutação, Onde vivem teste e gate G4

---

## Mina dos 4 atômicos

| Option | Description | Selected |
|--------|-------------|----------|
| Instrumentar inline | Bounds check + `ps3_mem_oob_report` dentro dos quatro `ppu_res_*` no `SOURCE_PREAMBLE`. Self-contained, evita o choque de convenção com `ppu_memory.h`. | ✓ |
| Declarar a perda por escrito | Não mexer no lifter; registrar a perda de instrumentação com nível de evidência e custo. | |
| Fundir com `ppu_memory.h` | Reusar o par `ppu_lwarx`/`ppu_stwcx` do runtime. O próprio comentário do lifter desaconselha. | |

**User's choice:** Instrumentar inline
**Notes:** A opção foi apresentada já corrigida pela leitura de `tools/ppu_lifter.py:288-345`, feita durante a discussão. O plano RDY-0 sugeria um trabalho maior; na verdade os helpers são self-contained (só usam `vm_base`), então instrumentar é cirúrgico. Descoberta subsequente que reforçou a escolha: `vm_oob` é `static` em `ppu_loader.cpp:399` e portanto inutilizável do preâmbulo, mas `ps3_mem_oob_report` é público e já linkado. A pergunta "abortar ou seguir em OOB?" **não foi feita** — o contrato de `ps3_mem_oob_report` já a responde (report-only, gated, com cap).

---

## Fonte de verdade do build

| Option | Description | Selected |
|--------|-------------|----------|
| Monorepo + gate de sync | Ancorar em `games/gow2/build_macos.sh` e adicionar check de divergência contra o irmão, com `SKIP:` quando ausente. | ✓ |
| Só o monorepo | Simples e sempre verde em CI, mas valida cópia comprovadamente defasada. | |
| Só o irmão | Fiel à realidade, mas viveria em SKIP permanente na CI — o único alvo bloqueante. | |

**User's choice:** Monorepo + gate de sync
**Notes:** A área só existiu porque o scout descobriu, ao vivo, que as duas cópias divergem: `../gow2-recomp/build_macos.sh` tem um bloco de 18 linhas para `host_gow2_factory` (commit `1f651aa`) ausente no monorepo.

---

## Tratamento da divergência atual (follow-up de "Fonte de verdade do build")

| Option | Description | Selected |
|--------|-------------|----------|
| WARN agora, FAIL depois | Gate avisa enquanto a divergência conhecida existir; vira FAIL na Fase 2, cujo domínio é o `host_gow2_factory`. | ✓ |
| FAIL desde já | Honesto e sem débito escondido, mas bloqueia a Fase 1 num trabalho de sincronização de subtree de outro escopo. | |
| Sincronizar antes, FAIL depois | Começa limpo, mas puxa `git subtree pull` para dentro de uma fase cujo domínio é emissão do lifter. | |

**User's choice:** WARN agora, FAIL depois
**Notes:** A promoção para FAIL ficou registrada como ideia diferida ligada à Fase 2.

---

## Prova por mutação

| Option | Description | Selected |
|--------|-------------|----------|
| Automatizada em todo run | O teste remove cada emissão em memória, reroda e exige falha. Prova viva. | ✓ |
| Alvo sob demanda | CI rápida, mas a prova vira ritual manual e o teste apodrece sem ninguém notar. | |
| Fixtures fixadas | Barato, mas as fixtures congelam e passam a testar formato morto. | |

**User's choice:** Automatizada em todo run
**Notes:** Custo aceito explicitamente — três passagens extras por execução, operações de string e não compilação, irrelevantes frente ao harness C++ que o teste já roda.

---

## Onde vivem teste e gate G4

| Option | Description | Selected |
|--------|-------------|----------|
| Estender o emit-test | Ambos em `test_ppu_lifter_emit.py`, já registrado nos dois lugares. Zero fiação nova. | ✓ |
| Arquivo irmão novo | Separação limpa por granularidade, mas exige registrar nos dois lugares — espelhamento manual e fácil de esquecer. | |
| Teste no emit, G4 à parte | G4 como script próprio em `scripts/`, por escanear a saída inteira. | |

**User's choice:** Estender o emit-test
**Notes:** Custo aceito: o arquivo cresce e passa a misturar granularidade de instrução com a de arquivo.

---

## Escopo do scan G4 (follow-up de "Onde vivem teste e gate G4")

| Option | Description | Selected |
|--------|-------------|----------|
| Preâmbulo + templates | Varre o `SOURCE_PREAMBLE` e as strings de emissão do lifter. Roda em CI sem EBOOT nem lift. | ✓ |
| Saída de um lift real | Cobertura verdadeira do artefato, mas vive em SKIP na CI. | |
| Os dois, em níveis | Cobre ambos os vetores, ao custo de duas rotinas para manter. | |

**User's choice:** Preâmbulo + templates
**Notes:** A lacuna resultante — tokens injetados por `patch_*.py` não são pegos — ficou declarada no CONTEXT.md e diferida para quando o re-lift existir (Fase 3 ou 4).

---

## Claude's Discretion

- Forma exata do bounds check inline no preâmbulo, desde que não dependa de `vm_oob`.
- Estrutura interna da rotina de mutação dentro do teste.
- Nomes de funções e organização das novas seções em `test_ppu_lifter_emit.py`.

## Deferred Ideas

- Promover o gate de sync do `build_macos.sh` para FAIL — Fase 2.
- Varredura G4 sobre a saída de um lift real, cobrindo `patch_*.py` — Fase 3 ou 4.
- Fechar as três cifras do inventário de host órfão (1603 / 1662 / 1793) e fazer o
  `verify_lift_baseline.sh` cobrir `preamble_pure.cpp` — Fase 2.
- `git subtree pull` do gow2-recomp — manutenção, fora do domínio da Fase 1.

## Nota de processo

Nenhum `01-DISCUSS-CHECKPOINT.json` foi escrito. O modo default prevê checkpoint após
cada área; as quatro áreas foram conduzidas em duas rodadas agrupadas em vez de oito
turnos isolados, e o `CONTEXT.md` foi escrito na mesma sessão. Sem interrupção, o
checkpoint não teve função — registrado aqui para o desvio não passar despercebido.
