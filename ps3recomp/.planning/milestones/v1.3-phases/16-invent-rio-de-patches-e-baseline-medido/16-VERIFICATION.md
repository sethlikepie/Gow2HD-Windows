---
phase: 16-invent-rio-de-patches-e-baseline-medido
verified: 2026-08-02T21:00:00Z
status: passed
score: 4/4 must-haves verificados (critérios ROADMAP) + LEDG-01 SATISFEITO
behavior_unverified: 0
overrides_applied: 0
re_verification: null
---

# Fase 16: Inventário de patches e baseline medido — Verificação

**Objetivo da fase:** Saber exatamente o que migrar e contra que número medir a
melhoria — substituir a estimativa "80+ patches" por um ledger classificado com
EA e aceite por patch.

**Verificado:** 2026-08-02
**Status:** `passed`
**Requisito:** `LEDG-01` — **FECHA nesta fase.** (`LEDG-02` é responsabilidade da
Fase 21 e não foi tocado nem prometido aqui — corretamente fora do escopo.)

## Metodologia

Nada foi aceite por leitura do SUMMARY. Cada afirmação abaixo foi confirmada por
comando corrido nesta sessão de verificação: grep sobre o TSV real, execução dos
dois test suites, regeneração `--merge` + `diff -q`, `check_contracts.py` corrido
ao vivo contra `recomp_macos_v2` para 2 dos 7 P0, grep literal sobre os 4 P0
`grep:`, e inspeção direta dos dois logs de baseline ainda presentes em `/tmp`
(não versionados, como o G6 exige).

## Critério 1 — Ledger com 7 colunas obrigatórias e ≥15 patches classificados

**Status: ✓ VERIFICADO**

- `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` existe, versionado, 182 linhas
  totais (41 de cabeçalho `#`, 1 de header de colunas, 140 de dados).
- Header de colunas (linha 42): `patch classe alvo ea prioridade aceite estado
  ea_pista_nome subclasse_catalogo so_no_build marcador` — as **7 colunas
  obrigatórias** (`patch`, `classe`, `alvo`, `ea`, `prioridade`, `aceite`,
  `estado`) estão todas presentes, mais 4 colunas derivadas auxiliares
  documentadas como tal.
- Contei diretamente (awk) as linhas com as **4 colunas humanas** preenchidas
  (`alvo`, `prioridade`, `aceite`, `estado` não vazios): **19 linhas** — bate
  exatamente com `N_classificados=19` que o cabeçalho auto-deriva.
- Domínios respeitados: `classe` só toma valores em
  `{CORRECTNESS, PROBE, OPD, OBSOLETE}` (subconjunto do domínio declarado,
  `BUILD` reservado sem ocorrências — consistente); `alvo` das 19 classificadas
  usa `{weak, midasm, switch_table, keep-probe}` — dentro do domínio.
- Os 6 nomes de maior impacto citados pelo ROADMAP estão TODOS entre os 19
  classificados: `patch_fallthrough_2550c8.py`, `patch_jumptable_2a209c.py`,
  `patch_jumptable_2b11b8.py`, `patch_ce03c_introseq_block.py`,
  `patch_factory_opd_gate.py` / `patch_icg_ctor_opd.py` (factory OPD / ICG
  ctor), `patch_host_res_inflate.py` (host inflate).
- `host inflate` tem `alvo=runtime` (não forçado a `midasm`), como o must-have
  do 16-02-PLAN exigia — confirmado na linha 136.

## Critério 2 — ≥5 P0 com EA concreto e aceite executável

**Status: ✓ VERIFICADO**

- Contei **7 linhas** com `prioridade=P0` (awk sobre a coluna 5) — acima do
  mínimo de 5.
- **Nenhuma** delas tem `ea='-'` (verificado por awk, resultado vazio).
- Todos os 7 aceites seguem o formato `grep:|contract:|smoke:` exigido; nenhum
  "verificar manualmente".
- Verificação independente ao vivo (não confiei no SUMMARY):

| Patch P0 | EA | aceite | Resultado verificado agora |
|---|---|---|---|
| `patch_14b1f0_opd.py` | `0x0014B1F0` | `contract:opd-14b1f0` | `check_contracts.py` → **rc=0**, 2 checks PASS |
| `patch_32e200_opd.py` | `0x0032E200` | `contract:opd-32e200` | `check_contracts.py` → **rc=0**, 4 checks PASS |
| `patch_ce03c_introseq_block.py` | `0x000CE03C` | `grep:[INTROSEQ] CE03C wait-idle 1st movie` | grep literal sobre `recomp_macos_v2` → **n=1** |
| `patch_factory_opd_gate.py` | `0x0039E6B4` | `grep:[WADLD-FACT] #A` | grep literal → **n=1** |
| `patch_jumptable_2a209c.py` | `0x002A209C` | `grep:case 0x002A27BCu: goto...` | grep literal → **n=3** |
| `patch_jumptable_2b11b8.py` | `0x002B11B8` | `grep:case 0x002B12C4u: goto...` | grep literal → **n=1** |
| `patch_fallthrough_2550c8.py` | `0x002550C8` | `smoke:bytes_read=20169344` | **NÃO-EXERCITADO** — ver Critério 4; declarado no `WINDOWS.md` #3, não escondido |

6/7 aceites confirmados verdes por mim de forma independente; o 7º é
honestamente marcado como não corrido (exige um smoke com `EOS=1` que a
receita de baseline não usa) e está aberto como janela partida rastreada — não
foi forjado como "PASS".

Os 11 `contract:` classificados apontam para IDs que **existem de facto** em
`CONTRACTS.tsv` (confirmei os 11, todos presentes).

## Critério 3 — Contagem baseline congelada, medida, não copiada

**Status: ✓ VERIFICADO**

- Cabeçalho do ledger (linhas 5-10) declara: `patch_*.py monorepo=140`,
  `build=140`, `catálogo=140`, `divergência=0`, medidos "nesta corrida".
- Confirmei ao vivo: `ls games/gow2/recomp_mid_v2/patch_*.py | wc -l` = **140**;
  `ls ../gow2-recomp/recomp_mid_v2/patch_*.py | wc -l` = **140**. Bate.
- O ROADMAP.md (critério 3, texto atual) já contém a nota "Medido depois, no
  mesmo dia: o commit `7f7581d`... a contagem passou a 140 e 140, divergência
  0" — a divergência de 51 registada quando o critério foi escrito (88 vs 139)
  não foi apagada nem escondida; ficou como "o que era verdade quando o
  critério foi escrito", com a medição seguinte anotada ao lado. Não há
  números mortos: o cabeçalho **re-deriva** estes valores a cada `--merge`
  (confirmado — ver "Idempotência" abaixo), por isso nunca fica desatualizado.
- A tabela de rastreabilidade do `REQUIREMENTS.md` (seção MARCO v1.3) também
  regista corretamente "Contagens recontadas a 2026-08-02: 140/140/140".

## Critério 4 — `verify_lift.sh` e smoke corridos uma vez, números honestos

**Status: ✓ VERIFICADO — com honestidade exemplar (regra 4 do CLAUDE.md)**

Inspecionei diretamente os dois logs ainda presentes em `/tmp` (fora dos dois
repositórios, não versionados — confirmado por `git ls-files | grep m0_fase16`
vazio):

- `/tmp/m0_fase16_verify_lift.log` (150 linhas): rodapé confirma
  `lift_parity PASS / MANIFEST PASS / audit_boundaries PASS / VERIFY_RC=0` —
  bate exatamente com o que o cabeçalho do ledger regista.
- `/tmp/m0_fase16_smoke.log` (3707 linhas, confirmei a contagem): sequência
  `st620` observada `0→1→3→3→11→11`, `overlay_done=1` (linhas 254-3707,
  confirmado por grep) — bate com `st620_max=11` do cabeçalho.
- `grep -cE "OOB|0xFFFF"` sobre o log de smoke = **0** — bate com
  `oob_or_ffff_hits=0`.

**O ponto crítico da instrução (honestidade dos campos não medidos) foi
verificado por mim, não apenas lido no cabeçalho:**

- **`sticky`**: corri `grep -n "sticky" runtime/ppu/ppu_loader.cpp | grep -iE
  "fprintf|printf"` — **zero resultados**. As três funções
  `ps3_fios_sticky_publish/peek/consume` (linhas 1370, 1383, 1393) de facto não
  emitem nenhuma linha de log. Um grep sobre o log de smoke daria sempre `0`
  independentemente do estado real do sticky. O ledger regista isto como
  `NAO-OBSERVAVEL` com a razão técnica — **não** como `sticky=0`. Isto é
  exatamente o comportamento que a regra 4 do CLAUDE.md exige e que a
  instrução deste verificador pedia para escrutinar com especial cuidado.
  **Confirmado: não há um zero forjado aqui.**
- **`bytes_read`**: grep sobre o log de smoke confirma que o único
  `[movieio] open` registado é o `gow2.psarc` (linha 82) — **não há nenhuma
  ocorrência de `R_PermA` nem de `GATE-FORCE... R_PermA full`** nas 3707
  linhas. Isto confirma tecnicamente a alegação de que o `R_PermA` não chegou
  a ser lido (a receita usa `PS3_MOVIE_EOS=0`), logo o aceite `smoke:` do P0
  `patch_fallthrough_2550c8.py` fica genuinamente **não-exercitado**, e é
  registado como tal — não como zero nem como "PASS" forjado.
- Ambas as ausências estão rastreadas como janelas partidas abertas em
  `.planning/WINDOWS.md` (itens #3 e #4, ambos `open`, com descrição técnica
  idêntica à do cabeçalho do ledger) — nada foi silenciosamente resolvido.

**Ressalva medida e honestamente disclosed (não é um gap):** 5 dos 7 chunks
`ppu_recomp_*.cpp` de `recomp_macos_v2` são mais recentes (31/jul) que o
binário `boot_gow2` testado (mtime 29/jul). O critério 4 do ROADMAP não exige
que o smoke exercite o lift byte-a-byte idêntico ao que o `verify_lift`
verificou — apenas que os números sejam medidos e registados honestamente, o
que aconteceu. A fase optou corretamente por **não reconstruir** o binário
(regra G5 — não arrumar o ambiente para o baseline parecer melhor), e deixou
isto escrito como trabalho explícito para a Fase 17/21. Registo isto como
observação, não como falha do critério.

## Testes correntes eu mesmo (não confiei nos SUMMARYs)

```
.venv/bin/python3 games/gow2/lift_baseline/test_gen_migration_ledger.py → rc=0, 8/8 PASS
.venv/bin/python3 games/gow2/lift_baseline/test_gen_catalog.py          → rc=0, 11/11 PASS
```

**Idempotência do regen** (corrida por mim, não copiada do SUMMARY):

```
gen_migration_ledger.py --merge PATCH_MIGRATION.tsv -o /tmp/regen.tsv
diff -q PATCH_MIGRATION.tsv /tmp/regen.tsv → IDEMPOTENTE, sem diferenças
```

Confirma que nenhuma coluna humana foi perdida/alterada por um regen, e que o
cabeçalho (incluindo as contagens do Critério 3) é re-derivado de forma
determinística.

## Requirements Coverage

| Requisito | Fase | Status | Evidência |
|---|---|---|---|
| `LEDG-01` | 16 | ✓ SATISFEITO (fecha) | 4/4 critérios do ROADMAP verificados de forma independente acima |
| `LEDG-02` | 21 (fora de escopo aqui) | não aplicável a esta fase | corretamente não reivindicado por nenhum PLAN da Fase 16 |

Nenhum requisito órfão: os 3 PLANs (16-01/02/03) declaram todos apenas
`requirements: [LEDG-01]`, sem duplicação nem lacuna face ao ROADMAP.

## Anti-Patterns

Grep por `TBD|FIXME|XXX|TODO|HACK|PLACEHOLDER` sobre `gen_migration_ledger.py`
e os dois `.tsv` não encontrou nenhum marcador de dívida técnica não
referenciado — as únicas ocorrências de "todo" são o valor de domínio da
coluna `estado` (`{todo, migrated, wont}`), não comentários de dívida.

## Deferred Items

Nenhum. `LEDG-02` pertence estruturalmente à Fase 21 pelo próprio desenho do
marco (ver rastreabilidade em `REQUIREMENTS.md`) e não foi prometido por
nenhum critério de sucesso da Fase 16.

## Human Verification Required

Nenhuma. Todos os 4 critérios e ambos os sub-pontos sensíveis (honestidade dos
campos `sticky`/`bytes_read`) foram verificáveis programaticamente e foram de
facto verificados por comando nesta sessão — não por leitura do SUMMARY.

## Conclusão

**Os 4 critérios de sucesso do ROADMAP Phase 16 estão todos verdadeiramente
cumpridos, com evidência reproduzida independentemente nesta verificação** (não
apenas citada do SUMMARY): 19 patches classificados com as 7 colunas
obrigatórias, 7 P0 com EA concreto e 6/7 aceites confirmados verdes ao vivo (o
7º honestamente não-exercitado e rastreado), contagem baseline 140/140/140
confirmada por `ls` direto, e o baseline de `verify_lift`/smoke medido uma vez
com os campos não-observáveis marcados como tal — não como zeros forjados
(confirmei pessoalmente a ausência de emissores de log para `sticky` no
código-fonte, e a ausência de leitura de `R_PermA` no log de smoke).

**`LEDG-01` fecha nesta fase.** `LEDG-02` (integração do `verify_lift.sh` no
caminho de aceitação de re-lift + documento dos 8 passos + suite T1-T10)
permanece corretamente como responsabilidade da Fase 21, não desta.

---

_Verificado: 2026-08-02_
_Verificador: Claude (gsd-verifier)_
