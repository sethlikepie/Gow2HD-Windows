---
schema_version: 1
open_count: 20
waived_count: 1
fixed_count: 3
total_count: 24
last_updated: 2026-08-12T19:01:38.126Z
---

# Broken Windows Ledger

> Cross-phase defect register. `/gsd-ship` blocks while `open_count > 0`.
> Waive with `gsd-tools windows waive <id> "<reason>"` (reason required).
> Mark fixed with `gsd-tools windows fixed <id>`.

| id | phase | kind | file | line | description | status | reason | recorded_at | resolved_at |
|----|-------|------|------|------|-------------|--------|--------|-------------|-------------|
| 1 | 05 | unrun-verify | N/A |  | placeholder-check | waived | entrada acidental de teste do comando windows append -- nao e um defeito real; nenhum stub/teste-skip/verify-nao-corrido ficou aberto no plano 05-02 | 2026-07-29T14:01:07.611Z | 2026-07-29T14:01:18.283Z |
| 2 | 06 | unmet-truth | .planning/phases/06-diagn-stico-da-regress-o-at-ao-c-digo/06-02-SUMMARY.md |  | REG-02 nao fechado: causa da regressao nao ficou nomeada por ficheiro:linha -- TOCFIX/ctx->lr, strip_block e regressao comportamental do lifter (contagem + vizinhanca de 3 niveis de thr_auto_load) todas refutadas por medicao real (gow2-recomp notes/2026-07-30-fase6-veredictos.md); proxima medicao recomendada para a Fase 8. | open |  | 2026-07-30T19:04:19.353Z |  |
| 3 | 16 | unrun-verify | games/gow2/lift_baseline/PATCH_MIGRATION.tsv |  | 16-02: aceite smoke:bytes_read=20169344 do P0 patch_fallthrough_2550c8.py nao foi corrido (exige boot) -- os outros 18 aceites do ledger correram verdes contra recomp_macos_v2; medir no plano 16-03 | open |  | 2026-08-02T20:45:08.048Z |  |
| 4 | 16 | unmet-truth | runtime/ppu/ppu_loader.cpp |  | 16-03: a metrica 'sticky' do baseline de smoke e NAO-OBSERVAVEL -- ps3_fios_sticky_publish/peek/consume nao tem um unico fprintf em runtime/ nem libs/, logo o grep prescrito mede o nada e um 0 nao distingue 'sticky morto' de 'sem emissor'. O criterio de nao-regressao 'sticky_pub>=1' do CLAUDE.md nao e verificavel por log neste build; para as Fases 17/21 compararem, falta um emissor gated ou outro metodo de observacao | open |  | 2026-08-02T20:54:32.638Z |  |
| 5 | 17 | unmet-truth | tools/ppu_lifter.py |  | 17-01: o contrato de recusa do 17-CONTEXT ('se a instrucao-ancora no EA declarado nao for a esperada, o lifter falha ruidosamente') esta implementado apenas na forma fraca: check_midasm_coverage falha se NAO EXISTIR instrucao no EA (ou se o EA nao for liftado / cair em stub HLE), mas NAO verifica que a instrucao e' a ESPERADA -- o schema [[midasm_hook]] nao tem campo para a declarar. Fechar exige estender o schema (recomp_config.py) com algo tipo expect_insn/expect_word | open |  | 2026-08-02T21:29:14.771Z |  |
| 6 | 17 | deviation | .planning/ROADMAP.md |  | 17-01: progresso da Fase 17 NAO foi actualizado no ROADMAP.md -- o ficheiro esta modificado no working tree por outra sessao activa e edita-lo arriscava commitar trabalho alheio (regra 7 do CLAUDE.md). Reconciliar quando a outra sessao fechar | open |  | 2026-08-02T21:29:14.827Z |  |
| 7 | 17 | stub | games/gow2/hooks/gow2_midasm_hooks.cpp | 46 | 17-02: gow2_midasm_Ce03cWaitIdle e' um stub no-op deliberado ((void)ctx). O simbolo liga e o smoke prova zero mudanca de comportamento, mas o CE03C continua a depender de recomp_mid_v2/patch_ce03c_introseq_block.py. Fecha no 17-03, que poe o corpo real e declara o [[midasm_hook]] no TOML | fixed | 17-03: o corpo real entrou em gow2_midasm_hooks.cpp e o [[midasm_hook]] esta declarado no TOML. Provado: re-lift com --config emite a chamada em func_000CE03C sem correr patch nenhum, objdump mostra o bl _gow2_midasm_Ce03cWaitIdle como 1a instrucao, e o smoke in-boot imprime o marcador [INTROSEQ] CE03C wait-idle 1st movie (o lift tem ZERO ocorrencias dessa string). O que fica por migrar e' o pad de setjmp -- entrada 8, nao esta. | 2026-08-02T21:40:37.644Z | 2026-08-02T22:07:57.000Z |
| 8 | 17 | stub | games/gow2/hooks/gow2_midasm_hooks.cpp |  | 17-03: o pad de setjmp/longjmp do CE03C (g_ce03c_play_abort + cellVdec_stop_all_for_play_abort + reconstrucao da freelist do media object) NAO foi migrado -- um [[midasm_hook]] corre AO LADO de uma instrucao e nao ENVOLVE o corpo da funcao. Num lift feito com --config esse pad nao existe (o patch legado passa a MIDASM/skip). Fecha na Fase 19 (weak overrides) ou com um patch residual minimo so' para o pad | fixed | 19-03: FECHADO por weak override. [[functions_override]] address = 0x000CE03C em games/gow2/config/gow2_recomp.toml + corpo host em games/gow2/hooks/gow2_func_overrides.cpp: envolve __imp_func_000CE03C num setjmp e repoe, no ramo de abort, cellVdec_stop_all_for_play_abort() + a reconstrucao da freelist do media object + a cauda (contador em r30 e trampolim para func_000CE01C) que o longjmp salta. Provado sobre um re-lift SEM patch nenhum: nm -m da' weak external no lift e external no binario ligado (o host ganhou), otool mostra setjmp/bl __imp_/bl _cellVdec_stop_all_for_play_abort no corpo daquele endereco, e as strings do pad existem no binario (1) e NAO no lift (0). Contrato executavel novo weak-ce03c-pad em CONTRACTS.tsv, verde no lift com --config E no lift de producao antigo, vermelho num lift sem os dois mecanismos. RESSALVA HONESTA: o ramo de abort NAO foi exercitado in-boot -- o smoke de intro de 25 s com PS3_MOVIE_EOS=0 nao chega a condicao que o dispara (ICALL-ASCII com StartSeq>=2 e streak>=12); ce03c_play_aborted=0 nas tres corridas. A prova e' estrutural/de link (offline-unit), nao de runtime. O wait-idle esse E' in-boot: 7 linhas [INTROSEQ] CE03C, o mesmo perfil 1/5/1 do 17-03. | 2026-08-02T22:07:23.997Z | 2026-08-03T01:13:59.699Z |
| 9 | 17 | deviation | tools/ppu_lifter.py |  | 17-03: um [[midasm_hook]] e' emitido em TODAS as funcoes liftadas que cobrem o EA, nao numa so'. Medido no piloto: o hook do 0x000CE03C saiu em func_000CE03C (correcto) E em func_000CE02C, cujo intervalo atravessa o mesmo endereco -- ai depois de um return incondicional, logo codigo morto. A guarda de cobertura do 17-01 verifica que o EA EXISTE, nao que corresponde a UMA funcao. Se houvesse um loc_XXXXXXXX: nesse ponto o hook seria alcancavel num contexto nao declarado | open |  | 2026-08-02T22:07:24.050Z |  |
| 10 | 18 | deviation | games/gow2/recomp_mid_v2/patch_jumptable_2b11b8.py |  | 18-03: classificado estado=redundant pelo aceite do ledger (a jump table sai do discover_jump_tables sem patch e sem --config, 22/22 alvos), MAS o patch NAO e no-op: ainda substitui a leitura de TOC-0x151C por tabela hardcoded. Medido na EBOOT que [TOC-0x151C]=0x002B1228, a base exacta que ele hardcoda -- a leitura do lift esta correcta na imagem, o que enfraquece a hipotese original (ICALL-BAD 0x27182818) sem a refutar em runtime. Fechar exige A/B in-boot no caminho typemap/WAD, que o smoke de intro de 25s (PS3_MOVIE_EOS=0) nao exercita | open |  | 2026-08-02T23:12:16.156Z |  |
| 11 | 18 | deviation | games/gow2/recomp_mid_v2/patch_freelist_guard_262610.py |  | 18-03: apply_all_patches.sh sobre um lift novo devolve rc=1 pelo gate de delta UNVERIFIED com 1 NOVO (este patch). NAO e causado pela Fase 18: provado por diff completo entre o lift do caminho real de build e o lift --config do 18-02 -- a unica linha diferente nos 7 chunks e o carimbo /* lifter-rev */, zero bytes funcionais. O mesmo patch ja aparecia UNVERIFIED nos dois apply_all_patches da Fase 17 (/tmp/m1_f17_apply_patches*.log). No conjunto o delta e uma melhoria grande: UNVERIFIED current=12 baseline=42, 31 resolvidos para 1 novo. Fechar exige escrever um contrato executavel para este patch em CONTRACTS.tsv | open |  | 2026-08-02T23:12:31.375Z |  |
| 12 | 20 | deviation | CLAUDE.md |  | 20-02: o ponteiro para docs/RE_PLAYBOOK_GOW2.md NAO foi acrescentado ao CLAUDE.md (o plano listava-o em files_modified). O ficheiro esta modificado no working tree por outra sessao activa e edita-lo arriscava commitar trabalho alheio (regra 7 do CLAUDE.md). A nota para agentes ficou escrita no proprio playbook (cabecalho 'Para agentes'). Fechar = acrescentar 3-4 linhas ao CLAUDE.md quando a outra sessao fechar | open |  | 2026-08-03T01:52:04.687Z |  |
| 13 | 20 | unrun-verify | docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md |  | 20-02: o experimento discriminador E1 da sessao de RE (sonda PS3_TYPEWALK_TRACE=1 em func_0041F700 a despejar w0/low16/subtag/tab[low16]/tab[subtag]/cursor+0xC8) NAO foi corrido -- exige um boot e a sessao foi deliberadamente 100% offline. Sem E1, o mecanismo candidato (push indexa por w0&0xFFFF, tabela registada por (w0>>16)&0xFFF) e' inferencia estatica, nao facto de runtime. Fecha na Fase 11, parede 4 | fixed |  | 2026-08-03T01:52:04.758Z | 2026-08-03T16:07:08.517Z |
| 14 | 20 | deviation | tools/ghidra_lookup.py |  | 20-02: --writes OFFSET so' constroi padroes em hexadecimal e da' falso negativo quando o Ghidra emite o offset em decimal (medido: --writes 0xC8 devolve zero para um campo escrito em func_0041F700 e lido/decrementado em func_0041F924, porque o C sai como *(char *)(param_1 + 200)). Um vazio do --writes nao e' prova de que ninguem escreve. Contornado no playbook (seccao 3.1) com a instrucao de tentar tambem --grep na forma decimal; fechar exige o padrao decimal no proprio --writes | open |  | 2026-08-03T01:52:04.855Z |  |
| 15 | 20 | unrun-verify | docs/GHIDRA_MCP_SETUP.md |  | 20-03: a sessao de paridade do MCP de Ghidra NAO foi corrida do lado do MCP -- declarada wont com razao medida (GhidraMCP 1.4 declara ghidraVersion=11.3.2 vs Ghidra 12.1.2 instalado, ExtensionInstaller.validateExtensionVersion recusa; instalacao so' por GUI; reconstrucao exigiria Maven ausente; sem projecto Ghidra persistido exigiria auto-analise nova do EBOOT; e um agente ja' a correr nao adquire tools MCP). O lado do CLI esta feito e escrito na seccao 9.2 com duas ancoras (0x0041F700 -> vazio; 0x00420A44 -> {0x00254C40}); fechar exige so' correr get_xrefs_to nos dois EA sobre o mesmo EBOOT (sha256 23cfd435..163b) e comparar. RE-03 fecha na mesma: e' soft por desenho | open |  | 2026-08-03T02:14:35.210Z |  |
| 16 | 21 | unrun-verify | docs/RELIFT_CANONICAL.md |  | Sequencia canonica de re-lift nunca corrida 1->8 numa so passagem; cada etapa tem evidencia isolada das Fases 16-20 (§7 do documento) | open |  | 2026-08-03T02:41:40.850Z |  |
| 17 | 21 | deviation | .planning/ROADMAP.md |  | 21-03: ROADMAP.md NAO actualizado nem commitado -- tem alteracoes nao commitadas de OUTRA sessao exactamente nas linhas que o 21-03 teria de editar (bloco 'Plans' da Fase 21 + linhas 17/20/21 da tabela Progress), logo as minhas linhas nao sao separaveis num commit (CLAUDE.md regra 7: nao commitar working tree alheio). PENDENTE, aplicar quando a outra sessao commitar: (1) marcar [x] os 3 planos das Fases 17-21 nos blocos 'Plans'; (2) na tabela Progress pôr as Fases 17,18,19,20,21 a '3/3' e 'Complete'; (3) Fases 16-20 tem *-VERIFICATION.md passed no disco, a 21 ainda nao. A verdade do fecho esta em .planning/REQUIREMENTS.md (rastreabilidade v1.3, 11/11), .planning/STATE.md (bloco 'Fecho medido') e games/gow2/notes/milestone-2026-08-xenon-re-results.md | open |  | 2026-08-03T02:54:06.815Z |  |
| 18 | 21 | unrun-verify | games/gow2/notes/milestone-2026-08-xenon-re-results.md |  | 21-03: o mid-asm NAO esta no lift de producao -- grep -rl 'gow2_midasm' ../gow2-recomp/recomp_macos_v2/ devolve zero ficheiros. O mecanismo esta provado (4 linhas migradas, 3 mecanismos) mas nao esta no caminho que o jogador corre; o smoke T9 usa o binario de producao, logo prova nao-regressao da intro e NAO o mid-asm em execucao. Fechar exige promover um lift com mid-asm (promote_lift.sh, gate proprio, decisao humana) -- e o P1 numero 1 do handoff | open |  | 2026-08-03T02:54:14.486Z |  |
| 19 | 11 | unmet-truth | docs/re_sessions/2026-08-03-E1-typewalk-trace.md |  | E1 mediu a explosao SO' com os gates de diagnostico 1-3 ligados (PS3_LIST254_EMPTY_IF_NULL/PS3_POOL_NULL_IF_BAD/PS3_LIST547_EMPTY_IF_BAD), que sao o que faz o boot chegar ao loop principal; com a receita de base o walk termina em 1176 visitas em 3/3 corridas. Um gate NUNCA fecha uma parede: reproduzir a explosao sem gates exige as paredes 1-3 corrigidas | open |  | 2026-08-03T16:07:19.965Z |  |
| 20 | 11 | unrun-verify | docs/re_sessions/2026-08-03-E1-typewalk-trace.md |  | E1: quem REGISTA o objecto h=0x4077AD5C (w0=0x40030001) no conjunto que o walk exterior func_0041FF70 enumera continua por medir -- e' o alvo do fix. Fecha com um hook em 0x0041FA90 (vt+0x08, o registador) filtrado por w0==0x40030001 a emitir lr+this. Tambem por explicar: tab[0]=0x4003ECB8 tem o cursor +0xC8 em 121 (perto da saturacao dos 127) e nao foi esta chamada que o la' pos | open |  | 2026-08-03T16:07:20.080Z |  |
| 21 | 11 | unmet-truth | docs/re_sessions/2026-08-04-E5-aceite-parede-4.md |  | 11-04: criterio 3 medido SEM gates (fix v2 da parede 4 aplicado, PS3_LIST254_EMPTY_IF_NULL/PS3_POOL_NULL_IF_BAD/PS3_LIST547_EMPTY_IF_BAD confirmados unset) -- thr_created(AUTO_LOAD)=0 em 6/6 corridas, [STATE] frame=1 tambem 0/6. Todas as 6 corridas abortam identicamente com [ppu] FATAL: stuck calling 0x00514E80 (2000x) dentro de func_002545D4, que o ghidra_lookup.py confirma estar 36 bytes DENTRO de func_002545B0 -- a PROPRIA parede 1 do inventario do ROADMAP (PS3_LIST254_EMPTY_IF_NULL), nao corrigida por esta fase. O fix da parede 4 (patch_outer_subtag_prune.py v2) fica confirmado como certo para o SEU alvo mas paredes 1-3 continuam a bloquear sem gates | open |  | 2026-08-04T21:21:37.766Z |  |
| 22 | 23 | unrun-verify | runtime/spu/tests/run_tests.sh |  | suite.runtime_spu e suite.runtime_trace nao correm neste host: cc nu sem -isysroot ('string.h'/'stdio.h' not found); divida de toolchain pre-existente provada em 62a3033 | open |  | 2026-08-12T18:51:45.918Z |  |
| 23 | 23 | unrun-verify | scripts/upstream_block_gate.sh |  | 23-01: --depois-arvore (modo pre-commit) e --antes-bin implementados e nao exercitados; so' a validacao de argumentos do --depois-arvore foi corrida (rc=2). Caminho completo fica para o Plano 23-04 | open |  | 2026-08-12T19:01:38.064Z |  |
| 24 | 23 | deviation | scripts/upstream_block_gate.sh |  | 23-01: o leg medido primeiro absorve a penalizacao de cache fria (medido: antes=1,11,11 vs depois=11,11,11 no MESMO commit), o que envieza ok(depois)>=ok(antes) no sentido de PASSAR. Mitigacao hoje: --runs 6. Ver deferred-items da Fase 23 | open |  | 2026-08-12T19:01:38.126Z |  |

````json
[
  {
    "id": 1,
    "kind": "unrun-verify",
    "phase": "05",
    "file": "N/A",
    "line": null,
    "description": "placeholder-check",
    "status": "waived",
    "reason": "entrada acidental de teste do comando windows append -- nao e um defeito real; nenhum stub/teste-skip/verify-nao-corrido ficou aberto no plano 05-02",
    "recorded_at": "2026-07-29T14:01:07.611Z",
    "resolved_at": "2026-07-29T14:01:18.283Z"
  },
  {
    "id": 2,
    "kind": "unmet-truth",
    "phase": "06",
    "file": ".planning/phases/06-diagn-stico-da-regress-o-at-ao-c-digo/06-02-SUMMARY.md",
    "line": null,
    "description": "REG-02 nao fechado: causa da regressao nao ficou nomeada por ficheiro:linha -- TOCFIX/ctx->lr, strip_block e regressao comportamental do lifter (contagem + vizinhanca de 3 niveis de thr_auto_load) todas refutadas por medicao real (gow2-recomp notes/2026-07-30-fase6-veredictos.md); proxima medicao recomendada para a Fase 8.",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-07-30T19:04:19.353Z",
    "resolved_at": null
  },
  {
    "id": 3,
    "kind": "unrun-verify",
    "phase": "16",
    "file": "games/gow2/lift_baseline/PATCH_MIGRATION.tsv",
    "line": null,
    "description": "16-02: aceite smoke:bytes_read=20169344 do P0 patch_fallthrough_2550c8.py nao foi corrido (exige boot) -- os outros 18 aceites do ledger correram verdes contra recomp_macos_v2; medir no plano 16-03",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-02T20:45:08.048Z",
    "resolved_at": null
  },
  {
    "id": 4,
    "kind": "unmet-truth",
    "phase": "16",
    "file": "runtime/ppu/ppu_loader.cpp",
    "line": null,
    "description": "16-03: a metrica 'sticky' do baseline de smoke e NAO-OBSERVAVEL -- ps3_fios_sticky_publish/peek/consume nao tem um unico fprintf em runtime/ nem libs/, logo o grep prescrito mede o nada e um 0 nao distingue 'sticky morto' de 'sem emissor'. O criterio de nao-regressao 'sticky_pub>=1' do CLAUDE.md nao e verificavel por log neste build; para as Fases 17/21 compararem, falta um emissor gated ou outro metodo de observacao",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-02T20:54:32.638Z",
    "resolved_at": null
  },
  {
    "id": 5,
    "kind": "unmet-truth",
    "phase": "17",
    "file": "tools/ppu_lifter.py",
    "line": null,
    "description": "17-01: o contrato de recusa do 17-CONTEXT ('se a instrucao-ancora no EA declarado nao for a esperada, o lifter falha ruidosamente') esta implementado apenas na forma fraca: check_midasm_coverage falha se NAO EXISTIR instrucao no EA (ou se o EA nao for liftado / cair em stub HLE), mas NAO verifica que a instrucao e' a ESPERADA -- o schema [[midasm_hook]] nao tem campo para a declarar. Fechar exige estender o schema (recomp_config.py) com algo tipo expect_insn/expect_word",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-02T21:29:14.771Z",
    "resolved_at": null
  },
  {
    "id": 6,
    "kind": "deviation",
    "phase": "17",
    "file": ".planning/ROADMAP.md",
    "line": null,
    "description": "17-01: progresso da Fase 17 NAO foi actualizado no ROADMAP.md -- o ficheiro esta modificado no working tree por outra sessao activa e edita-lo arriscava commitar trabalho alheio (regra 7 do CLAUDE.md). Reconciliar quando a outra sessao fechar",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-02T21:29:14.827Z",
    "resolved_at": null
  },
  {
    "id": 7,
    "kind": "stub",
    "phase": "17",
    "file": "games/gow2/hooks/gow2_midasm_hooks.cpp",
    "line": 46,
    "description": "17-02: gow2_midasm_Ce03cWaitIdle e' um stub no-op deliberado ((void)ctx). O simbolo liga e o smoke prova zero mudanca de comportamento, mas o CE03C continua a depender de recomp_mid_v2/patch_ce03c_introseq_block.py. Fecha no 17-03, que poe o corpo real e declara o [[midasm_hook]] no TOML",
    "status": "fixed",
    "reason": "17-03: o corpo real entrou em gow2_midasm_hooks.cpp e o [[midasm_hook]] esta declarado no TOML. Provado: re-lift com --config emite a chamada em func_000CE03C sem correr patch nenhum, objdump mostra o bl _gow2_midasm_Ce03cWaitIdle como 1a instrucao, e o smoke in-boot imprime o marcador [INTROSEQ] CE03C wait-idle 1st movie (o lift tem ZERO ocorrencias dessa string). O que fica por migrar e' o pad de setjmp -- entrada 8, nao esta.",
    "recorded_at": "2026-08-02T21:40:37.644Z",
    "resolved_at": "2026-08-02T22:07:57.000Z"
  },
  {
    "id": 8,
    "kind": "stub",
    "phase": "17",
    "file": "games/gow2/hooks/gow2_midasm_hooks.cpp",
    "line": null,
    "description": "17-03: o pad de setjmp/longjmp do CE03C (g_ce03c_play_abort + cellVdec_stop_all_for_play_abort + reconstrucao da freelist do media object) NAO foi migrado -- um [[midasm_hook]] corre AO LADO de uma instrucao e nao ENVOLVE o corpo da funcao. Num lift feito com --config esse pad nao existe (o patch legado passa a MIDASM/skip). Fecha na Fase 19 (weak overrides) ou com um patch residual minimo so' para o pad",
    "status": "fixed",
    "reason": "19-03: FECHADO por weak override. [[functions_override]] address = 0x000CE03C em games/gow2/config/gow2_recomp.toml + corpo host em games/gow2/hooks/gow2_func_overrides.cpp: envolve __imp_func_000CE03C num setjmp e repoe, no ramo de abort, cellVdec_stop_all_for_play_abort() + a reconstrucao da freelist do media object + a cauda (contador em r30 e trampolim para func_000CE01C) que o longjmp salta. Provado sobre um re-lift SEM patch nenhum: nm -m da' weak external no lift e external no binario ligado (o host ganhou), otool mostra setjmp/bl __imp_/bl _cellVdec_stop_all_for_play_abort no corpo daquele endereco, e as strings do pad existem no binario (1) e NAO no lift (0). Contrato executavel novo weak-ce03c-pad em CONTRACTS.tsv, verde no lift com --config E no lift de producao antigo, vermelho num lift sem os dois mecanismos. RESSALVA HONESTA: o ramo de abort NAO foi exercitado in-boot -- o smoke de intro de 25 s com PS3_MOVIE_EOS=0 nao chega a condicao que o dispara (ICALL-ASCII com StartSeq>=2 e streak>=12); ce03c_play_aborted=0 nas tres corridas. A prova e' estrutural/de link (offline-unit), nao de runtime. O wait-idle esse E' in-boot: 7 linhas [INTROSEQ] CE03C, o mesmo perfil 1/5/1 do 17-03.",
    "recorded_at": "2026-08-02T22:07:23.997Z",
    "resolved_at": "2026-08-03T01:13:59.699Z"
  },
  {
    "id": 9,
    "kind": "deviation",
    "phase": "17",
    "file": "tools/ppu_lifter.py",
    "line": null,
    "description": "17-03: um [[midasm_hook]] e' emitido em TODAS as funcoes liftadas que cobrem o EA, nao numa so'. Medido no piloto: o hook do 0x000CE03C saiu em func_000CE03C (correcto) E em func_000CE02C, cujo intervalo atravessa o mesmo endereco -- ai depois de um return incondicional, logo codigo morto. A guarda de cobertura do 17-01 verifica que o EA EXISTE, nao que corresponde a UMA funcao. Se houvesse um loc_XXXXXXXX: nesse ponto o hook seria alcancavel num contexto nao declarado",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-02T22:07:24.050Z",
    "resolved_at": null
  },
  {
    "id": 10,
    "kind": "deviation",
    "phase": "18",
    "file": "games/gow2/recomp_mid_v2/patch_jumptable_2b11b8.py",
    "line": null,
    "description": "18-03: classificado estado=redundant pelo aceite do ledger (a jump table sai do discover_jump_tables sem patch e sem --config, 22/22 alvos), MAS o patch NAO e no-op: ainda substitui a leitura de TOC-0x151C por tabela hardcoded. Medido na EBOOT que [TOC-0x151C]=0x002B1228, a base exacta que ele hardcoda -- a leitura do lift esta correcta na imagem, o que enfraquece a hipotese original (ICALL-BAD 0x27182818) sem a refutar em runtime. Fechar exige A/B in-boot no caminho typemap/WAD, que o smoke de intro de 25s (PS3_MOVIE_EOS=0) nao exercita",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-02T23:12:16.156Z",
    "resolved_at": null
  },
  {
    "id": 11,
    "kind": "deviation",
    "phase": "18",
    "file": "games/gow2/recomp_mid_v2/patch_freelist_guard_262610.py",
    "line": null,
    "description": "18-03: apply_all_patches.sh sobre um lift novo devolve rc=1 pelo gate de delta UNVERIFIED com 1 NOVO (este patch). NAO e causado pela Fase 18: provado por diff completo entre o lift do caminho real de build e o lift --config do 18-02 -- a unica linha diferente nos 7 chunks e o carimbo /* lifter-rev */, zero bytes funcionais. O mesmo patch ja aparecia UNVERIFIED nos dois apply_all_patches da Fase 17 (/tmp/m1_f17_apply_patches*.log). No conjunto o delta e uma melhoria grande: UNVERIFIED current=12 baseline=42, 31 resolvidos para 1 novo. Fechar exige escrever um contrato executavel para este patch em CONTRACTS.tsv",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-02T23:12:31.375Z",
    "resolved_at": null
  },
  {
    "id": 12,
    "kind": "deviation",
    "phase": "20",
    "file": "CLAUDE.md",
    "line": null,
    "description": "20-02: o ponteiro para docs/RE_PLAYBOOK_GOW2.md NAO foi acrescentado ao CLAUDE.md (o plano listava-o em files_modified). O ficheiro esta modificado no working tree por outra sessao activa e edita-lo arriscava commitar trabalho alheio (regra 7 do CLAUDE.md). A nota para agentes ficou escrita no proprio playbook (cabecalho 'Para agentes'). Fechar = acrescentar 3-4 linhas ao CLAUDE.md quando a outra sessao fechar",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T01:52:04.687Z",
    "resolved_at": null
  },
  {
    "id": 13,
    "kind": "unrun-verify",
    "phase": "20",
    "file": "docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md",
    "line": null,
    "description": "20-02: o experimento discriminador E1 da sessao de RE (sonda PS3_TYPEWALK_TRACE=1 em func_0041F700 a despejar w0/low16/subtag/tab[low16]/tab[subtag]/cursor+0xC8) NAO foi corrido -- exige um boot e a sessao foi deliberadamente 100% offline. Sem E1, o mecanismo candidato (push indexa por w0&0xFFFF, tabela registada por (w0>>16)&0xFFF) e' inferencia estatica, nao facto de runtime. Fecha na Fase 11, parede 4",
    "status": "fixed",
    "reason": "",
    "recorded_at": "2026-08-03T01:52:04.758Z",
    "resolved_at": "2026-08-03T16:07:08.517Z"
  },
  {
    "id": 14,
    "kind": "deviation",
    "phase": "20",
    "file": "tools/ghidra_lookup.py",
    "line": null,
    "description": "20-02: --writes OFFSET so' constroi padroes em hexadecimal e da' falso negativo quando o Ghidra emite o offset em decimal (medido: --writes 0xC8 devolve zero para um campo escrito em func_0041F700 e lido/decrementado em func_0041F924, porque o C sai como *(char *)(param_1 + 200)). Um vazio do --writes nao e' prova de que ninguem escreve. Contornado no playbook (seccao 3.1) com a instrucao de tentar tambem --grep na forma decimal; fechar exige o padrao decimal no proprio --writes",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T01:52:04.855Z",
    "resolved_at": null
  },
  {
    "id": 15,
    "kind": "unrun-verify",
    "phase": "20",
    "file": "docs/GHIDRA_MCP_SETUP.md",
    "line": null,
    "description": "20-03: a sessao de paridade do MCP de Ghidra NAO foi corrida do lado do MCP -- declarada wont com razao medida (GhidraMCP 1.4 declara ghidraVersion=11.3.2 vs Ghidra 12.1.2 instalado, ExtensionInstaller.validateExtensionVersion recusa; instalacao so' por GUI; reconstrucao exigiria Maven ausente; sem projecto Ghidra persistido exigiria auto-analise nova do EBOOT; e um agente ja' a correr nao adquire tools MCP). O lado do CLI esta feito e escrito na seccao 9.2 com duas ancoras (0x0041F700 -> vazio; 0x00420A44 -> {0x00254C40}); fechar exige so' correr get_xrefs_to nos dois EA sobre o mesmo EBOOT (sha256 23cfd435..163b) e comparar. RE-03 fecha na mesma: e' soft por desenho",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T02:14:35.210Z",
    "resolved_at": null
  },
  {
    "id": 16,
    "kind": "unrun-verify",
    "phase": "21",
    "file": "docs/RELIFT_CANONICAL.md",
    "line": null,
    "description": "Sequencia canonica de re-lift nunca corrida 1->8 numa so passagem; cada etapa tem evidencia isolada das Fases 16-20 (§7 do documento)",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T02:41:40.850Z",
    "resolved_at": null
  },
  {
    "id": 17,
    "kind": "deviation",
    "phase": "21",
    "file": ".planning/ROADMAP.md",
    "line": null,
    "description": "21-03: ROADMAP.md NAO actualizado nem commitado -- tem alteracoes nao commitadas de OUTRA sessao exactamente nas linhas que o 21-03 teria de editar (bloco 'Plans' da Fase 21 + linhas 17/20/21 da tabela Progress), logo as minhas linhas nao sao separaveis num commit (CLAUDE.md regra 7: nao commitar working tree alheio). PENDENTE, aplicar quando a outra sessao commitar: (1) marcar [x] os 3 planos das Fases 17-21 nos blocos 'Plans'; (2) na tabela Progress pôr as Fases 17,18,19,20,21 a '3/3' e 'Complete'; (3) Fases 16-20 tem *-VERIFICATION.md passed no disco, a 21 ainda nao. A verdade do fecho esta em .planning/REQUIREMENTS.md (rastreabilidade v1.3, 11/11), .planning/STATE.md (bloco 'Fecho medido') e games/gow2/notes/milestone-2026-08-xenon-re-results.md",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T02:54:06.815Z",
    "resolved_at": null
  },
  {
    "id": 18,
    "kind": "unrun-verify",
    "phase": "21",
    "file": "games/gow2/notes/milestone-2026-08-xenon-re-results.md",
    "line": null,
    "description": "21-03: o mid-asm NAO esta no lift de producao -- grep -rl 'gow2_midasm' ../gow2-recomp/recomp_macos_v2/ devolve zero ficheiros. O mecanismo esta provado (4 linhas migradas, 3 mecanismos) mas nao esta no caminho que o jogador corre; o smoke T9 usa o binario de producao, logo prova nao-regressao da intro e NAO o mid-asm em execucao. Fechar exige promover um lift com mid-asm (promote_lift.sh, gate proprio, decisao humana) -- e o P1 numero 1 do handoff",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T02:54:14.486Z",
    "resolved_at": null
  },
  {
    "id": 19,
    "kind": "unmet-truth",
    "phase": "11",
    "file": "docs/re_sessions/2026-08-03-E1-typewalk-trace.md",
    "line": null,
    "description": "E1 mediu a explosao SO' com os gates de diagnostico 1-3 ligados (PS3_LIST254_EMPTY_IF_NULL/PS3_POOL_NULL_IF_BAD/PS3_LIST547_EMPTY_IF_BAD), que sao o que faz o boot chegar ao loop principal; com a receita de base o walk termina em 1176 visitas em 3/3 corridas. Um gate NUNCA fecha uma parede: reproduzir a explosao sem gates exige as paredes 1-3 corrigidas",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T16:07:19.965Z",
    "resolved_at": null
  },
  {
    "id": 20,
    "kind": "unrun-verify",
    "phase": "11",
    "file": "docs/re_sessions/2026-08-03-E1-typewalk-trace.md",
    "line": null,
    "description": "E1: quem REGISTA o objecto h=0x4077AD5C (w0=0x40030001) no conjunto que o walk exterior func_0041FF70 enumera continua por medir -- e' o alvo do fix. Fecha com um hook em 0x0041FA90 (vt+0x08, o registador) filtrado por w0==0x40030001 a emitir lr+this. Tambem por explicar: tab[0]=0x4003ECB8 tem o cursor +0xC8 em 121 (perto da saturacao dos 127) e nao foi esta chamada que o la' pos",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-03T16:07:20.080Z",
    "resolved_at": null
  },
  {
    "id": 21,
    "kind": "unmet-truth",
    "phase": "11",
    "file": "docs/re_sessions/2026-08-04-E5-aceite-parede-4.md",
    "line": null,
    "description": "11-04: criterio 3 medido SEM gates (fix v2 da parede 4 aplicado, PS3_LIST254_EMPTY_IF_NULL/PS3_POOL_NULL_IF_BAD/PS3_LIST547_EMPTY_IF_BAD confirmados unset) -- thr_created(AUTO_LOAD)=0 em 6/6 corridas, [STATE] frame=1 tambem 0/6. Todas as 6 corridas abortam identicamente com [ppu] FATAL: stuck calling 0x00514E80 (2000x) dentro de func_002545D4, que o ghidra_lookup.py confirma estar 36 bytes DENTRO de func_002545B0 -- a PROPRIA parede 1 do inventario do ROADMAP (PS3_LIST254_EMPTY_IF_NULL), nao corrigida por esta fase. O fix da parede 4 (patch_outer_subtag_prune.py v2) fica confirmado como certo para o SEU alvo mas paredes 1-3 continuam a bloquear sem gates",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-04T21:21:37.766Z",
    "resolved_at": null
  },
  {
    "id": 22,
    "kind": "unrun-verify",
    "phase": "23",
    "file": "runtime/spu/tests/run_tests.sh",
    "line": null,
    "description": "suite.runtime_spu e suite.runtime_trace nao correm neste host: cc nu sem -isysroot ('string.h'/'stdio.h' not found); divida de toolchain pre-existente provada em 62a3033",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-12T18:51:45.918Z",
    "resolved_at": null
  },
  {
    "id": 23,
    "kind": "unrun-verify",
    "phase": "23",
    "file": "scripts/upstream_block_gate.sh",
    "line": null,
    "description": "23-01: --depois-arvore (modo pre-commit) e --antes-bin implementados e nao exercitados; so' a validacao de argumentos do --depois-arvore foi corrida (rc=2). Caminho completo fica para o Plano 23-04",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-12T19:01:38.064Z",
    "resolved_at": null
  },
  {
    "id": 24,
    "kind": "deviation",
    "phase": "23",
    "file": "scripts/upstream_block_gate.sh",
    "line": null,
    "description": "23-01: o leg medido primeiro absorve a penalizacao de cache fria (medido: antes=1,11,11 vs depois=11,11,11 no MESMO commit), o que envieza ok(depois)>=ok(antes) no sentido de PASSAR. Mitigacao hoje: --runs 6. Ver deferred-items da Fase 23",
    "status": "open",
    "reason": "",
    "recorded_at": "2026-08-12T19:01:38.126Z",
    "resolved_at": null
  }
]
````
