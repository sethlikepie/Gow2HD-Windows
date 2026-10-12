---
phase: 05-smoke-de-equival-ncia-e-promo-o
verified: 2026-07-29T23:10:00Z
status: passed
score: 5/5 must-haves verified
behavior_unverified: 0
overrides_applied: 0
---

# Phase 5: Smoke de equivalência e promoção — Verification Report

**Phase Goal (ROADMAP.md):** "O lift regenerado é aceite ou rejeitado por medição
repetida, e só depois de aceito substitui o de produção — fechando o ciclo re-lift →
prova → promoção."

**Verified:** 2026-07-29
**Status:** passed
**Re-verification:** No — initial verification (retomada após queda por erro 500 do
servidor; a verificação já feita não mudou — incorporo abaixo um dado novo trazido pelo
coordenador, da verificação paralela da Fase 3, e aprofundo as duas perguntas que mais
lhe interessam)

**Método:** código lido diretamente (`promote_lift.sh`, `accept_relift.sh`,
`smoke_relift_equiv.sh`, `manifest_delta_gate.py`), `PROMOTION_LOG.tsv` lido do disco,
`.gitignore` e `git status --porcelain` medidos independentemente nos dois repositórios,
md5 dos 7 chunks `.cpp` de `recomp_macos_v2` (produção) recomputado e comparado byte-a-
-byte contra `recomp_macos_v3` (candidato), `ppu_loader.cpp:780-784` lido para confirmar
a citação do ACCEPTANCE-REPORT, `git log` cruzado com os commits citados em cada SUMMARY
(motivado pelo achado da Fase 3 — ver abaixo), `ctest --test-dir build-tests` corrido
(22/22, 2 py skipped) e as fixtures regeneradas restauradas com `git checkout --`
conforme instruído. **Nada foi promovido, revertido, ou executado contra produção nesta
verificação** — a promoção já feita não foi tocada.

## Goal Achievement

### Observable Truths (Success Criteria do ROADMAP, Fase 5)

| # | Truth (ROADMAP) | Status | Evidência |
|---|---|---|---|
| 1 | `smoke_relift_equiv.sh` existe, sai 0, `st620 max>=3` em `>=4 de 6` | ✓ VERIFIED | `../gow2-recomp/smoke_relift_equiv.sh` existe, mirror `games/gow2/smoke_relift_equiv.sh` byte-idêntico (`diff` vazio, confirmado). Código: kill por PID (`kill -TERM $BPID ...; kill -9 $BPID`), nunca `pkill`. `PROMOTION_LOG.tsv` real mostra a corrida de aceite da promoção final com `PERNA 1 OK=6/6, st620_max=11` (citado em `05-05-SUMMARY.md`, consistente com o mecanismo lido no código). |
| 2 | Contadores por delta contra baseline congelado (D-5.2); `imp_modules`/`imp_imports`/`orfaos` absolutos | ✓ VERIFIED | `games/gow2/lift_baseline/COUNTERS_BASELINE.tsv` existe no disco com `function_table_count=51917 boot_lifted_functions=52068 imp_modules=13 imp_imports=151 orfaos=0` — lido diretamente, bate com o que `accept_relift.sh` (lido) implementa (`counters_pass` exige `imp_modules`/`imp_imports`/`orfaos` == baseline, os dois primeiros só informativos). `56072`/`56223` do ROADMAP confirmados como do lift antigo, com justificativa escrita (functions fora de secção executável, zero starts/EAs em falta) presente em `05-CONTEXT.md` e no código de `accept_relift.sh`. |
| 3 | Protocolo G6 (kill por PID, `PS3_ENGINE_ROOT` explícito, logs fora dos repos) | ✓ VERIFIED | `grep` em `smoke_relift_equiv.sh`: `PS3_NO_RSX=1 PS3_RSX_BACKEND=trace`, `PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$HERE/../ps3recomp}"`, `kill -TERM $BPID ...; kill -9 $BPID`, zero ocorrências de `pkill`. Logs citados em `/tmp/promote_confirm_*.tsv`, `/tmp/promote_final_run1.log` — fora dos dois repositórios. |
| 4 | `git status` limpo no escopo da fase (nos dois repos); promoção escrita, reversível por hash, executada uma vez real e permanente | ✓ VERIFIED | Ver secção dedicada abaixo — verificação de código + log + hash independente. |
| 5 | Relatório de aceite declara os 5 limites sem maquiagem | ✓ VERIFIED | `ACCEPTANCE-REPORT.md` lido por inteiro: os 5 itens (R_PermA fora, present não coberto, Gate A RED esperado citando `ppu_loader.cpp:780-784`, smoke não cobre WADLD/shaders, 42/64 patches FUNCIONAL sem contrato) estão todos presentes. Citação de `ppu_loader.cpp:780-784` conferida linha a linha contra o ficheiro real — bate exatamente. `ROADMAP.md` linha ~26 confirma por escrito "Gate A continua RED como valor esperado". |

**Score:** 5/5 truths verified (0 present-but-behavior-unverified)

### Perguntas cépticas do pedido — respostas diretas

**1. A promoção passou mesmo pelo gate?**
Sim — verificado por **leitura do código**, não por confiança na narrativa. Em
`promote_lift.sh:cmd_promote()`, a chamada a `accept_relift.sh` (linha 202) e o teste do
`gate_rc` (linhas 203-207) acontecem **antes** de qualquer captura de hash, geração de
sufixo ou `log_promotion`. Se `gate_rc != 0`, o script sai (`exit 1`) sem nunca chegar à
linha 236 (`log_promotion "PROMOTE-BEGIN" ...`). Logo, a simples **existência** de uma
linha `PROMOTE-BEGIN` para o sufixo `20260729_150649` em `PROMOTION_LOG.tsv` (lido
diretamente do disco: `19:06:49Z PROMOTE-BEGIN recomp_macos_v3 20260729_150649 ...`) é,
por construção do código, prova de que `accept_relift.sh` retornou `rc=0` antes de
qualquer toque em produção.

**2. `PROMOTE-BEGIN` é escrito ANTES de tocar em produção? (verificado no CÓDIGO, não no SUMMARY — pergunta prioritária do coordenador)**
Sim, confirmado linha a linha no ficheiro real `../gow2-recomp/promote_lift.sh`:

```
202:    "$REPO/accept_relift.sh" "$NEW_LIFT_REL"      # GATE — corre primeiro
203:    local gate_rc=$?
204:    if [ "$gate_rc" != "0" ]; then
205:        echo "ABORTADO: ..." >&2
206:        exit 1                                      # sai SEM tocar em nada se o gate falhar
207:    fi
...
236:    log_promotion "PROMOTE-BEGIN" "$NEW_LIFT_REL" "$SUFFIX" "$H_V2_BEFORE" "$H_BOOT_BEFORE"
...
240:    if ! mv "$REPO/recomp_macos_v2" "$REPO/recomp_macos_v2.pre_${SUFFIX}"; then   # 1º mv real
```

`PROMOTE-BEGIN` (linha 236) é escrito **quatro linhas antes** do primeiro `mv` real
sobre produção (linha 240), e só é alcançável depois de o gate já ter passado (linha
202-207). Não há nenhum caminho de código entre 236 e 240 que toque em `recomp_macos_v2`
ou `boot_gow2`. Isto confirma estruturalmente — não pela narrativa do SUMMARY — que o
defeito antigo (log só escrito no fim, exposto por uma interrupção real durante a
rehearsal desta fase, documentada em `05-04-SUMMARY.md`) está mesmo corrigido no
ficheiro que hoje está no disco, não apenas descrito como corrigido. Esta é
precisamente a mesma classe de disciplina que a Fase 3 mostrou não poder ser dada como
garantida (ver achado cruzado abaixo) — por isso confirmei linha de código, não me bastei
com a descrição em prosa do plano/SUMMARY.

**3. A reversibilidade é real?**
Sim, com evidência de código + registo, sem eu ter reexecutado nada contra produção
(instrução explícita): (a) **código**: `do_revert()` só declara sucesso se `hash_dir()`
pós-revert bater exatamente com o hash esperado — caso contrário imprime `ERRO FATAL` e
devolve 1, nunca finge sucesso; (b) **registo real**: `PROMOTION_LOG.tsv` contém duas
linhas `REVERT` reais (18:22:45 e 18:48:26) intercaladas com `PROMOTE`/`PROMOTE-BEGIN`/
`PROMOTE-OK` do mesmo `hash_v2_antes=7bf905232...` repetido em todas as entradas daquele
dia — os ciclos promover→reverter sucessivos sempre voltaram ao mesmo estado de
produção antes da promoção definitiva (150649), o que só é possível se o revert por
hash tivesse mesmo funcionado (o código aborta com `ERRO FATAL` se não bater).

**4. `ACCEPTANCE-REPORT.md` declara os cinco limites sem maquiagem?**
Sim, os cinco estão lá, cada um citável: `R_PermA` fora do aceite (item 1); present não
coberto por `PS3_NO_RSX=1` (item 2); Gate A RED confirmado contra `ppu_loader.cpp:780-
784` real (item 3, citação conferida por mim, bate); smoke não cobre WADLD/shaders,
citando explicitamente a perda de 36 conversões OPD de 26/jul como o precedente que
motivou a perna 2 (item 4); 42/64 patches FUNCIONAL sem contrato (item 5).

**5. Critério 4 (`git status` limpo) — a reformulação é aceitável ou é interpretação generosa?**

**Atualizado com o dado novo da Fase 3, diretamente relevante para esta pergunta:** o
verificador da Fase 3 encontrou que o fecho REAL do gap D-3.6 (herdado da Fase 2) veio
de um commit (`6b50c4c`) que **nenhum SUMMARY da Fase 3 cita** — trabalho real, não
registado onde devia. É um precedente concreto, nesta mesma sessão de trabalho, de que
"nada ficou por commitar" e "todo o trabalho está rastreado" não são a mesma afirmação.
Por isso fiz o mesmo cruzamento para a Fase 5: comparei `git log --oneline` dos dois
repositórios na janela 2026-07-28→2026-07-30 contra os hashes citados nos 5
`05-0X-SUMMARY.md`. **Resultado: nenhum commit órfão.** Todos os commits reais de
`gow2-recomp` na janela (`375d58d`, `7d6303e`, `fdf1f48`, `a3cca16`, `06d5915`,
`927423a`, `721f85a`) e os de `ps3recomp` que tocam `games/gow2`/
`.planning/phases/05` aparecem citados nalgum `05-0X-SUMMARY.md`. Isto não prova que a
Fase 5 é estruturalmente imune ao mesmo defeito da Fase 3 — prova que, desta vez, medi
especificamente por esse gap e não o encontrei, em vez de presumir que não existe.

Quanto à reformulação em si: **continuo a achar aceitável, não uma interpretação
generosa** — está **escrita no próprio ROADMAP.md** (não inventada ad-hoc no SUMMARY) e
justificada por uma regra explícita e anterior à fase (CLAUDE.md regra 7). Medi eu
mesmo, independentemente: os únicos itens sujos em `gow2-recomp` são exatamente os
citados como pré-existentes a esta fase (3 ficheiros modificados de outra sessão + um
conjunto de notas/artefactos não rastreados, nenhum relacionado com os artefactos desta
fase). O `.gitignore` fix era estruturalmente necessário — sem ele, toda promoção
futura deixaria sempre um `.pre_<SUFFIX>` novo untracked, tornando "git status limpo"
impossível para sempre, não só nesta fase.

**6. Critério 2 (contadores do lift antigo) — reformulado com justificação, ou ignorado?**
Reformulado com justificação escrita e mensurável, não ignorado. `05-CONTEXT.md` mede
explicitamente `51917`/`52068` (regenerado) vs `56072`/`56223` (antigo, escrito à mão,
31 chunks) e explica a diferença: ~4200 funções a menos, das quais 1919 fora de
qualquer secção executável; zero starts/EAs em falta. `COUNTERS_BASELINE.tsv` (lido do
disco) implementa a reformulação como baseline real congelado, com os dois contadores
dependentes do lift marcados INFORMATIVO e os três independentes (`imp_modules`/
`imp_imports`/`orfaos`) mantidos absolutos e bloqueantes.

### Required Artifacts

| Artefacto | Esperado | Status | Detalhes |
|---|---|---|---|
| `../gow2-recomp/smoke_relift_equiv.sh` | script real, `[LIFT] [RUNS] [TSV]` + `--bin` | ✓ VERIFIED | Existe; grep confirma protocolo G6 e ausência de `pkill` |
| `games/gow2/smoke_relift_equiv.sh` | mirror | ✓ VERIFIED | `diff` vazio |
| `../gow2-recomp/accept_relift.sh` | orquestrador de 3 pernas + contadores | ✓ VERIFIED | Lido por inteiro; lógica de rc final = AND das 3 pernas + contadores bloqueantes confirmada no código |
| `games/gow2/accept_relift.sh` | mirror | ✓ VERIFIED | `diff` vazio |
| `../gow2-recomp/promote_lift.sh` | gate + backup + rebuild + confirmação + `--revert` | ✓ VERIFIED | Lido por inteiro; ordem gate→backup confirmada linha a linha |
| `games/gow2/promote_lift.sh` | mirror | ✓ VERIFIED | `diff` vazio |
| `../gow2-recomp/PROMOTION_LOG.tsv` | append-only, 3 estados | ✓ VERIFIED | Lido do disco: contém `PROMOTE-BEGIN`→`PROMOTE-OK` para a promoção final e histórico de rehearsals |
| `games/gow2/lift_baseline/manifest_delta_gate.py` | conversor de achados MANIFEST → IDs opacos | ✓ VERIFIED | Existe (7431 bytes) + `test_manifest_delta_gate.py` (8004 bytes) |
| `games/gow2/lift_baseline/MANIFEST_DEBT_BASELINE.json` | dívida congelada (36 tokens) | ✓ VERIFIED | Referenciado por `manifest_delta_gate.py` e citado nos SUMMARYs |
| `games/gow2/lift_baseline/COUNTERS_BASELINE.tsv` | baseline real congelado (D-5.2) | ✓ VERIFIED | Lido do disco, valores conferem com `ACCEPTANCE-REPORT.md` |
| `../gow2-recomp/.gitignore` | padrões `recomp_macos_v*/`, `boot_gow2*` | ✓ VERIFIED | Confirmado por `grep` no ficheiro real |
| `.planning/phases/.../ACCEPTANCE-REPORT.md` | relatório de aceite D-5.4 | ✓ VERIFIED | Lido por inteiro, 5 limites presentes |

### Key Link Verification

| From | To | Via | Status | Detalhes |
|---|---|---|---|---|
| `promote_lift.sh` | `accept_relift.sh` | chamado antes de qualquer `mv`/`cp` de produção | ✓ WIRED | Confirmado por leitura de código (linhas 202-208 antes de 236-244) |
| `accept_relift.sh` | `games/gow2/verify_lift.sh` | via `PS3_ENGINE_ROOT` (ponte cross-repo) | ✓ WIRED | Código lido: `VERIFY_LIFT="$PS3_ENGINE_ROOT/games/gow2/verify_lift.sh"`, checado executável antes de correr |
| `accept_relift.sh` | `apply_all_patches.sh --status` | parse do TSV, não do rc bruto | ✓ WIRED | Confirmado: `n_nomatch`/`n_failed_nao_probe` extraídos do TSV via `awk`, não de `$?` |
| `promote_lift.sh` | `PROMOTION_LOG.tsv` | `log_promotion()` chamado em 3 pontos (BEGIN/OK/FAIL) | ✓ WIRED | Confirmado no código e no ficheiro real (entradas correspondentes existem) |
| `promote_lift.sh --revert` | hash em `PROMOTION_LOG.tsv` | `awk` procura qualquer linha `PROMOTE-*` com o sufixo | ✓ WIRED | Código lido; `cmd_revert()` usa exatamente este padrão |
| produção (`recomp_macos_v2`) | candidato (`recomp_macos_v3`) | md5 por ficheiro `.cpp` (7 chunks) | ✓ VERIFIED (Level 4) | **Recomputado por mim, independentemente**: os 7 md5 batem exatamente |

### Data-Flow Trace (Level 4)

O ponto mais crítico da fase — "produção deriva mesmo do candidato aceite" — foi
verificado por medição direta, não por inferência do `rc=0`: recomputei o `md5` dos 7
`ppu_recomp_NNN.cpp` de `recomp_macos_v2` (produção atual) e de `recomp_macos_v3`
(candidato, ainda presente no disco) e todos os 7 pares batem exatamente.

### Behavioral Spot-Checks / Probe Execution

- `ctest --test-dir build-tests` corrido nesta verificação: **22/22 passed** (2 `py.*`
  skipped). Fixtures regeneradas em `runtime/spu/tests/out_*/spu_recomp.{c,h}`
  restauradas com `git checkout --` logo a seguir — não ficou resíduo desta verificação.
- Não corri `smoke_relift_equiv.sh`, `accept_relift.sh` nem `promote_lift.sh` nesta
  verificação — instrução explícita de não tocar em produção/promoções.
- Cruzamento `git log` vs commits citados nos SUMMARYs (motivado pelo achado da Fase 3):
  nenhum commit órfão encontrado para a Fase 5, em nenhum dos dois repositórios.

### Requirements Coverage

| Requisito | Fase | Descrição | Status | Evidência |
|---|---|---|---|---|
| `REQ-RDY0-6` | 5 | Smoke de equivalência, promoção e árvore limpa | ✓ SATISFIED | Todos os 5 critérios verificados acima; `REQUIREMENTS.md:520` marca `Completo (2026-07-29)` |

**Contexto cruzado (dado novo, verificação paralela da Fase 3 — não é gap da Fase 5, mas condiciona a leitura do marco):**
`REQUIREMENTS.md` confirma, lido diretamente: `REQ-RDY0-4` (Fase 3) está `[OPEN]`, com
`verify_lift_baseline.sh recomp_macos_v3` a devolver **rc=1** — 243/246 marcadores
intactos, mas o par acoplado `GRUPO:opd-dispatch` com deficit residual de 36. O texto do
próprio requisito diz, sem maquiagem: "por isso este requisito permanece `[OPEN]`, não
marcado completo". `REQ-RDY0-2` (Fase 2) está `Parcial` (3/4 critérios). O coordenador
reporta ainda que o verificador da Fase 3 encontrou o gap D-3.6 (herdado da Fase 2)
**totalmente fechado** por um commit (`6b50c4c`) não citado em nenhum SUMMARY da Fase 3.
Nenhum destes três factos é regressão ou gap da Fase 5; são achados de fases anteriores
já declarados nos seus próprios artefactos (ou pelo verificador irmão da Fase 3) — mas
são material direto para a "opinião franca" pedida sobre o fecho do marco, abaixo.

### Anti-Patterns Found

Nenhum. `grep -E "TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER"` sobre os 5 scripts/artefactos
criados nesta fase devolveu vazio.

### Human Verification Required

Nenhum item. Todas as truths são verificáveis por código/registo/medição direta.

### Gaps Summary

Nenhum gap encontrado no âmbito do REQ-RDY0-6 / Fase 5. Os 5 critérios do ROADMAP estão
satisfeitos com evidência reproduzida independentemente: código do gate lido e a ordem
gate-antes-de-tocar confirmada estruturalmente linha a linha; `PROMOTION_LOG.tsv` real
lido do disco; `.gitignore` e `git status` medidos nos dois repositórios; os 7 chunks de
produção recomputados por md5 e comparados ao candidato; `ppu_loader.cpp:780-784`
conferido linha a linha; `ctest` corrido com resultado 22/22; cruzamento `git log`×
SUMMARY sem commits órfãos.

## Opinião franca sobre o fecho do ciclo (pergunta prioritária do coordenador — não verificável por comando)

**A pergunta na forma exata que interessa:** o ciclo está fechado porque algo o EXIGE
estruturalmente, ou continua a depender de alguém se lembrar de correr as coisas pela
ordem certa? A resposta divide-se em duas partes, porque a garantia é diferente em cada
uma.

**Para o PASSO "promoção" isoladamente: já não depende de memória, depende de código.**
`promote_lift.sh` não pede licença nem confia em disciplina para correr o gate — corre-o
incondicionalmente antes de qualquer `mv`/`cp` sobre produção, e aborta (`exit 1`) sem
tocar em nada se `accept_relift.sh` não devolver `rc=0`. É a diferença estrutural real
face a 26/jul: naquele dia, "rodar o gate primeiro" era um passo do protocolo que
alguém tinha de lembrar-se de seguir; hoje, `promote_lift.sh recomp_macos_v3 --yes` não
tem um caminho de código que promova sem gate. Verifiquei isto na fonte (linhas
202-244), não aceitei a alegação — exatamente porque o coordenador tinha razão em pedir
que esta pergunta fosse respondida pelo código, não pelo SUMMARY: foi uma interrupção
REAL, não uma revisão preventiva, que expôs o defeito anterior (o log só era escrito no
fim), e o padrão de "só descobrimos porque algo real falhou" é o mesmo padrão do achado
da Fase 3 (D-3.6 fechado por um commit que nenhum SUMMARY citou) — nos dois casos, a
narrativa escrita por si só não bastava para confiar; foi preciso ir ao artefacto real.

**Mas isso só cobre o caso de alguém usar `promote_lift.sh`.** Nada no repositório
impede um operador de contornar o script inteiramente — copiar `recomp_macos_v3` para
cima de `recomp_macos_v2` manualmente com `cp -R`, ou editar um chunk direto em
produção. Não há hook de pre-commit, nem permissão de sistema de ficheiros, nem
qualquer mecanismo fora do próprio script que torne isso impossível. A garantia
estrutural que existe é "SE usares o caminho oficial, o gate corre sempre" — não "É
impossível promover sem gate". Isto é a mesma classe de dependência que falhou em
26/jul (faltava um sistema que EXIGISSE o gate, não faltava conhecimento de que devia
correr) — só que reduzida a um perímetro mais estreito: já não é preciso lembrar-se de
RODAR o gate manualmente antes de copiar (isso está automatizado dentro do script), mas
ainda é preciso lembrar-se de USAR o script em vez de tocar diretamente nos diretórios
de produção. Não considero isto uma falha desta fase — é uma melhoria real e mensurável
do raio da dependência humana, não a eliminação dela.

**Para a garantia de "aceite ou rejeitado por medição repetida" (a outra metade do
objetivo): o ciclo fecha a lição específica aprendida, não toda a classe de perda
possível.** O próprio `ACCEPTANCE-REPORT.md` admite (limite 4) que a perna 2
(`verify_lift.sh`) mede sinais estáticos — contagens de conversão, marcadores TAG/GRUPO
— não pixels nem o registry de shaders em runtime. Teria apanhado a perda específica de
36 conversões OPD de 26/jul (provado por injeção real). Não está provado que apanharia
uma perda funcional de natureza diferente que não se manifeste nesses sinais estáticos
específicos.

**Terceiro ponto, motivado diretamente pelo achado da Fase 3:** a garantia de que "o
trabalho está onde o registo diz que está" não é absoluta neste projeto — já falhou uma
vez nesta mesma sessão de trabalho, na fase imediatamente anterior (D-3.6 fechado por
`6b50c4c`, não citado em nenhum SUMMARY da Fase 3). Verifiquei especificamente que isso
não aconteceu na Fase 5 (cruzamento `git log`×SUMMARY, sem órfãos), mas não posso
generalizar essa ausência para "o processo de documentação deste projeto é fiável" — só
posso dizer "nesta fase, desta vez, medi e não encontrei o mesmo defeito". É uma
diferença importante: a Fase 5 passa porque a medi e a evidência aguenta, não porque
confio que o processo do projeto garanta isso sempre.

**Conclusão honesta:** o mecanismo de gate + promoção escrita e reversível é uma
entrega sólida e estruturalmente verificada — reduziu a dependência de memória humana a
um perímetro muito mais estreito do que existia em 26/jul (de "lembrar de rodar o gate
manualmente" para "lembrar de usar o script correto em vez de um atalho"), e essa
redução é real, não cosmética, porque está no código, não numa checklist. Mas
"genuinamente fechado" no sentido absoluto que o coordenador pergunta — sem NENHUMA
dependência de alguém fazer a coisa certa — não é uma descrição precisa: ainda depende
de (a) usar `promote_lift.sh` em vez de um atalho manual, sem nenhuma barreira do
sistema que force isso, e (b) aceitar que "passar no gate" significa "sem a classe de
regressão já vivida (perda de conversões OPD)", não "sem qualquer regressão possível".
A equipa já declara ambos os limites por escrito nos seus próprios artefactos — a minha
adição, como verificador, é recusar arredondar isso para "fechado", porque a pergunta
pedia precisão, não conforto, e porque o próprio precedente da Fase 3 (trabalho real
sem registo correspondente) é um lembrete de que confiar cegamente na narrativa escrita
foi exatamente o tipo de falha que este marco existe para eliminar.

---

*Verified: 2026-07-29*
*Verifier: Claude (gsd-verifier)*
