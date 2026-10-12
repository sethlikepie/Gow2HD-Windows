# Fase 8, Plano 08-03 — Reposição final

## Ramo: `REFUTADA-AMBAS` (per `notes/2026-07-30-fase8-plano02-veredicto.md`)

O Plano 08-02 refutou os dois candidatos desta fase (patches e o runtime
`f6708cb`) por medição real do binário. **Nenhuma correcção é aplicada nesta
rodada** — nenhum ficheiro-fonte (`patch_*.py`, `runtime/ppu/ppu_loader.cpp`)
foi editado no âmbito desta tarefa. Per o próprio plano: reconfirmar a
rejeição actual e entregar o estado final honesto, com o funil completo.

## REG-03: NÃO cumprido nesta fase

`./accept_relift.sh recomp_macos_v3 6` (relift completo do zero,
`FORCE_REBUILD_LIFT=1` via `smoke_relift_equiv.sh`, PERNA 1-4 + CONTADORES):

```
PERNA 1 (smoke)          PASS   (OK=6/6)
PERNA 2 (verify_lift)    PASS   (rc=0)
PERNA 3 (apply_patches)  PASS   (NO-MATCH=0 FAILED-fora-PROBE=0, UNVERIFIED=55 informativo)
PERNA 4 (chain gate)     FAIL   (OK=0/6, elo_stopped mais frequente: 2o movie (StartSeq))
CONTADORES (D-5.2)       FAIL   (imp_modules/imp_imports/orfaos bloqueantes)

REJEITADO: rc=1
```

Medido directamente no TSV da PERNA 4 (`/tmp/accept_recomp_macos_v3_chain.tsv`,
o gate da Fase 7 que decide REG-03, não o `st620`):

| run | st620 | startseq | thr_end | r_perma | elo_stopped | class |
|---|---|---|---|---|---|---|
| 1-6 | 11 | 1 | 0 | 0 | 2o movie (StartSeq) | REGRESSAO |

`elo_stopped=nenhum em 0 de 6` (limiar: 4). **StartSeq nunca chega a 2,
`thr_auto_load_end` fica em 0, `R_PermA` fica em 0** — a mesma barreira exacta
de toda a investigação (Fase 6, Fase 8), reconfirmada num relift completo do
zero desta sessão.

**REG-03 NÃO é cumprido nesta fase.** A causa continua não corrigida — porque
continua não confirmada. Nota lateral, fora do escopo desta investigação: os
CONTADORES (D-5.2) também reprovam por conta própria (`orfaos` candidato=2 vs
produção=0) — um achado pré-existente do processo de aceite, não relacionado
com a regressão do `2o movie/StartSeq`.

## Funil COMPLETO de causas eliminadas (Fase 6 + Fase 8)

| # | Candidato | Fase | Método | Resultado |
|---|---|---|---|---|
| 1 | `truncated-bounds repair` | 6 | A/B de contagem | REFUTADO — adiciona 78 funções, não remove; nem existia no lift `v3` (gerado antes do commit) |
| 2 | `invalid_instructions` (config do lifter) | 6 | leitura do commit + `build_macos.sh:77` | REFUTADO — no-op, lift sem `--config` |
| 3 | Sub-emissão da cadeia `CD7B4` | 6 | contagem de chamadas | REFUTADO — 19 chamadas no lift que falha vs 8 no que funciona (sobre-emitida, não sub-emitida) |
| 4 | TOCFIX / `ctx->lr` (`func_00147038`) | 6 | reversão real + rebuild | REFUTADA-EM-COMBINAÇÃO — produz a mesma REGRESSAO |
| 5 | `strip_block` | 6 | ver `2026-07-30-fase6-veredictos.md` | REFUTADA |
| 6 | Regressão comportamental do lifter perto de `thr_auto_load` | 6 | comparação byte-a-byte de 3 níveis | MEDIDA-MAS-NÃO-CONFIRMADA — contagem cresce monotonicamente, vizinhança idêntica entre binário OK e binário que falha |
| 7 | **Patches** (diff hoje vs `e6d65a2`) | 8 (08-01/08-02) | A/B de diff + medição de binário | **REFUTADA** — `hoje` completo falha como a produção; `e6d65a2` completo nem compila (deriva de nomes não relacionada com o timing); nenhum patch toca `func_00147038` em nenhuma das eras |
| 8 | **Runtime `f6708cb`** (`ppu_register_committed_range`/`vm_uncommitted`) | 8 (08-02) | reversão cirúrgica isolada + medição | **REFUTADO** — reversão não repõe o boot |
| 9 | Rejeição actual (relift completo do zero, `recomp_macos_v3`) | 8 (08-03, esta nota) | `accept_relift.sh` PERNA 4 | **RECONFIRMADA** — `REGRESSAO` idêntica, `elo_stopped='2o movie (StartSeq)'` em 6/6 |

## REG-04: não aplicável — nenhuma causa foi corrigida nesta fase

**REG-04 não aplicável — nenhuma causa foi corrigida nesta fase.** O Plano
08-02 refutou os dois candidatos testados (patches, runtime `f6708cb`); esta
tarefa não teve nenhuma causa nova para corrigir, logo não há re-lift a
provar sobrevivência de correcção nenhuma. O estado fica para retomar na
próxima sessão, com o próximo passo já escrito acima (bissecção automatizada
de `runtime/`+`libs/` na janela `8648805..4e815cf`).

Como nenhuma correcção foi aplicada, `accept_relift.sh` **não muda de
veredicto** — é a MESMA ferramenta, o MESMO resultado, ANTES e DEPOIS desta
fase:

| Momento | `accept_relift.sh recomp_macos_v3 6` | PERNA 4 (GATE-03) |
|---|---|---|
| Antes da Fase 8 (07-02-SUMMARY.md, 2026-07-30) | REJEITADO, rc=1 | elo_stopped="2o movie (StartSeq)" |
| Depois da Fase 8 (esta nota, 2026-07-31) | REJEITADO, rc=1 | elo_stopped="2o movie (StartSeq)" |

Isto é o comportamento CORRECTO e esperado quando `REFUTADA-AMBAS`: o gate da
Fase 7 continua a fazer exactamente o que foi desenhado para fazer — rejeitar
um binário que não chega ao `thr_auto_load`/`R_PermA` — porque a causa real
ainda não foi corrigida, não porque o gate tenha regredido.

## Próximo passo recomendado (herdado do Plano 08-02, reconfirmado aqui)

Nenhum dos 9 candidatos acima nomeia a causa. A investigação eliminou o
lifter (3 hipóteses), a região do código liftado perto de `thr_auto_load` (2
hipóteses), os patches (diff + binário), e os dois únicos commits que tocam
`runtime/ppu/ppu_loader.cpp` na janela exacta da regressão. **O resto de
`runtime/`+`libs/` na mesma janela nunca foi eliminado.**

Próximo passo preciso, pronto para uma sessão futura: bissecção automatizada
commit a commit da janela `8648805`→`4e815cf` (25 Jul 17:53 → 26 Jul 02:59)
sobre TODOS os ficheiros de `runtime/`+`libs/` que mudaram nesse intervalo
(não só `ppu_loader.cpp`), usando `bisect_regression.sh --bin` como oráculo e
`git worktree` isolado por candidato — o mesmo padrão já provado nesta fase
(rebuild só da lib + relink puro, ~1 minuto por candidato testado, muito
abaixo do custo de um relift completo). Primeiro passo dessa sessão:
`git log --oneline --since="2026-07-25 17:53" --until="2026-07-26 03:00" --
runtime/ libs/` para enumerar todos os candidatos da janela (não só os dois já
eliminados aqui).
