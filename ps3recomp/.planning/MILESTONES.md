# Milestones

## v1.2 Toolchain Go PoC (Queued: 2026-08-02 — não activo)

**Status:** planeado, **não** em execução. O marco activo continua a ser **v1.1** (boot / walls).

**Objectivo:** provar (ou refutar) migração seletiva da toolchain `tools/` Python → Go com **parity gates**, sem big-bang e sem tocar no path default de `boot_gow2`.

**Artefactos:**

- Requisitos: [`milestones/v1.2-REQUIREMENTS.md`](milestones/v1.2-REQUIREMENTS.md) (GO-01 … GO-12)
- Roadmap: [`milestones/v1.2-ROADMAP.md`](milestones/v1.2-ROADMAP.md) (Fases **12–15**)
- Veredicto (a criar no fecho): `milestones/v1.2-VERDICT.md` → PROMOTE | HOLD | HYBRID

**DoD resumido:** disasm parity ≥99,9% no corpus; lift golden N≥20; flag opt-in; veredicto escrito.  
**Fora:** apagar Python, patches GoW2, runtime Metal, full 52k funcs, Xenon midasm em Go.

**Activar:** `/gsd-new-milestone v1.2 Toolchain Go PoC` (após pausa/fecho v1.1) ou workstream paralelo.

---

## v1.0 RDY-0: desbloquear o re-lift (Shipped: 2026-07-30)

**Phases completed:** 5 phases, 19 plans, 36 tasks

**Key accomplishments:**

- Restaura a geracao de `ppu_stubs.cpp` (apagada pelo `11a1c3c`) e fixa por teste-ouro as quatro emissoes que o build consome, com prova por mutacao em todo run.
- Desarma a mina do re-lift: `ppu_res_lwarx/stwcx/ldarx/stdcx` do `SOURCE_PREAMBLE` agora chamam `ps3_mem_oob_report` em acesso fora dos limites (report-only, matriz len/op D-03), com a fatia compilada e rodada por um harness dedicado que prova o caminho dentro-dos-limites permanece bit-a-bit igual.
- Fecha o critério 4 (varredura de tokens proibidos por-ramo, com controle negativo) e a metade de sincronização do critério 1 (D-05/D-06) — último plano da Fase 1, que agora tem os quatro critérios do ROADMAP satisfeitos.
- O gate `verify_lift_baseline.sh` passa a cobrir as 131 linhas do preambulo puro via 2 marcadores `kind=PREAMBLE` (`g_trampoline_fn`, `ps3_indirect_call`), provados por mutacao, com `ps3_timebase_now` deliberadamente excluido e as tres cifras do criterio 1 (1603/1662/1793) reconciliadas no proprio `MANIFEST.tsv`.
- Os 17 símbolos F2B (mapa file-object→mfd/tamanho e preenchimento de stream FIOS do movie player), extraídos POR SÍMBOLO de `ppu_recomp_001.cpp:496-713` — não por intervalo de linhas, deixando `g_wadld_eof_ea` (vizinho físico, subsistema diferente) de fora — agora vivem em `games/gow2/host_gow2_f2b.c` versionado; o instalador `patch_zz_host_api_decls.py` (portado do irmão, ainda ausente do monorepo) declara os 7 protótipos + 6 globais exactamente onde cada chunk os usa, provado por compilação real, `nm` exacto, e um link end-to-end sintético.
- `games/gow2/build_macos.sh` do monorepo passa a compilar e ligar condicionalmente os dois ficheiros extraídos (`host_gow2_factory.cpp` já existente, `host_gow2_f2b.c` do plano 02-02), cada um com guard anti-duplicate-symbol — mas o guard do F2B foi REDESENHADO a meio da execução face a um achado crítico medido nesta sessão: 5 das 7 funções e os 10 globais são `static` (linkagem interna) no lift de produção real, não `extern "C"` como o plano assumia por analogia com o factory — um guard só-extern deixaria estado duplicado em silêncio em vez de "duplicate symbol" ruidoso.
- `RELIFT=1 ./build_macos.sh` regenera o lift a partir do EBOOT.ELF/functions.json num directório sempre novo (recomp_macos_v3), com guarda que aborta se o alvo for recomp_macos_v2 — provado por hash de conteúdo que a produção fica intacta.
- `patch_14b1f0_opd.py` passou a aceitar os dois formatos de restauro do TOC (vm_read64 antigo e TOCFIX actual) e a distinguir CONVERTIDO/JA-APLICADO/SEM-EFEITO por rc — fechando as 12 conversões OPD que faltavam para `ps3_call_opd` chegar a 47 chamadas no projecto (35→47).
- `gen_manifest.py::verify()` ganhou os mecanismos OBSOLETO/GRUPO (D-3.1/D-3.2); `[WADLD-ALLOC]` ganhou instalador dedicado (D-3.3); e a primeira corrida real do catálogo completo de 87 patches contra um `recomp_macos_v3` genuinamente fresco produziu 243/246 (rc=1, honesto) — o par OPD acoplado fica com um deficit residual de 36 que a correção do Plano 03-02 não eliminou contra um lift fresco.
- Os 87 patches do catálogo ficam classificados em **64 FUNCIONAL / 23 PROBE / 0 indeterminados** — batendo exatamente a medição do planeamento — com a ausência de indeterminados provada por um teste Python que percorre as linhas estruturadas do TSV em vez de as extrair por `cut`/`grep`.
- `patch_14a01c_opd.py` corrigido com duas emendas ao que o plano assumia: são **4 sítios, não 2** (`func_0014A01C` e `func_0014AD94` têm 2 conversões cada), e o conflito de posse com `patch_icg_ctor_opd.py` era real — os dois reivindicavam as mesmas funções *porque* o `14a01c` estava partido, pelo que corrigir um faria o outro imprimir `SEM-EFEITO` (rc≠0 no desenho novo). A posse foi transferida e verificada na ordem real do glob alfabético.
- `verify_lift.sh` orquestra lift_parity.py (--functions), audit_boundaries.py (I1/I3) e verify_lift_baseline.sh contra baselines congelados nesta task (77 e 180 achados reais contra recomp_macos_v3), falhando so por achado NOVO -- prova por injecao de que detecta regressao e nunca falha por melhoria.
- `check_contracts.py` verifica, de forma INDEPENDENTE do que um patch imprime, se a pos-condicao declarada em `CONTRACTS.tsv` e verdade no lift -- 56 contratos (6 da familia OPD + 50 derivados do cruzamento TAG-MANIFEST), 22/64 patches FUNCIONAL cobertos, com prova de injeccao de falha real e um achado de falso-positivo (marcador OBSOLETO) corrigido antes do commit.
- `apply_all_patches.sh` reescrito com classificacao de SEIS estados (D-4.1) verificada por `check_contracts.py`, `--status <tsv>`, encadeamento de `verify_lift.sh` no fim (D-4.6) e prova de injecao de falha REAL fim-a-fim (NO-MATCH + rc=1 numa copia scratch inteira, nao so a chamada isolada) -- fecha a Fase 4; corrida real honesta contra `recomp_macos_v3` fica em rc=1 por 42 `UNVERIFIED` nao cobertos por contrato e pelo `MANIFEST FAIL` herdado da Fase 3, exatamente como o plano previa.
- Script de smoke com build automatico (FORCE_REBUILD_LIFT=1), 6 execucoes reais contra recomp_macos_v3 (st620=11 em 6/6, rc=0), TSV por corrida, portao >=4-de-6, modo --bin sem rebuild, e discriminador FLAKE/REGRESSAO provado offline com fixtures sinteticas -- descobriu no processo uma chaveta em falta que partia a compilacao do candidato ha 3 dias sem que ninguem soubesse.
- `verify_lift.sh` deixa de nascer cronicamente vermelho: o passo MANIFEST passa a exigir zero achados NOVOS contra uma dívida congelada e medida (36 ids `GRUPO:opd-dispatch`), não `rc=0` literal do verificador subjacente -- provado por injeção real nos dois sentidos.
- `accept_relift.sh` orquestra o aceite de promocao inteiro num so comando: smoke (perna 1) + `verify_lift.sh` (perna 2) + `apply_all_patches.sh --status` parseado por NO-MATCH/FAILED-fora-de-PROBE (perna 3, NUNCA o rc bruto que tambem conta os 62 UNVERIFIED conhecidos) + contadores por delta contra um baseline REAL congelado (D-5.2) -- rc=0 so se tudo passar, provado ACEITANDO `recomp_macos_v3` real e REJEITANDO em tres mutacoes isoladas, uma por perna.
- `promote_lift.sh` formaliza a promocao a producao que ate agora so tinha sido feita a mao (2026-07-26): gate obrigatorio de tres pernas, backup por rename, rebuild, confirmacao pos-build, reversao automatica -- e um registo de auditoria (`PROMOTION_LOG.tsv`) que sobrevive a uma interrupcao externa real, depois de uma promocao real ter sido interrompida a meio durante a propria rehearsal desta task e o coordenador ter exposto que o log so' se escrevia no fim.
- `recomp_macos_v3` é agora produção real e permanente (`rc=0`, suffix `20260729_150649`, sem reversão): o `.gitignore` de `gow2-recomp` foi corrigido para cobrir as variantes com sufixo que a Fase 3 introduziu (sem isso o critério 4 era estruturalmente inatingível), e o `ACCEPTANCE-REPORT.md` fecha o Marco 1 (RDY-0) declarando os 5 limites sem maquiagem -- incluindo o descoberto nesta sessão: o smoke não cobre WADLD nem o caminho de shaders, exatamente onde a perda de 36 conversões OPD se escondeu em 2026-07-26.

---
