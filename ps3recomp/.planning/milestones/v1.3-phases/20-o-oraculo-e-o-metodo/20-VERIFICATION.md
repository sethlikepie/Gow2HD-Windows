---
phase: 20-o-oraculo-e-o-metodo
verified: 2026-08-03T02:24:46Z
status: passed
score: 5/5 must-haves verified
behavior_unverified: 0
overrides_applied: 0
re_verification: null
---

# Fase 20: O oráculo e o método — Relatório de verificação

**Objectivo da fase:** a IA/agente passa a ser navegador do mapa guest com oráculos
determinísticos por trás; `verify_lift` ganha um passo de oráculo Ghidra por delta, e o
método de atacar uma wall fica escrito e usado.
**Verificado:** 2026-08-03T02:24:46Z
**Status:** **passed**
**Re-verificação:** Não — verificação inicial.

Todos os comandos abaixo foram **corridos por mim, nesta sessão**, e não copiados do
SUMMARY. Onde a evidência do SUMMARY é reproduzível offline em minutos (mutação, sha256,
formato do baseline, bloqueios do MCP), reproduzi-a de forma independente.

---

## Critérios de sucesso do ROADMAP (5)

| # | Critério | Estado | Evidência |
|---|---|---|---|
| 1 | `verify_lift.sh` continua rc=0 sem variável nova; com `VERIFY_ORACLE=1` corre o passo Ghidra por delta contra baseline congelado | ✓ VERIFICADO | `PS3_ENGINE_ROOT=$PWD ./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v2` → **rc=0**, imprime `ORACULO:   desligado (opcional: VERIFY_ORACLE=1)`, 3/3 PASS (lift_parity, MANIFEST, audit_boundaries), sem I4/I5. Com `VERIFY_ORACLE=1` (mesmo comando) → **rc=0**, 4/4 PASS, `[oracle_i4_i5] current=2673 baseline=2673 novos=0 resolvidos=0` |
| 2 | Prova por mutação revalidada: truncar uma função numa cópia faz `rc=1` | ✓ VERIFICADO | Reproduzi eu mesmo a mutação (função `0x00010534`, end `0x000105D0`→`0x000105C8`, cópia em `mktemp -d`): `VERIFY_ORACLE=1 PS3_FUNCTIONS="$MUT/functions.json" … verify_lift.sh` → **rc=1**, `NOVO  I5:functions.json:0x00010534:extende-seguro` — id idêntico ao citado no SUMMARY. `functions.json` de produção com sha256 **idêntico** antes/depois (`90ad64dd…d884d7`) |
| 3 | `ghidra_out/MANIFEST.json` regista proveniência (sha256, data, versão, contagens) | ✓ VERIFICADO | `../gow2-recomp/ghidra_out/MANIFEST.json` existe; `elf_sha256=23cfd435…1163b` **bate** com `shasum -a 256 ../gow2-recomp/EBOOT.ELF` corrido por mim agora; `exported_at=2026-07-25T19:26:53Z` (mtime do export, ≠ `stamped_at`); `ghidra_version=12.1.2`; contagens `functions=13143 decompiled=13096 names=266 strings=1962 symbols=114065` |
| 4 | `docs/RE_PLAYBOOK_GOW2.md` com as 6 secções mínimas, e usado uma vez numa wall real (D ou C2), com ledger de hipóteses refutadas por medição | ✓ VERIFICADO | 6 secções presentes e nomeadas (`## 1.` pergunta de wall … `## 6.` anti-padrões, todos os 13 itens incl. CRC bypass/stub/magic à mão/null-check cosmético/mascarar guard/LLM a reescrever). `docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md` ataca a parede 4 (Fase 11): 4 hipóteses tabeladas, **3 refutadas por medição** com comandos e `rc`, veredicto **ABERTO** (não forjado). Refiz uma das medições centrais (H3): `0x00010534` no `functions.json` de produção termina em `0x105D0`; no `ghidra_out/functions.json` (`addr=0x00010534, size=156`) também termina em `0x105D0` — concordância confirmada de forma independente, não só citada |
| 5 | `docs/GHIDRA_MCP_SETUP.md` com versão fixada e política (leitura/anotação sim, reescrita/produto não); soft: `wont` se instável, playbook fecha na mesma | ✓ VERIFICADO | Documento com 10 secções, pin `LaurieWired/GhidraMCP 1.4` (commit `27f316f8`, sha256 do artefacto citado), whitelist (19 leitura / 6 anotação / 2 condicionais) e 7 proibições + política de 4 linhas. `wont` declarado com 4 bloqueios — **remedidos por mim**: `which mvn` → ausente (confirmado); `application.version=12.1.2` em `/opt/homebrew/Cellar/ghidra/12.1.2/.../application.properties` (confirmado, ≠ `ghidraVersion=11.3.2` da extensão); `~/.claude.json` → `mcpServers={'MCP_DOCKER': …}` sem nenhum `ghidra` (confirmado); `find … *.gpr *.rep` → nada (confirmado) |

**Score:** 5/5 critérios verificados por reprodução directa, não por leitura de SUMMARY.

---

## Requisitos (RE-01, RE-02, RE-03)

### RE-01 — fecha

Critério (`REQUIREMENTS.md`): `verify_lift.sh` sai 0 sem variável; com `VERIFY_ORACLE=1`
corre e sai 0 no lift de produção; prova por mutação dá `rc=1`.

Os três sub-testes foram corridos por mim (não apenas citados) — ver critérios 1 e 2 acima.
Testes unitários também corridos directamente nesta sessão:

```
.venv/bin/python3 tools/test_oracle_manifest.py                     → 10/10 PASS, rc=0
python3 tools/test_oracle_manifest.py (python3.9.6 do sistema)      → 10/10 PASS, rc=0
python3 games/gow2/lift_baseline/test_oracle_step.py                → 5/5 PASS, rc=0
```

`accept_relift.sh` **não** define `VERIFY_ORACLE` em lado nenhum
(`grep -n VERIFY_ORACLE games/gow2/accept_relift.sh` → 0 linhas) — o caminho de aceitação
não regride.

**Confirmação adicional (não pedida explicitamente, mas relevante):** o `ORACLE_BASELINE.json`
tem **2673 ids**, todos no formato `I4:functions.json:0xADDR` ou
`I5:functions.json:0xADDR:tipo` — verifiquei os **2673 na íntegra** (não uma amostra) contra
uma regex e **zero** estão fora do formato; `extende-funde` conta **0** dentro do conjunto do
gate (2618 medidos fora dele), coerente com a política declarada de excluir funde do gate mas
contá-lo. Contagens por classe (I4=137, seguro=2422, encurta=114) somam exactamente 2673.

**RE-01 fecha.**

### RE-02 — fecha

Critério: playbook existe **e foi usado uma vez** numa wall real (D ou C2), com ledger de
hipóteses refutadas por medição; "ainda aberto" é resultado válido.

O playbook cobre as 6 secções mínimas do ROADMAP. A sessão em
`docs/re_sessions/2026-08-02-wall-4-walk-de-tipos.md` ataca a parede 4 da Fase 11 (autorizada
pelo `20-CONTEXT.md` como candidata legítima, junto com D/C2). Li a sessão inteira: as
refutações não são afirmações vagas — H1 e H3 comparam **bit a bit** as máscaras
`ppc_rlwinm`/`ppc_rldicl` do lift contra o C decompilado do Ghidra, e o mecanismo candidato
final (o par push/pop indexar campos diferentes da mesma palavra) é derivado por aritmética
sobre valores medidos noutra sessão, não inventado. O veredicto é honestamente **ABERTO**, com
secção explícita "O que esta sessão NÃO afirma" (6 itens) — não há fecho forjado.

Ressalva já declarada no próprio SUMMARY: `REQUIREMENTS.md` diz "wall real (D ou C2)" e a
sessão atacou a parede 4 do inventário da Fase 11 (não literalmente "D ou C2"); o
`20-CONTEXT.md` autoriza essa substituição por escrito e por custo. Aceito essa leitura — o
`20-CONTEXT.md` é o documento de escopo do plano.

**RE-02 fecha.**

### RE-03 — fecha, na metade *soft* que o próprio requisito autoriza

Critério: `docs/GHIDRA_MCP_SETUP.md` com versão fixada, tools permitidas/proibidas; 1 sessão
de paridade com os mesmos callers que `ghidra_lookup.py`; soft — se instável, `wont` fecha na
mesma.

Documento existe com versão fixada e política. A paridade tem o lado **CLI executado e
escrito** (duas âncoras, saída literal, incluindo uma auto-refutação honesta de uma hipótese de
bug no `--callers` que caiu numa sonda de controlo). O lado **MCP não correu** — declarado
`wont` com **4 bloqueios medidos**, todos remedidos por mim nesta sessão (ver critério 5
acima) e todos batendo com o que o documento afirma. Não encontrei nenhum bloqueio "presumido"
sem número/comando por trás — cada um dos 4 tem uma medição citável.

O quinto ponto do documento (um agente já em execução não adquire tools de MCP a meio da
sessão) também confere: esta própria sessão de verificação não tem nenhuma tool `mcp__ghidra__*`
disponível.

**RE-03 fecha na metade soft, com `wont` medido — não é desistência presumida.**

---

## O que fica por exercitar (declarado, não escondido)

Confirmo que a limitação central do 20-01 está **escrita no próprio SUMMARY**, secção
"O que NÃO foi exercitado (G5 — honestidade)", e não omitida:

1. **`analyze_eboot_ghidra.sh` não correu de ponta a ponta** nesta fase — uma re-análise
   headless do EBOOT custa dezenas de minutos a horas. O que foi provado: o passo `3/4`
   (`oracle_manifest.py write`) isolado com rc=0, `bash -n` sobre o script inteiro (repeti este
   `bash -n` eu mesmo — sintaxe OK), e a estrutura de 4 passos lida directamente no script
   (confirmei os marcadores `1/4`…`4/4` e a chamada a `oracle_manifest.py write` na posição
   `3/4`, **antes** do resumo e **depois** do `ghidra_names.py`, como o SUMMARY descreve).
2. **O carimbo dentro do `ghidra_analyze.py`** (o `write_manifest` embutido no exportador) só
   foi verificado ao nível de import/assinatura, não numa exportação real.
3. **Nada disto é in-boot** — é ferramenta offline. Nenhuma alegação de pixels/comportamento
   do jogo é feita nesta fase, e não encontrei nenhuma no SUMMARY.
4. **Lado MCP da paridade não corrido** (RE-03) — registado como janela aberta 15 em
   `.planning/WINDOWS.md` (confirmei a entrada: `open_count: 12`, `total_count: 15`).

Estas quatro limitações estão **escritas nos artefactos entregues** (SUMMARY 20-01, apêndice
§9–10 do `GHIDRA_MCP_SETUP.md`, `WINDOWS.md`), não inferidas por mim. Não são gaps que
bloqueiem os critérios do ROADMAP — o próprio critério 5 do ROADMAP autoriza soft-close por
`wont`, e nenhum critério exige a corrida de ponta a ponta do `analyze_eboot_ghidra.sh`.

---

## Anti-padrões / segurança de conteúdo

| Verificação | Resultado |
|---|---|
| `TBD\|FIXME\|XXX\|TODO\|HACK\|PLACEHOLDER` nos 11 ficheiros da fase | Nenhum marcador de dívida — os únicos hits são o padrão de nomenclatura `func_XXXXXXXX`/`.opd.FUN_XXXXXXXX`, não marcadores |
| `ghidra_out/*.json` no git (monorepo ou `gow2-recomp`) | Nenhum — `git check-ignore -v` confirma `ghidra_out/` coberto pelo `.gitignore` do `gow2-recomp`; `git ls-files` no monorepo não lista nada em `ghidra_out` |
| `ORACLE_BASELINE.json` — nomes de função / bytes do EBOOT | Zero — 2673/2673 ids batem no formato `I[45]:functions.json:0xADDR[:tipo]`, verificado na íntegra (não amostra) |
| Ficheiros de produção tocados pelos 8 commits da fase (`905f529`…`04f5a78`) | Nenhum — `git show --stat --name-status` de cada commit lista só `tools/`, `games/gow2/verify_lift.sh`, `games/gow2/analyze_eboot_ghidra.sh`, `games/gow2/lift_baseline/`, `docs/` |
| `recomp_macos_v2` / binário `boot_gow2` alterados | Não — as três corridas de `verify_lift.sh` (default, oráculo, mutação) sobre o mesmo lift devolveram sempre `novos=0` nos passos não mutados, e a corrida final pós-mutação voltou a `rc=0` |

---

## Verificação humana necessária

Nenhuma. Todos os 5 critérios do ROADMAP e os 3 requisitos foram verificados por comandos
correntes nesta sessão (não por leitura de SUMMARY), incluindo a reprodução independente da
prova por mutação (RE-01) e a remedição dos 4 bloqueios do MCP (RE-03). O único elemento que
o próprio SUMMARY marca como `human_judgment: true` — se a sessão de RE do 20-02 é "RE a
sério" e não teatro escrito a posteriori — foi avaliado por leitura integral do ledger: as
refutações usam aritmética verificável (máscaras `rlwinm`/`rldicl` aplicadas a valores
concretos, contagem de consumidores por `--grep`), não afirmações vagas, e o veredicto
permanece honestamente "aberto". Considero isso resolvido por evidência, não por confiança.

---

## Conclusão

**RE-01 fecha. RE-02 fecha. RE-03 fecha na metade soft, com `wont` medido — não é desistência
presumida.** Os 5 critérios de sucesso da Fase 20 no ROADMAP estão verdadeiros no código, não
apenas afirmados no SUMMARY. A limitação declarada (re-análise Ghidra de ponta a ponta não
corrida) está escrita nos artefactos, não escondida, e não é exigida por nenhum critério de
aceite da fase.

---

_Verificado: 2026-08-03T02:24:46Z_
_Verificador: Claude (gsd-verifier)_
