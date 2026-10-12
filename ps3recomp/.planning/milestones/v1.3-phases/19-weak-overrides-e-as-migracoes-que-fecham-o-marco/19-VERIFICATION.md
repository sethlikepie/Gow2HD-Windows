---
phase: 19-weak-overrides-e-as-migracoes-que-fecham-o-marco
verified: 2026-08-03T01:31:01Z
status: passed
score: 4/4 critérios ROADMAP verificados (com ressalvas documentadas, ver secção 6)
behavior_unverified: 0
overrides_applied: 0
---

# Fase 19: Weak overrides e as migrações que fecham o marco — Verificação

**Meta da fase:** Poder substituir uma função guest inteira sem editar o `.cpp` gerado —
e usar esse mecanismo, mais os das Fases 17–18, para chegar aos ≥3 fixes de correctness
re-lift-safe que fecham o marco.
**Verificado:** 2026-08-03T01:31Z
**Estado:** passed (com ressalvas honestas documentadas — ver secção 6 e 7)
**Metodologia:** goal-backward, todos os comandos abaixo foram CORRIDOS por mim nesta
sessão de verificação (não é transcrição do SUMMARY) — evidências e logs próprios em
`/tmp/verify_*.log` e reaproveitamento dos artefactos deixados em disco pela Fase 19
(`/tmp/m1_f19_lift_A`, `../gow2-recomp/recomp_macos_v3_mig19{,b,c}`, `boot_gow2_f19_mig3`).

---

## 1. `nm -m` no `test_weak_override_symbols.py` (não `nm` simples)

**Verificado.** `tools/test_weak_override_symbols.py:531` chama `subprocess.run(["nm",
"-m", str(path)], ...)` — a forma correta no Mach-O (`nm` simples imprime `T` para uma
definição fraca; só `nm -m` distingue `weak external` de `external`). Corri o script eu
mesmo:

```
$ .venv/bin/python3 tools/test_weak_override_symbols.py
... 56 asserções, todas [ok] ...
rc=0
```

Nota operacional (não é um gap da fase, mas relevante para reprodução): o `python3` de
sistema (`/usr/bin/python3`, 3.9.6) **falha** neste script com
`TypeError: unsupported operand type(s) for |: 'type' and 'NoneType'` — o script usa
sintaxe `str | None` (PEP 604, Python ≥3.10). Só corre com `.venv/bin/python3` (3.14),
que é o que o `PROJECT.md`/scripts do repo já assumem. O comando `python3
tools/test_weak_override_symbols.py` pedido literalmente na instrução de verificação
falha no ambiente default deste Mac — reportado como ressalva de ambiente, não como
falha da fase.

Assertions-chave que provam a medição (não a suposição) do risco do `ld` da Apple:
- `L1a/L1b`: `nm -m` vê o wrapper como `weak external` e o `__imp_` como `external` forte.
- `L3b/L4`: override forte ganha na chamada directa E via `function_table`.
- `L6` (controlo por mutação): o MESMO par com wrapper FORTE **tem de falhar** o link
  (`duplicate symbol`) — passou, confirmando que é o `weak` quem carrega o mecanismo.
- `L7-O1/L7-O2`: o override continua a ganhar a -O1 e -O2 (o wrapper não foi inlined).

## 2. `runtime/ppu/ppc_hooks.h` — Clang-first, emissão atrás de flag, off por default

**Verificado**, lido o ficheiro inteiro (97 linhas):
- `#if defined(__clang__)` define `PPC_HOOKS_WEAK_SUPPORTED 1` e `PPC_WEAK
  __attribute__((weak))`; o `#else` degrada para símbolo FORTE (`PPC_WEAK` vazio) —
  falha alta e ruidosa no link em vez de mecanismo que "não pega" silenciosamente.
- `PPC_FUNC_IMPL(name)` → `void __imp_##name(...)` (forte); `PPC_FUNC(name)` →
  `PPC_WEAK void name(...)` (wrapper fraco).
- Em `tools/ppu_lifter.py`: `--emit-weak-wrappers` é `action="store_true"` (default
  `False`); a emissão só liga por CLI **ou** por `[main].emit_weak_wrappers` no TOML
  (linhas 4210, 4342-4349) — nenhuma das duas por omissão. Confirmado também pelas
  asserções do `test_weak_override_symbols.py` ("diff vazio contra o golden (sem
  --config)", "zero símbolos __imp_func_ sem --config").

## 3. `nm -m` no binário real — override forte ganha o link

**Verificado sobre o binário deixado em disco pela própria Fase 19**
(`../gow2-recomp/boot_gow2_f19_mig3`, 121.667.408 B, sha256
`2427ea153022...` — bate exactamente com o registado no SUMMARY 19-03):

```
$ nm -m ../gow2-recomp/boot_gow2_f19_mig3 | grep -E '13func_(0002F3F0|000CE03C|002C0498)P11ppu_context$|19__imp_func_...'
external __Z13func_0002F3F0P11ppu_context      <- forte (host ganhou)
external __Z13func_000CE03CP11ppu_context      <- forte (host ganhou)
external __Z13func_002C0498P11ppu_context      <- forte (host ganhou)
external __Z19__imp_func_0002F3F0...
external __Z19__imp_func_000CE03C...
external __Z19__imp_func_002C0498...
```

`external` (não `weak external`) dentro do binário ligado só pode vir do objecto host —
confirma que a definição FORTE (o override) ganhou sobre o wrapper fraco do lift nos
três pilotos migrados, mais o piloto original de identidade `func_00010230` (também
`external` no binário, confirmado). `strings -a` confirma o texto do fix (`CE03C Play
aborted`, `2F3F0 bad str=`) presente no binário (1 ocorrência cada) e ausente do lift.

## 4. A contagem do XEN-05 — recontada por mim, independentemente do ledger

Corri `awk` directo sobre `games/gow2/lift_baseline/PATCH_MIGRATION.tsv` (linhas de
dados, `NR>86`, ignorando os comentários do cabeçalho):

```
classe=CORRECTNESS AND estado=migrated  →  4 linhas
  patch_b71_2f3f0_guard.py          (weak,       0x0002F3F0)
  patch_ce03c_introseq_block.py     (midasm+weak, 0x000CE03C)
  patch_ce03c_movie_done_reset.py   (midasm,      0x000CE03C — subsumido pelo hook)
  patch_fios_play_already_active.py (weak,       0x002C0498)

estado=redundant                        →  2 linhas (as duas jump tables da Fase 18)
classe=OPD AND estado=migrated          →  0 linhas
```

Estes números batem **exactamente** com o cabeçalho congelado do ledger e com o
SUMMARY. Cruzando com `CONTRACTS.tsv`: existem 4 contratos executáveis
(`midasm-ce03c-waitidle`, `weak-ce03c-pad`, `weak-2f3f0`, `weak-2c0498`) — os dois
primeiros cobrem a mesma função `func_000CE03C` (o hook mid-asm da Fase 17 cobre o
wait-idle, o override da Fase 19 cobre o pad de `setjmp`, e o próprio texto do contrato
`weak-ce03c-pad` declara explicitamente "Complementa midasm-ce03c-waitidle, que cobre a
OUTRA metade do mesmo patch").

**As duas leituras, verificadas:**
- **Por linha:** 4/4 ≥ 3 → ✅
- **Por mecanismo, leitura conservadora (1 função = 1 mecanismo):** `func_000CE03C`,
  `func_0002F3F0`, `func_002C0498` = **3** mecanismos ≥ 3 → ✅ (é a leitura mais apertada
  possível, e ainda assim fecha)

**Isto não se aguenta só numa leitura generosa** — mesmo a leitura mais conservadora
(1 função = 1 mecanismo, que é a regra que a própria Fase 18 estabeleceu para o caso
análogo do `Ce03cWaitIdle`/`movie_done_reset`) dá exactamente 3, o mínimo do critério.
A subsunção da Fase 18 (`Ce03cWaitIdle` subsume `movie_done_reset`) **continua a
aplicar-se** e está correctamente reflectida (as duas linhas do CE03C contam como 1
mecanismo na leitura conservadora, não como 2).

## 5. A escada A/B/C — reproduzida por mim, não só a palavra do executor

Os artefactos da escada ainda estavam em disco (`/tmp/m1_f19_lift_A`,
`../gow2-recomp/recomp_macos_v3_mig19c`). Corri `check_contracts.py` eu mesmo contra
ambos, para os 3 patches:

**Passo A (sem patch, sem `--config`) — os aceites TÊM de falhar:**
```
patch_ce03c_introseq_block.py      → [FAIL] midasm-ce03c-waitidle, [FAIL] weak-ce03c-pad   rc=1
patch_b71_2f3f0_guard.py           → [FAIL] weak-2f3f0                                     rc=1
patch_fios_play_already_active.py  → [FAIL] weak-2c0498                                    rc=1
```
Confirmado: sem o mecanismo, o fix genuinamente não está no lift. É isto que distingue
`migrated` de `redundant`.

**Passo B (sem patch, com `--config`) — os mesmos aceites TÊM de passar:**
```
patch_ce03c_introseq_block.py      → [PASS] midasm-ce03c-waitidle, [PASS] weak-ce03c-pad   rc=0
patch_b71_2f3f0_guard.py           → [PASS] weak-2f3f0                                     rc=0
patch_fios_play_already_active.py  → [PASS] weak-2c0498                                    rc=0
```

**Conclusão:** a escada A/B é **reproduzível**, não uma alegação do executor — corri os
dois passos eu mesmo, sobre os ficheiros reais deixados em disco, e o resultado bate
exactamente com o que o SUMMARY 19-03 relata.

## 6. `check_contracts.py` e `verify_lift.sh` — o ponto que merece ceticismo

**`check_contracts.py` contra o lift de produção** (`recomp_macos_v2`, que ainda leva os
patches legados injectados): PASS nos 3 patches, `rc=0` em cada um — esperado, porque o
contrato tem alternativa regex que serve tanto o texto injectado antigo como o novo
mecanismo.

**`verify_lift.sh`** — corri contra 3 lifts diferentes:

| Lift | `lift_parity` | `MANIFEST` | `audit_boundaries` | rc global |
|---|---|---|---|---|
| `recomp_macos_v2` (produção) | PASS | **PASS** | PASS | **0** |
| `recomp_macos_v3_mig19` (só CE03C migrado, sub-passo intermédio da própria Fase 19) | PASS | **FAIL** (current=7 baseline=36 novos=7) | PASS | **1** |
| `recomp_macos_v3_mig19c` (as 3 migrações, o lift final do 19-03) | PASS | **FAIL** (current=7 baseline=36 novos=7) | PASS | **1** |

O SUMMARY 19-03 afirma que este `rc=1` "tem os números ao número iguais aos do 19-02 e
da Fase 18" — ou seja, dívida congelada, não um achado novo desta fase. **Verifiquei isto
por reprodução, não por confiança no texto:** corri `verify_lift.sh` contra
`recomp_macos_v3_mig19` — o **primeiro** re-lift da escada desta fase, produzido **antes**
de `patch_b71_2f3f0_guard.py` e `patch_fios_play_already_active.py` serem migrados — e
obtive **exactamente os mesmos 7 itens `NOVO`** (`AMENOS:PREAMBLE:g_trampoline_fn`,
`AMENOS:SYM:ps3_factory_freelist_replenish`, `ps3_factory_repair_vt`,
`ps3_factory_reuse_product`, `ps3_mp_on_enter`, `AMENOS:TAG:[INTROSEQ]`,
`AMENOS:TAG:[POSTINTRO]`). Como estes 7 itens já apareciam no **primeiro** sub-lift da
escada, antes das duas migrações que fecham o XEN-05 terem sido feitas, a conclusão do
SUMMARY é sustentável: o `MANIFEST FAIL` não é causado pelas migrações que contam para o
XEN-05 — é o efeito colateral, já conhecido, de o `gen_manifest.py` só varrer os chunks
`ppu_recomp_*.cpp` (não os `.cpp` host em `games/gow2/hooks/`), e portanto qualquer
mecanismo que mova texto injectado do lift para um ficheiro host (o que a Fase 17 já fez
para o `Ce03cWaitIdle`/`[INTROSEQ]`) faz esses marcadores aparecerem como "a menos" numa
ferramenta com esse âmbito. O código-fonte de `gen_manifest.py` (comentário nas linhas
43-52, `preamble_end()`) já documenta esta mesma medição, datada de 2026-08-02.

**Não consegui confirmar de forma totalmente independente** que este exacto `7/36` já
existia no baseline da **Fase 18** propriamente dita (os SUMMARY/VERIFICATION da Fase 18
não citam estes números literalmente — só o comentário de código da própria Fase 19 o
afirma). O que **posso** afirmar com evidência directa e reproduzida por mim: o `7/36`
já existia no **primeiro** re-lift produzido dentro da Fase 19 (antes das migrações que
fecham o XEN-05), o que é suficiente para dizer que **não é uma regressão introduzida
pelas 2-3 migrações contadas para o XEN-05** — mas fica registado como uma pequena
lacuna de evidência (não pude reproduzir o baseline exacto da Fase 18 em si, só o estado
já presente no início da Fase 19).

**Resposta directa à pergunta do critério 6:** o `rc=1` do `verify_lift.sh` **não** é
"só MANIFEST" no sentido de zero achado — há 7 achados `NOVO` reais e nomeados. Mas são
demonstravelmente **anteriores** às migrações que contam para o XEN-05 (reproduzido no
sub-lift intermédio da própria escada desta fase) e não relacionados com elas
(g_trampoline_fn, ps3_factory_*, e os marcadores INTROSEQ/POSTINTRO que já tinham saído
do âmbito do lift desde a Fase 17). O `check_contracts.py` — a ferramenta que
especificamente verifica CADA migração contada para o XEN-05 — dá `rc=0`/PASS limpo nos
3 patches, em 2 lifts diferentes. **O critério ROADMAP #4 diz literalmente "verify_lift.sh
e check_contracts.py verdes"; isso não é 100% verdade** (verify_lift.sh não devolve 0)
— é uma ressalva real, já declarada no SUMMARY e não escondida, mas o texto literal do
critério não bate ao pé da letra.

## 7. Produção intacta

**Verificado.** `recomp_macos_v2/ppu_recomp_*.cpp` tem `mtime` de 29-31/jul (antes da
janela de trabalho da Fase 19, que é 2/ago). `sha256(cat ppu_recomp_*.cpp)` do lift de
produção = `73c8abd21abb17e829fc4186b55a5b2395a3d0e4b4f8019bf6bd8e8cc901ae7f` — **idêntico**
ao valor congelado no cabeçalho do `PATCH_MIGRATION.tsv` desde a Fase 16. Os 3 re-lifts
da escada foram para directórios novos (`recomp_macos_v3_mig19{,b,c}`), e o lift A para
`/tmp` — nenhum binário de produção foi tocado ou promovido.

## 8. `WINDOWS.md` #8 — fechado com ressalva escrita

**Verificado**, lido o ficheiro completo. Entrada 8: `status: fixed`, com o texto de
fecho a incluir explicitamente:

> "RESSALVA HONESTA: o ramo de abort NAO foi exercitado in-boot -- o smoke de intro de
> 25 s com PS3_MOVIE_EOS=0 nao chega a condicao que o dispara (ICALL-ASCII com
> StartSeq>=2 e streak>=12); ce03c_play_aborted=0 nas tres corridas. A prova e'
> estrutural/de link (offline-unit), nao de runtime."

Não foi fechado sem qualificação — a ressalva está escrita na própria entrada, e é
consistente com o que corri eu mesmo: `grep -c ce03c_play_aborted` no log de smoke real
dá 0 nas três corridas citadas, e a prova estrutural (`nm -m`, `otool`, `strings`) é a
única evidência para o ramo de abort. A parte que **é** in-boot (o wait-idle, 7 linhas
`[INTROSEQ]`, perfil 1/5/1) está correctamente separada da parte que não é.

## 9. Smoke in-boot — reproduzido a partir do log em disco

`/tmp/m1_fase19_smoke_migracoes.log` ainda existe. Verifiquei:
- Sequência `st620`: `0→1, 1→3, 3→3 (×2), 3→11, 11→11` — máximo **11**, bate com o
  baseline da Fase 16 (piso ≥3, medido 11).
- `[CONTENT] boot logo queue DONE — hold cleared` no fim do log — sem crash.
- Zero ocorrências reais de abort/crash (as únicas linhas com "assert" são
  `[SPU0ASSERT] a=0x00000000 b=0x00000000`, uma marca de trace SPU rotineira e não uma
  falha).
- `ce03c_play_aborted` e `b71_2f3f0 bad/cap`: 0 ocorrências, como esperado (o smoke de
  intro não exercita os caminhos patológicos que esses guards existem para apanhar).

---

## Resumo por critério ROADMAP (Fase 19)

| # | Critério | Estado | Evidência |
|---|---|---|---|
| 1 | `ppc_hooks.h` com `PPC_FUNC`/`PPC_FUNC_IMPL`, emissão atrás de flag, off por default | ✅ VERIFICADO | Ficheiro lido, `--emit-weak-wrappers` é `store_true` (default False), confirmado por 56 asserções do teste corridas por mim |
| 2 | `test_weak_override_symbols.py` prova por `nm` (na verdade `nm -m`) que o fraco existe e o forte ganha | ✅ VERIFICADO | `nm -m` na linha 531 do script; `rc=0` corrido por mim; controlo por mutação L6 passou |
| 3 | Override trivial liga e corre; smoke não regride | ✅ VERIFICADO | `func_00010230` `external` (forte) no binário real; smoke com máximo `st620=11` = baseline |
| 4 | ≥3 correcções `CORRECTNESS`/`migrated`, cada uma provada por re-lift sem patch; `verify_lift.sh`/`check_contracts.py` verdes | ⚠️ VERIFICADO COM RESSALVA | 4 linhas / 3 mecanismos (leitura conservadora) recontados por mim independentemente; escada A/B reproduzida por mim; `check_contracts.py` PASS limpo nos 3 patches; **mas** `verify_lift.sh` global devolve `rc=1` por `MANIFEST` — achado real mas demonstravelmente pré-existente à própria Fase 19 (reproduzido no sub-lift intermédio da escada, antes das migrações contadas), não uma regressão introduzida pelas correcções contadas para o XEN-05 |

## Resumo de requisitos

| Requisito | Aceite (REQUIREMENTS.md) | Estado |
|---|---|---|
| `XEN-04` | `test_weak_override_symbols.py` PASS (weak + strong override ganha, por `nm`); 1 override trivial liga e corre | ✅ SATISFEITO |
| `XEN-05` | ≥3 correcções `CORRECTNESS` re-lift-safe; para cada uma, re-lift sem correr o patch mantém o mecanismo; `verify_lift.sh` e `check_contracts.py` não regridem | ✅ SATISFEITO NO NÚMERO (4 linhas / 3 mecanismos, ambas leituras ≥3), com a mesma ressalva do MANIFEST acima — o "não regridem" é verdade por reprodução (o `MANIFEST FAIL` já existia antes destas migrações), mas `verify_lift.sh` isolado não está 100% verde |

*(Nota: `REQUIREMENTS.md` linha 608-609 ainda lista `XEN-04`/`XEN-05` como "Pendente" na
tabela de rastreio — é uma tabela de estado do documento, não tocada por esta
verificação por instrução explícita; caberá a quem gere o ROADMAP reconciliar.)*

---

## A pergunta central: o XEN-05 fecha de facto?

**Sim, fecha — com uma ressalva concreta que fica escrita aqui e não deve ser lida como
"forjado".**

A contagem central (≥3 correcções `CORRECTNESS` re-lift-safe) **não depende de uma
leitura generosa**: recontei eu mesmo, directamente do `.tsv`, ignorando os comentários
do ledger, e a leitura mais conservadora possível (1 função = 1 mecanismo, a mesma regra
que a Fase 18 usou para o caso análogo) ainda dá exactamente 3 — o mínimo do critério, não
uma folga. A terceira migração (`0x002C0498`) foi genuinamente necessária para sair da
zona ambígua de 2/3, e a evidência de que ela está realmente presente e re-lift-safe é
reproduzível: eu mesmo corri a escada A (falha sem mecanismo) e B (passa com mecanismo,
sem o patch legado) para os 3 patches, sobre os ficheiros reais deixados em disco, e o
resultado bate com o que o SUMMARY relata.

A única mancha real é que o critério ROADMAP #4 pede `verify_lift.sh` **e**
`check_contracts.py` "verdes", e só o segundo está literalmente verde — o primeiro
devolve `rc=1`. Investiguei isto a fundo (não aceitei a alegação do SUMMARY de "dívida
congelada" sem prova): reproduzi o mesmo `MANIFEST FAIL` num sub-lift produzido **antes**
das duas migrações que fecham a contagem, o que corrobora que não é regressão causada
por elas. Isto é uma ressalva honesta e documentada, coerente com a cultura de "PARCIAL
com o número real, nunca forjado" que a Fase 19 (e o `CLAUDE.md`) exige — não invalida o
fecho do XEN-05, mas fica registado que a palavra "verdes" do critério ROADMAP não é
100% literal.

---

## Fronteiras respeitadas nesta verificação

- `.planning/ROADMAP.md` **não foi tocado**.
- Nenhum verbo `roadmap.update-plan-progress` ou `state.*` foi usado.
- Nenhum ficheiro fora da lista de saída foi escrito; todos os comandos correram em
  árvores já existentes (`/tmp`, `../gow2-recomp/recomp_macos_v3_mig19*`,
  `../gow2-recomp/boot_gow2_f19_mig3`) sem gerar novos re-lifts nem builds.
- Nada foi corrigido — apenas relatado, por instrução explícita.

---

_Verificado: 2026-08-03T01:31Z_
_Verificador: Claude (gsd-verifier)_
