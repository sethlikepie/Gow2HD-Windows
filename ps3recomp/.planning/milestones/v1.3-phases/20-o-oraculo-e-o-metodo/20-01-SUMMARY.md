---
phase: 20-o-oraculo-e-o-metodo
plan: 01
subsystem: lift-verification
tags: [ghidra, oraculo, proveniencia, verify_lift, baseline-delta, i4-i5, gow2, ppu-lift]

requires:
  - phase: 16-invent-rio-de-patches-e-baseline-medido
    provides: "verify_lift.sh com os 3 passos por DELTA, baseline_delta.py e o vocabulário de níveis de evidência"
  - phase: 03-lifter-coverage-contract
    provides: "tools/audit_boundaries.py com I1–I5 e --oracle (o motor de achados que este plano passa a gatear)"
provides:
  - "games/gow2/verify_lift.sh — 4.º passo `oracle_ghidra`, OPT-IN por VERIFY_ORACLE=1, por delta"
  - "tools/oracle_manifest.py — write/check/ids: proveniência do ghidra_out e ids I4/I5 opacos"
  - "games/gow2/lift_baseline/ORACLE_BASELINE.json — 2673 ids MEDIDOS no corpus real do GoW2"
  - "ghidra_out/MANIFEST.json — sha256 do ELF, data do export, versão do Ghidra e 5 contagens"
  - "prova por mutação no corpus REAL: truncar 8 bytes numa função → rc=1 citando o id novo"
affects: [fase-21-antes-depois, LEDG-02, RE-03-mcp-paridade, accept_relift]

tech-stack:
  added: []
  patterns:
    - "Proveniência como pré-condição do oráculo: ausente/obsoleto vira rc≠0, nunca silêncio (a classe de silêncio que deixou passar o func_0037C384)"
    - "Gate por DELTA contra baseline congelado, nunca por limiar absoluto — 5154 I5 históricos fariam um gate por contagem nascer vermelho e ser ignorado"
    - "exported_at do mtime dos JSON vs stamped_at de agora: um export antigo carimbado hoje não pode mentir sobre a própria idade"
    - "Prova por mutação sempre numa CÓPIA, com sha256 de produção antes/depois como testemunha da reversão"

key-files:
  created:
    - games/gow2/lift_baseline/ORACLE_BASELINE.json
    - games/gow2/lift_baseline/test_oracle_step.py
    - tools/oracle_manifest.py
    - tools/test_oracle_manifest.py
  modified:
    - games/gow2/verify_lift.sh
    - games/gow2/analyze_eboot_ghidra.sh
    - tools/ghidra_analyze.py
    - tools/audit_boundaries.py

key-decisions:
  - "O oráculo entra OPT-IN e não por default: o verify_lift.sh está no caminho de aceitação (accept_relift.sh, perna 2) e mudar o default alteraria o que o projecto aceita hoje, além de passar a exigir Ghidra instalado a quem só quer re-liftar"
  - "I5 `extende-funde` fica FORA do conjunto do gate, mas CONTADO e impresso: a lacuna já é reclamada por outra função nossa e a discordância pode ser legítima. Escotilha declarada em --gate-funde"
  - "Nenhum mecanismo novo de oráculo: reutiliza-se o audit_boundaries.py --oracle que já implementa I4/I5, em vez de um terceiro motor (C9 do plano de adopção)"
  - "exported_at passa a vir do mtime do export e não de datetime.now() — carimbar um dump de 25/jul com a data de hoje seria proveniência a mentir sobre a idade, e a idade é metade do que torna o oráculo confiável"
  - "RE-01 declarado FECHADO neste SUMMARY, mas REQUIREMENTS.md NÃO foi editado — mesma razão do 20-02: evitar escrita concorrente com os outros planos da fase"

metrics:
  duration: "~35 min (21:42–22:16 -0400, incluindo a retoma após expiração de sessão)"
  completed: 2026-08-02
  tasks: 2
  files: 8
  commits: 4

status: complete
---

# Fase 20 Plano 01: O oráculo com proveniência — Summary

O `verify_lift.sh` ganhou um 4.º passo **opt-in** (`VERIFY_ORACLE=1`) que compara as
nossas fronteiras de função com as do Ghidra e falha **só por achado I4/I5 NOVO** contra
um baseline congelado de 2673 ids — e recusa-se a correr sobre um `ghidra_out/` sem
proveniência ou de outro ELF.

---

## O que ficou provado (rc reais, medidos em 2026-08-02)

| # | O que | Comando | rc | Resultado |
|---|---|---|---|---|
| 1 | Default **inalterado** | `PS3_ENGINE_ROOT=$PWD ./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v2` | **0** | 3/3 PASS, `ORACULO: desligado`, sem consultar o Ghidra |
| 2 | Oráculo ligado | `VERIFY_ORACLE=1 …` (idem) | **0** | 4/4 PASS, `novos=0 resolvidos=0` |
| 3 | **Mutação** (o que separa gate real de decorativo) | cópia com `0x00010534` truncada 8 bytes | **1** | `oracle_ghidra FAIL`, cita o id novo |
| 4 | Re-confirmação pós-reversão | como o #2 | **0** | 4/4 PASS, `novos=0` — verde outra vez |
| 5 | MANIFEST **ausente** | `VERIFY_ORACLE=1 PS3_ORACLE_MANIFEST=/tmp/nao-existe…` | **1** | `ORACLE: absent` → `oracle_ghidra FAIL` |
| 6 | MANIFEST **obsoleto** | `VERIFY_ORACLE=1 PS3_ORACLE_MANIFEST=/tmp/MANIFEST_stale.json` | **1** | `ORACLE: stale`, imprime os dois sha → FAIL |

Suites unitárias: `tools/test_oracle_manifest.py` **10/10 [PASS]**,
`games/gow2/lift_baseline/test_oracle_step.py` **5/5 [PASS]**,
`games/gow2/lift_baseline/test_baseline_delta.py` **5/5 [PASS]** (não regrediu).

O critério central do plano — **o default não muda** — está cumprido: sem a variável
o script corre os mesmos 3 passos, com o mesmo rc, e `accept_relift.sh` (perna 2) nunca
define `VERIFY_ORACLE` (verificado por `grep`).

---

## A prova por mutação (o item que o plano marca como obrigatório)

Escolhida a função `0x00010534` porque o nosso lift e o Ghidra **concordam** hoje sobre
ela (fim `0x000105D0`) — de 4451 candidatas nessas condições. Se tivesse escolhido uma
já divergente, o id resultante já estaria no baseline e a mutação passaria despercebida:
o gate é por *id novo*, não por contagem.

```bash
# 1. testemunha ANTES
shasum -a 256 ../gow2-recomp/functions.json
#   90ad64dd9f6b64663a8d3938b65ac5fa2ee648b6153f14ad542375f181d884d7

# 2. CÓPIA mutada (produção nunca é tocada): 0x00010534 end 0x000105D0 -> 0x000105C8
MUT=$(mktemp -d); .venv/bin/python3 -c '...'   # -8 bytes = 2 instruções

# 3. o gate tem de ficar vermelho
VERIFY_ORACLE=1 PS3_FUNCTIONS="$MUT/functions.json" PS3_ENGINE_ROOT=$PWD \
  ./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v2
```

Saída do passo:

```
[oracle] I4=137  I5 extende-seguro=2423  encurta=114  extende-funde=2618 (fora do gate)
[oracle] ids no gate: 2674
[oracle_i4_i5] current=2674 baseline=2673 novos=1 resolvidos=0
[oracle_i4_i5] NOVO (regressao face ao baseline):
  NOVO  I5:functions.json:0x00010534:extende-seguro
[oracle_ghidra] FAIL (rc=1)
```

**rc=1**, com o id exacto nomeado. É a classe do `func_002550C8` (que custou meses) e do
`0x0037C384`: um fim errado faz o emissor trampolinar para o vizinho e saltar código real.

**Reversão confirmada por inspecção, não por fé** (o aviso do 19-01: `git checkout --`
falha em silêncio em ficheiro untracked — e aqui `functions.json` nem sequer é versionado):

1. `rm -rf $MUT` → o directório deixou de existir (testado com `test -e`).
2. sha256 de produção DEPOIS = `90ad64dd…d884d7` — **idêntico** ao de antes.
3. Leitura directa do JSON: `0x00010534 end=0x000105D0`, o valor original.
4. Re-corrida limpa (#4 na tabela): `novos=0`, verde.

Nível de evidência: **offline/tooling** sobre o corpus real (20750 ranges nossos, 13143
funções do Ghidra). **Não** é in-boot — este plano não toca em nada que corra no jogo.

---

## Proveniência: `ghidra_out/MANIFEST.json`

Não existia quando a fase começou. Agora existe e o sha256 **bate certo** com o EBOOT actual:

```json
{
 "counts": { "decompiled": 13096, "functions": 13143, "names": 266,
             "strings": 1962, "symbols": 114065 },
 "elf_sha256": "23cfd435be284adb83745c3e1bcad7b7660782470f71255cb74a1d2be8b1163b",
 "exported_at": "2026-07-25T19:26:53Z",
 "ghidra_version": "12.1.2",
 "ghidra_version_source": "detectada-no-stamp",
 "stamped_at": "2026-08-03T01:46:32Z"
}
```

`shasum -a 256 ../gow2-recomp/EBOOT.ELF` → `23cfd435…1163b`. Confere.

Três decisões de honestidade ficaram no formato:

- **`exported_at` ≠ `stamped_at`.** O export é de 25/jul; o carimbo é de agora. O
  `exported_at` vem do mtime dos JSON, por isso um dump antigo não pode passar por fresco.
- **`ghidra_version_source: "detectada-no-stamp"`** diz que a versão é a do Ghidra
  *instalado hoje*, não necessariamente a que produziu o export de Julho.
- **contagem ausente = `null`, não `0`** (ausente não é vazio).

### O re-carimbo depois do `ghidra_names.py` (a nota do agente anterior)

Estava certa a suspeita: o `ghidra_analyze.py` carimba o MANIFEST no fim do export, mas
isso acontece **antes** de o `ghidra_names.py` produzir o `names.json` — a contagem de
nomes ficaria `null`. O `analyze_eboot_ghidra.sh` passou de 3 para 4 passos e re-carimba
em `3/4`, depois dos nomes. Verificado: **`names=266`**, não `null`.

O re-carimbo é **idempotente** — corrido outra vez contra o mesmo export (escrito para
`/tmp` para não sujar produção), todos os campos coincidem excepto `stamped_at`:

```
[OK ] counts / elf_path / elf_sha256 / exported_at / ghidra_version / ghidra_version_source
```

---

## O baseline congelado (medido, não inventado)

`games/gow2/lift_baseline/ORACLE_BASELINE.json` — **2673 ids**, congelados de uma corrida
real contra `../gow2-recomp/ghidra_out/` (local; nunca versionado):

| Classe | No gate | Total medido |
|---|---|---|
| I4 (o oráculo viu, nós não) | 137 | 137 |
| I5 `extende-seguro` | 2422 | 2422 |
| I5 `encurta` | 114 | 114 |
| I5 `extende-funde` | **0 (fora do gate)** | 2618 |
| **Total I5** | 2536 | 5154 |

É exactamente por causa destes números que o critério é por delta: um gate por contagem
absoluta nasceria vermelho com 5291 achados e ensinaria toda a gente a ignorá-lo — a lição
do `verify_lift` no v1.0.

**Segurança do conteúdo:** os 2673 ids são só `I4:functions.json:0xADDR` e
`I5:functions.json:0xADDR:tipo`. Zero nomes de função, zero bytes do EBOOT — verificado
por regex sobre os 2673 (0 fora do formato). É a mesma classe do já versionado
`BOUNDS_BASELINE.json`.

---

## Desvios do plano

**Nenhum desvio de comportamento.** Uma decisão de formato foi acrescentada durante a
execução (Regra 2 — correcção): o `exported_at` original usava `datetime.now()`, o que
faria qualquer export antigo carimbado a posteriori declarar-se fresco. Separado em
`exported_at` (mtime do export) + `stamped_at` (agora), com teste que força o mtime a
2026-07-25 e exige esse valor no manifesto. Commit `9711457`.

Dívida da Fase 19 **não** foi tocada, como o plano exige: contra o lift de **produção** o
passo `MANIFEST` dá **PASS** (`DELTA OK`), e os 7 itens `NOVO` que a Fase 19 viu eram
contra o lift das migrações. Isso é da Fase 21 / LEDG-02.

---

## O que NÃO foi exercitado (G5 — honestidade)

1. **O `analyze_eboot_ghidra.sh` não foi corrido de ponta a ponta.** Uma re-análise
   headless do EBOOT (5,6 MB, ~13 mil funções, com decompilação) custa dezenas de minutos
   a horas e não cabia aqui. Mitigação: o passo `3/4` é *literalmente* o comando
   `oracle_manifest.py write --out "$OUT" --elf "$ELF"` que corri isolado com rc=0 e
   contagens correctas, e o script passa `bash -n`. O que fica por provar é a integração
   dos 4 passos numa corrida real.
2. **O carimbo dentro do `ghidra_analyze.py`** (o `write_manifest` no fim do export) só foi
   verificado ao nível do import e da assinatura — não numa exportação real, pela mesma
   razão. Está envolvido em `try/except` que degrada para aviso, por isso não pode invalidar
   um export.
3. **Nada disto é in-boot.** É ferramenta offline; não há nem pode haver aqui uma afirmação
   sobre pixels, intro ou comportamento do jogo.
4. O baseline está congelado contra **um** export (25/jul). Quando o EBOOT ou o Ghidra
   mudarem, o `check` passa a `stale` e obriga a re-analisar — que é o comportamento
   desejado, mas ainda não foi exercitado num ciclo real de re-análise.

Não foi aberta entrada em `.planning/WINDOWS.md` para estes itens: o ficheiro foi escrito
há minutos por outro plano desta fase e há um agente a correr em paralelo no 20-03 —
evitar escrita concorrente. Ficam registados aqui.

---

## RE-01 fecha?

**Sim.** Os três critérios de aceite do `REQUIREMENTS.md` estão medidos:

| Critério RE-01 | Estado | Evidência |
|---|---|---|
| `./verify_lift.sh $LIFT` sai 0 **sem** a variável | ✅ | `rc_default=0`, 3/3 PASS |
| com `VERIFY_ORACLE=1` corre e sai 0 no lift de produção | ✅ | `rc_oracle=0`, 4/4 PASS, `novos=0` |
| prova por mutação: truncar uma função numa cópia dá `rc=1` | ✅ | `rc=1` + `I5:functions.json:0x00010534:extende-seguro` |
| (requisito) delta, nunca limiar absoluto | ✅ | `baseline_delta` vs 2673 ids; nota anti-`--gate I4,I5` no `audit_boundaries.py` |
| (requisito) proveniência: sha256, data, versão, contagens | ✅ | `MANIFEST.json` acima, sha a bater com o EBOOT |

`REQUIREMENTS.md` **não** foi editado (escrita concorrente com os outros planos da fase);
a linha de rastreabilidade fica para o rollup da fase.

---

## Commits

| Hash | Mensagem |
|---|---|
| `905f529` | test(20-01): suite do oráculo com proveniência (RED) |
| `f902ea8` | feat(20-01): oracle_manifest.py — proveniência do ghidra_out e ids I4/I5 (GREEN) |
| `9711457` | feat(20-01): exported_at do MANIFEST vem do mtime do export, não de "agora" |
| `d356954` | feat(20-01): passo do oráculo Ghidra no verify_lift, opt-in por VERIFY_ORACLE=1 |

Gates TDD na ordem certa: `test(` → `feat(`. Nenhum dado de jogo, nenhum JSON do
`ghidra_out/`, nenhum log. Paths explícitos, sem `git add -A`. Os ficheiros de outras
sessões (`.planning/PROJECT.md`, `MILESTONES.md`, `ROADMAP.md`, `CLAUDE.md`,
`games/gow2/CLAUDE.md`, `config.json`, `*-VERIFICATION.md`, `scripts/gsd_plan*.sh`)
ficaram intactos e fora dos commits.

## Self-Check: PASSED

- `tools/oracle_manifest.py`, `tools/test_oracle_manifest.py`,
  `games/gow2/lift_baseline/ORACLE_BASELINE.json`,
  `games/gow2/lift_baseline/test_oracle_step.py` — todos presentes em disco e no índice git.
- Commits `905f529`, `f902ea8`, `9711457`, `d356954` — todos em `git log`.
- Produção (`../gow2-recomp/functions.json`) com sha256 idêntico ao de antes da mutação.
