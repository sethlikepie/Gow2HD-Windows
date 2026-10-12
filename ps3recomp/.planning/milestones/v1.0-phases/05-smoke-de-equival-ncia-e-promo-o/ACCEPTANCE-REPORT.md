# Relatório de Aceite — Fase 5 / Marco 1 (RDY-0), D-5.4

**Requisito:** `REQ-RDY0-6` — Smoke de equivalência, promoção e árvore limpa
**Escrito em:** 2026-07-29, após a promoção real e final executada no Plano 05-05 Task 1
**Autor:** executor autónomo do Plano 05-05

---

## Resumo

`recomp_macos_v3` (candidato re-liftado da Fase 3, com os fixes de patch da Fase 4)
foi promovido a produção **real e permanente** em 2026-07-29T19:06:49Z–19:09:47Z, via
`./promote_lift.sh recomp_macos_v3 --yes` — aceite de três pernas (D-5.1) PASS, backup
por rename, rebuild, confirmação pós-build 6/6, `rc=0` de ponta a ponta. Não houve
reversão a seguir: esta é a promoção definitiva do marco.

- **Suffix da promoção:** `20260729_150649`
- **`PROMOTION_LOG.tsv`:** `PROMOTE-BEGIN` (19:06:49Z) → `PROMOTE-OK` (19:09:47Z), ambos
  com `hash_v2_antes=7bf905232c20e9ab705c1321482f4f6c` / `hash_boot_antes=346aa5ae178225838f07e58638fed878`.
- **Backup do estado anterior preservado:** `recomp_macos_v2.pre_20260729_150649/` /
  `boot_gow2.pre_20260729_150649` (não apagados). O histórico da promoção manual de
  26/jul continua intacto em `recomp_macos_v2.pre_v4/` / `boot_gow2.pre_v4`.
- **Produção depois da promoção:** `boot_gow2` = 120.635.880 bytes, mtime 29 jul 15:07;
  `recomp_macos_v2/ppu_recomp_*.cpp` (7 chunks) confirmados **byte-idênticos** ao
  candidato `recomp_macos_v3` por `md5` de cada um dos 7 ficheiros — não é uma inferência
  a partir do `rc=0` do script: os 7 pares de hash foram comparados diretamente e batem.
  (Nota sobre `hash_dir()`: uma comparação de diretório inteiro deu valores diferentes
  entre `recomp_macos_v2` pós-promoção e `recomp_macos_v3` candidato — **não é** conteúdo
  divergente, é a instabilidade de `hash_dir()`/`tar` sobre ficheiros `.o`/`.cclog`
  recém-gerados com mtimes diferentes, já documentada como dívida conhecida, não
  bloqueante, no `05-04-SUMMARY.md`. A comparação por ficheiro `.cpp` fonte, que é o que
  importa para "produção deriva de v3", é a prova autoritativa aqui.)

---

## Critério 1 (smoke) — `st620 >= 3` em `>= 4 de 6`

Resultado real da corrida de aceite que precedeu a promoção (`accept_relift.sh
recomp_macos_v3` dentro do `promote_lift.sh`, 2026-07-29T19:06–19:07Z):

| run | st620_max | log_lines | alloc | class |
|---|---|---|---|---|
| 1–6 | 11 (todas) | 3517–3880 | 0 | **OK** (todas as 6) |

`st620>=3 em 6 de 6 (limiar: 4)`, `orfaos_pos_run: 0`, `rc=0`. Nenhuma corrida caiu em
`FLAKE_SUSPEITA`/`REGRESSAO_SUSPEITA` nesta execução específica — folga confortável
acima do limiar, ao contrário da rehearsal do Plano 05-04 que ficou no limite exato
(`4/6`, 2 `REGRESSAO_SUSPEITA`). A confirmação pós-build (binário `boot_gow2`
reconstruído, não o lift) repetiu 6/6 `OK` com o mesmo padrão (`st620_max=11` em todas).

## Critério 2 (contadores) — REFORMULADO (D-5.2)

`56072`/`56223` citados no ROADMAP.md/REQUIREMENTS.md são do lift **antigo**, escrito à
mão, e não voltam a bater contra qualquer lift regenerado por `tools/ppu_lifter.py`.
Medição real da corrida de aceite (`COUNTERS_BASELINE.tsv`, congelado no Plano 05-03
contra a produção pré-Fase-5):

| Contador | Produção (antes) | Candidato (`v3`) | Delta | Classe |
|---|---|---|---|---|
| `function_table_count` | 51917 | 51917 | 0 | INFORMATIVO |
| `boot_lifted_functions` | 52068 | 52068 | 0 | INFORMATIVO |
| `imp_modules` | 13 | 13 | 0 | **BLOQUEANTE** |
| `imp_imports` | 151 | 151 | 0 | **BLOQUEANTE** |
| `orfaos` | 0 | 0 | 0 | **BLOQUEANTE** |

Os três bloqueantes (`imp_modules`/`imp_imports`/`orfaos`) batem exatamente, absolutos,
como o ROADMAP exige. Os dois informativos batem por delta zero contra a produção
imediatamente anterior — não contra os absolutos desatualizados do lift escrito à mão.
A diferença entre os absolutos antigos (56072/56223) e os do lift regenerado (51917/52068)
já foi investigada e é benigna (Plano 05-CONTEXT/03-03): das ~4200 funções a menos, 1919
caem fora de qualquer secção executável (o lift antigo desassemblava dados); zero starts
do `functions.json` faltam; zero EAs da `.opd` faltam.

## Critério 3 (protocolo G6)

Confirmado em todas as execuções desta fase (Planos 05-01 a 05-05, incluindo a promoção
final desta task): `PS3_NO_RSX=1`, `PS3_RSX_BACKEND=trace`, `PS3_PERF_FSM=1`,
`PS3_ENGINE_ROOT` explícito (herdado por `promote_lift.sh`/`accept_relift.sh` via
`PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"`), kill por PID
(`Terminated: 15` visível no log de cada corrida — `TERM` seguido de `-9` interno ao
`smoke_relift_equiv.sh`), nunca `pkill -f boot_gow2`. Logs de confirmação da promoção
final em `/tmp/promote_confirm_20260729_150649.tsv` e `/tmp/promote_final_run1.log`,
fora dos dois repositórios. `pgrep -fl boot_gow2` confirmado vazio após a promoção —
sem processos órfãos.

## Critério 4 (git status + promoção) — REFORMULADO

**A correção do `.gitignore` (Task 1 desta plan):** medido no início desta sessão que
`recomp_macos_v3`, `boot_gow2_v3`, `boot_gow2_v3fix`, `boot_gow2_v4`, `boot_gow2.pre_v3`,
`boot_gow2.pre_v4`, `boot_gow2.wip`, `boot_gow2_O1`, `boot_gow2_pre`,
`boot_gow2_relift_test` já apareciam como `??` (untracked) em `gow2-recomp` **antes**
desta task — o `.gitignore` só cobria `recomp_macos_v2/` e `boot_gow2` exatos, não os
nomes com sufixo de versão que a convenção da Fase 3 introduziu (nem os `.pre_*` que o
próprio `promote_lift.sh` cria a cada promoção). Sem correção, o critério 4 era
estruturalmente inatingível: **cada promoção real deixaria automaticamente para trás um
diretório/binário `.pre_<SUFFIX>` novo e sempre untracked**, mesmo que nada mais na
árvore estivesse sujo.

Corrigido acrescentando duas linhas ao `.gitignore` (sem remover nenhuma existente):
`recomp_macos_v*/` e `boot_gow2*`. Confirmado por medição: `git status --porcelain |
grep -E 'recomp_macos_v3|boot_gow2_v3|...'` deu vazio depois da edição. Commit
`721f85a` (`gow2-recomp`, `fix(.gitignore): cobre variantes com sufixo de
recomp_macos_v*/boot_gow2*`).

**O procedimento de promoção está escrito** (`promote_lift.sh`, Plano 05-04) e foi
executado **duas vezes** nesta fase:
1. Uma **rehearsal reversível** (Plano 05-04): promoveu `recomp_macos_v3`, confirmou
   por hash, reverteu — produção byte-idêntica ao estado de antes.
2. Uma **promoção final e permanente** (esta task, Plano 05-05): promoveu
   `recomp_macos_v3` a produção, **sem reversão a seguir**. `rc=0`.

**`git status --porcelain` em `ps3recomp` (raiz do monorepo):** limpo exceto
`build-tests/` (untracked) e `runtime/spu/tests/out_spu_channel_blocking/` (untracked) —
ambos artefactos de teste pré-existentes, irrelevantes a este plano, e um conjunto de
ficheiros `M` sob `runtime/spu/tests/out_*/spu_recomp.{c,h}` (regenerados por corridas de
`ctest` de sessões anteriores) — nenhum destes é tocado nem commitado por esta fase
(CLAUDE.md regra 7).

**`git status --porcelain` em `gow2-recomp` após o commit da Task 1** — os únicos itens
remanescentes são exatamente a dívida de OUTRA sessão, medida no início desta fase e
deixada intocada por CLAUDE.md regra 7:

Modificados (3):
- `notes/2026-07-23-gate-a-verdict.md`
- `recomp_mid_v2/gow2_spu_register.c`
- `recomp_mid_v2/patch_postthr_pc_probe.py`

Não rastreados (todos pré-existentes ao início desta fase, confirmados por medição
antes de qualquer edição desta task):
- `.DS_Store`
- `config/`
- 12 ficheiros `notes/2026-07-22-*.md` e `notes/2026-07-23-*.md` (investigações de sessão
  anterior sobre a parede Type15/WALL-D)
- `recomp_mid_v2/__pycache__/`
- `recomp_mid_v2/wad_tex__00ccomicsmoke_pal_c.png`, `recomp_mid_v2/wad_tex__010decorchest01_gol.png`
- `spu_lifted/.DS_Store`, `spu_lifted/spu4_v2/`, `spu_lifted/spu5_v2/`

**Nenhum destes é tocado nem commitado por esta fase.** Nenhum ficheiro desta fase
(`.gitignore`, `PROMOTION_LOG.tsv`, `promote_lift.sh`, `accept_relift.sh`,
`smoke_relift_equiv.sh`, `manifest_delta_gate.py`, `MANIFEST_DEBT_BASELINE.json`,
`COUNTERS_BASELINE.tsv`) aparece como não commitado depois do commit `721f85a`.

## Critério 5 (limites declarados, D-5.4) — sem maquiagem

1. **`R_PermA` está fora do aceite desta fase.** Declaração original do plano RDY-0: o
   Marco 1 entrega o ciclo re-lift→prova→promoção, não o desbloqueio de `R_PermA`. Nada
   nesta fase exercitou esse caminho.

2. **O caminho de present não é coberto pela receita headless.** Todas as execuções
   desta fase correm com `PS3_NO_RSX=1`, que desativa o backend gráfico inteiramente. Um
   re-lift que partisse o flip/apresentação **passaria** neste gate sem ser detectado —
   o smoke mede progressão de FSM (`st620`), não pixels nem frames.

3. **Gate A continua RED e isso é o valor ESPERADO, não uma falha.** Confirmado em
   `runtime/ppu/ppu_loader.cpp:780-784`: o comentário documenta explicitamente que
   "post-WAD the main thread wedges in func_000CC9D0 spinning on the two TYPE15 shell
   objects... never climbing back to re-arm the flip (Gate A RED,
   SetFlip_after_R_Perm=0)". `.planning/ROADMAP.md:26` declara por escrito: "O que o
   Marco 1 **não** entrega... Gate A continua RED como valor esperado." Nenhuma corrida
   desta fase tentou nem esperou fechar Gate A — seria um objetivo do Marco 9
   (`REQ-gate-a-post-thr-life`), fora do escopo do RDY-0.

4. **O smoke mede só a intro até `st620=11` — não cobre WADLD nem o caminho de
   registo de shaders.** Este é o limite descoberto nesta própria sessão de planeamento
   (05-CONTEXT.md), e a razão de existir a perna 2 (`verify_lift.sh`) além do smoke: em
   2026-07-26, um lift regenerado passou o smoke M0 (`st620=11` em 6/6, acima do
   critério 1) e foi promovido a produção com base só nisso — e faltavam-lhe **36
   conversões OPD** (`ps3_call_opd` de 47 para 11), **35 guardas** `0x4F000000u` e **9
   famílias de marcador**. O caminho que alimenta o registry de shaders tinha perdido as
   conversões, e o smoke não piscou, porque o gate mede a intro até `st620=11`, e a
   perda manifesta-se depois — no WADLD e no caminho de shaders. `st620=11 em 6/6`
   prova equivalência NA INTRO, não equivalência funcional. **A perna 2 (`verify_lift.sh`
   / `manifest_delta_gate.py`, Plano 05-02) é o mecanismo que teria apanhado essa perda**
   — mas mesmo ela não prova pixels reais de shader, só ausência de regressão nos sinais
   estáticos medidos (contagem de conversões OPD, marcadores TAG/GRUPO). Nenhum gate
   desta fase exercita o registry de shaders em runtime.

5. **42 dos 64 patches `FUNCIONAL` continuam `UNVERIFIED`, sem contrato — a perna 3 do
   aceite não os verifica.** `CONTRACTS.tsv` (Plano 04-04) cobre 22 dos 64 patches
   classificados `FUNCIONAL` em `PATCH_CATALOG.tsv`. A corrida real de aceite desta fase
   confirmou: `apply_all_patches.sh --status` → `TOTAL: 87 patches | ALREADY-APPLIED=22
   NO-MATCH=0 UNVERIFIED=62 FAILED=3`, os 3 `FAILED` todos classe `PROBE`. A perna 3 do
   aceite (D-5.1) exige apenas zero `NO-MATCH` e zero `FAILED` fora de `PROBE` — os 62
   `UNVERIFIED` (que incluem os 42 `FUNCIONAL` sem contrato mais os `PROBE`) passam sem
   verificação de pós-condição. Um patch `FUNCIONAL` sem contrato que deixasse de ter
   efeito silenciosamente **não seria apanhado** por nenhuma das três pernas, a menos
   que também quebrasse o smoke ou o `lift_parity`/`MANIFEST`.

### O que o aceite GARANTE hoje, e não garantia antes desta fase

As três pernas foram provadas a **rejeitar** de verdade, uma a uma, em cópias scratch
do lift real (nunca no `recomp_macos_v3` real — hash MD5 do corpus confirmado intacto
antes/depois de cada prova, Plano 05-03):

- **Perna 1 (smoke) rejeita build quebrado**: `#error` injetado num chunk → `PERNA 1:
  OK=0 de 1, rc=1` → `accept_relift.sh` sai `rc=1`.
- **Perna 2 (`verify_lift.sh`) rejeita regressão de conversão OPD real**: removida 1
  chamada `ps3_call_opd` → `lift_parity FAIL` (`NOVO 0x002B0AA8/CALL_MISMATCH`) →
  `accept_relift.sh` sai `rc=1`. Esta é exatamente a classe de regressão que escapou em
  2026-07-26 — agora tem um gate dedicado que a apanha.
- **Perna 3 (`apply_all_patches.sh --status`) rejeita pós-condição de patch quebrada**:
  removidas as 2 chamadas exigidas pelo contrato `opd-14a01c-a`/`opd-14a01c-b` →
  `NO-MATCH=1` → `accept_relift.sh` sai `rc=1`.

E o mecanismo de promoção (`promote_lift.sh`, Plano 05-04) garante, também provado:
nenhuma promoção toca em produção sem o gate das três pernas passar primeiro; o backup
do estado anterior nunca é apagado (só renomeado); uma interrupção externa a meio (um
kill -9, testado tanto por acidente real quanto por sandbox sintético) deixa rasto em
`PROMOTION_LOG.tsv` (`PROMOTE-BEGIN` escrito antes de qualquer `mv`/`cp`) e é
recuperável por hash via `--revert`.

## Dívida conhecida que continua aberta

- **42 dos 64 patches `FUNCIONAL` continuam `UNVERIFIED`** em `CONTRACTS.tsv` — cobertura
  parcial (22/64), Plano 04-04. Não fechado por esta fase.
- **27 `RED` do `lift_parity`** (9 `TRUNCATED`, 18 `CALL_MISMATCH`) continuam no baseline
  aceite — congelados, não investigados individualmente.
- **O déficit de 36 do grupo `opd-dispatch`** (`MANIFEST_DEBT_BASELINE.json`, Plano 05-02)
  fica registado como dívida CONHECIDA e congelada, não como problema resolvido — uma
  promoção futura que o AGRAVE será apanhada pela perna 2, mas o déficit em si continua
  aberto.
- **O smoke não cobre WADLD nem o caminho de shaders** (critério 5, item 4 acima) — não
  há, hoje, gate algum desta fase que exercite o registry de shaders em runtime.
- **`hash_dir()` (copiada de `test_relift_build.sh`) mostra instabilidade** entre a
  primeira leitura de ficheiros recém-copiados/gerados e leituras seguintes (observado
  em sandbox no Plano 05-04, reconfirmado nesta task ao comparar `recomp_macos_v2`
  pós-promoção vs `recomp_macos_v3` candidato — hashes de diretório diferentes apesar
  do conteúdo `.cpp` fonte ser byte-idêntico ficheiro-a-ficheiro). Suspeita: metadata do
  macOS (xattrs/Spotlight) capturada pelo `tar`. Não investigado a fundo — não bloqueou
  nenhuma prova desta fase porque todas as comparações usaram ou ficheiros de produção
  estáveis há dias, ou comparação direta por ficheiro fonte em vez de hash de diretório
  inteiro.

## Comando de reversão

Se uma regressão for descoberta depois desta promoção:

```bash
cd ../gow2-recomp
./promote_lift.sh --revert 20260729_150649
```

Restaura `recomp_macos_v2`/`boot_gow2` ao estado anterior (`hash_v2_antes` =
`7bf905232c20e9ab705c1321482f4f6c`, `hash_boot_antes` =
`346aa5ae178225838f07e58638fed878`), preservando o estado substituído sob
`.reverted_<timestamp>`, e confirma por hash — não por confiança.

---

## Conclusão: `REQ-RDY0-6` e o Marco 1 (RDY-0)

Os 5 critérios do ROADMAP para a Fase 5 estão satisfeitos, com as reformulações D-5.2/D-5.4
explicadas e medidas, não assumidas:

1. Smoke: `rc=0`, `st620>=3` em 6 de 6 (folga acima do limiar `>=4`).
2. Contadores: bloqueantes (`imp_modules`/`imp_imports`/`orfaos`) absolutos e exatos;
   informativos por delta zero contra baseline real.
3. Protocolo G6: confirmado em todas as execuções.
4. `git status`: limpo no escopo desta fase, nos dois repositórios; `.gitignore`
   corrigido; procedimento de promoção escrito, provado reversível, e executado de
   forma real e permanente.
5. Limites declarados sem maquiagem: os cinco itens acima, incluindo o descoberto nesta
   sessão (smoke não cobre WADLD/shaders).

`REQ-RDY0-6` pode ser marcado **`[DONE]`**. O Marco 1 (RDY-0) fecha com o ciclo
"re-lift → prova → promoção" consumado de facto: `recomp_macos_v3` é agora a produção
real, e o mecanismo (gate de três pernas + promoção escrita e reversível) existe para
qualquer re-lift futuro usar.

**O que fica honestamente por fechar, fora do escopo declarado do RDY-0:** `R_PermA`,
o caminho de present, Gate A (RED por design deste marco), o registry de shaders em
runtime, os 42 patches `FUNCIONAL` sem contrato, e os 27 `RED` do `lift_parity` no
baseline aceite. Nenhum destes é uma regressão desta fase — são a fronteira declarada do
que o Marco 1 se propôs a entregar, e o próximo trabalho (Marco 9 — GoW2 até o menu)
começa exatamente onde esta fronteira termina.
