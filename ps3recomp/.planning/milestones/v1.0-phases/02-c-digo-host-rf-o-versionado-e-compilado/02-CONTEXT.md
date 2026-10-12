# Phase 2: Código host órfão versionado e compilado - Context

**Gathered:** 2026-07-26
**Status:** Ready for planning
**Mode:** Autónomo — decisões fundamentadas em medição feita nesta sessão

<domain>
## Phase Boundary

O código host escrito à mão que hoje só existe dentro dos `.cpp` gitignored passa a ter
casa versionada e a ser compilado como fonte de primeira classe, para que um lift novo e
limpo consiga linkar.

**Fora de escopo:** promover o gate de sincronização `build_macos.sh` de WARN a FAIL
(D-06 da Fase 1 atribui isso a esta fase — ver decisão D-2.5 abaixo); qualquer trabalho
sobre pixels, shaders ou as paredes D/E.
</domain>

<measured_state>
## Estado MEDIDO (2026-07-26, não re-derivar)

### Critério 1 — inventário órfão: as três cifras reconciliadas

| Fonte | Linhas | O que é |
|-------|--------|---------|
| `games/gow2/lift_baseline/injected_00{0..6}.cpp` | **1662** | 000=704, 001=910, 002=20, 003=16, 004/005/006=4 |
| `games/gow2/lift_baseline/preamble_pure.cpp` | **131** | preâmbulo byte-idêntico nos chunks 007–030 do lift antigo |
| **Total** | **1793** | |
| Cifra citada no plano RDY-0 | 1603 | **desactualizada** |

A cifra 1603 vem de uma contagem anterior feita com um "preâmbulo limpo de referência"
errado: usou-se `ppu_recomp_002.cpp` como referência, e esse chunk tem 20 linhas
injectadas. Corrigido para 1662/7 chunks quando o preâmbulo de 131 linhas idêntico nos
chunks 007–030 foi identificado. **O gate `verify_lift_baseline.sh` usa hoje só as 1662 —
não cobre o `preamble_pure.cpp`.**

### Critério 2 — F2B

Vive dentro de `recomp_macos_v2/ppu_recomp_001.cpp` (lift gitignored), sem ficheiro
versionado próprio. **17 símbolos distintos**, 78 ocorrências, primeira na linha 10 e
última na 128446 — ou seja, NÃO é um bloco contíguo: as declarações estão no preâmbulo
e os pontos de uso espalham-se pelo chunk.

Símbolos: `f2b_fo_mfd_get`, `f2b_fo_mfd_put`, `f2b_fo_sz_get`, `f2b_stream_ensure`,
`f2b_stream_eof_try_complete`, `f2b_stream_fill`, `f2b_stream_pre_consume`,
`g_f2b_fill_file_pos`, `g_f2b_fill_fo`, `g_f2b_fill_mfd`, `g_f2b_fill_stream`,
`g_f2b_fill_sz`, `g_f2b_fo_mfd_fd`, `g_f2b_fo_mfd_fo`, `g_f2b_fo_mfd_n`,
`g_f2b_fo_mfd_sz`, `g_f2b_natural_movie_fo`.

O bloco de definições começa em `ppu_recomp_001.cpp:498` (`static uint32_t
g_f2b_fo_mfd_fo[8];`). A cifra "197 linhas" do plano RDY-0 refere-se ao bloco de
definições, não ao span.

### Critério 3 — `host_gow2_factory.cpp` no build

- `games/gow2/host_gow2_factory.cpp` **EXISTE** (versionado).
- `games/gow2/build_macos.sh` **NÃO o compila** — zero ocorrências de `host_gow2_factory`.
- O checkout irmão `../gow2-recomp/build_macos.sh` **JÁ o compila**, condicionalmente,
  desde hoje (bloco acrescentado nesta sessão, com um guard que salta a compilação se o
  lift trouxer as 13 definições dentro dele — evita `duplicate symbol`).
- **`games/gow2/` é um espelho via `git subtree` do irmão e está atrasado**: `diff -rq`
  acusa divergência em `build_macos.sh`, `movie_eos_arm.c`, `lift_baseline/counters_pre.tsv`
  e ~8 `recomp_mid_v2/patch_*.py`, entre outros. É esta divergência que o gate de
  sincronização da Fase 1 apanha e reporta como WARN (2 hunks, `host_gow2_factory`).

### Critério 4 — o link

**JÁ SATISFEITO E MEDIDO.** Um lift regenerado do zero linka:
- `boot_gow2_v3` (115 MB) linkou depois de dois órfãos caírem: as declarações da API host
  (`recomp_mid_v2/patch_zz_host_api_decls.py`) e `ppu_timebase_now` (ps3recomp `e441f37`).
- `boot_gow2_v4` linkou e **passa** o gate M0.
- O lift regenerado foi **promovido a produção** (gow2-recomp `57b418f`): `boot_gow2` dá
  agora `st620=11` em **6/6** runs, igualando o baseline da produção antiga.

O critério pede apenas o link. O link está provado três vezes. O que o plano desta fase
tem de fazer é **fixar isso por gate**, não voltar a demonstrá-lo.
</measured_state>

<decisions>
## Decisões

### D-2.1 — O gate passa a cobrir o `preamble_pure.cpp` (1793, não 1662)
O `verify_lift_baseline.sh` cobre hoje só as 1662 linhas dos `injected_00*.cpp`. As 131
linhas do preâmbulo são inventário órfão exactamente na mesma medida e ficaram de fora.
Passa a cobrir as duas, e o número declarado no gate passa a ser **1793**.

### D-2.2 — As três cifras ficam declaradas no próprio artefacto
1603 (plano RDY-0, desactualizada) / 1662 (gate actual) / 1793 (real). A reconciliação
vive num comentário no `MANIFEST.tsv` ou no `gen_manifest.py`, não só num plano — quem
lê o gate daqui a três meses tem de perceber porque há três números na história.

### D-2.3 — O F2B vai para ficheiro versionado próprio
Nome: `games/gow2/host_gow2_f2b.c` (paralelo a `host_gow2_factory.cpp`). Responsabilidade
declarada no cabeçalho: mapa file-object→(mfd, size) e o preenchimento de stream do
caminho FIOS do movie player. Um instalador `patch_*.py` idempotente declara os protótipos
no chunk que os usa, pelo mesmo padrão do `patch_zz_host_api_decls.py`.

### D-2.4 — A extracção é por **símbolo**, não por intervalo de linhas
O F2B não é contíguo (span de 128437 linhas para 78 ocorrências). Extrair "linhas 498–695"
apanharia código alheio e deixaria usos de fora. O critério de extracção é a lista de 17
símbolos medida acima.

### D-2.5 — Promover o gate de sync a FAIL fica FORA desta fase
O D-06 da Fase 1 diz que a promoção de WARN para FAIL "pertence à Fase 2". Mas o gate só
pode passar a FAIL depois de a divergência real ser resolvida, e resolvê-la exige um
`git subtree pull` — que por sua vez exige um `git push` para o espelho GitHub. Push é
acção externa e **precisa de autorização explícita do utilizador**; o CLAUDE.md restringe
push ao fork pessoal e esta sessão não tem mandato para o fazer.
**Portanto:** esta fase deixa o gate em WARN e regista a dívida. Se o utilizador autorizar
o push+subtree pull, a promoção a FAIL é um follow-up de uma linha.

### D-2.6 — Não voltar a provar o critério 4
Está medido três vezes e commitado. O plano deve consumir essa evidência (citando
`counters_pre.tsv` e os commits), não gastar uma task a repeti-la. O que falta é o gate
que a torna repetível — e isso é a Fase 3.
</decisions>

<code_context>
## Ficheiros relevantes

| Caminho | Papel |
|---------|-------|
| `games/gow2/lift_baseline/gen_manifest.py` | gera e verifica o MANIFEST (tem `--verify`) |
| `games/gow2/lift_baseline/MANIFEST.tsv` | 244 marcadores: 98 TAG + 71 SYM + 75 GLOBAL |
| `games/gow2/verify_lift_baseline.sh` | o gate; hoje não cobre o preamble |
| `games/gow2/host_gow2_factory.cpp` | precedente de extracção bem-sucedida (13 funções) |
| `games/gow2/build_macos.sh` | **desactualizado** face ao irmão |
| `games/gow2/recomp_mid_v2/patch_zz_host_api_decls.py` | padrão para declarar protótipos no lift |
| `../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp` | onde o F2B vive hoje (gitignored) |

## Armadilhas verificadas

- **Build de testes:** `-DCMAKE_BUILD_TYPE=Release` injecta `-DNDEBUG` e desactiva
  `assert()` em silêncio. Usar `-DCMAKE_BUILD_TYPE=` vazio. Build pronto em `build-tests/`.
  Baseline: **21/21**.
- **`duplicate symbol`:** se o lift trouxer as definições dentro dele E o build compilar o
  ficheiro extraído, o link parte. O guard tem de procurar a **definição** (`) *{`), nunca
  o protótipo — um grep terminado em `$` nunca casa nenhum dos dois (bug real, corrigido
  hoje no irmão).
- **Fixtures SPU:** o `ctest` regenera `runtime/spu/tests/out_*/spu_recomp.[ch]`. Restaurar
  com `git checkout --`, nunca commitar.
- **Working tree alheio:** `.planning/codebase/TESTING.md` e `scripts/test_python.sh` estão
  modificados por outra sessão. Ficam fora de todos os commits.
</code_context>

<specifics>
## Critérios de aceitação (do ROADMAP, com o estado medido)

1. Inventário órfão todo versionado e compilável; três cifras reconciliadas; **gate cobre
   o preamble** — hoje não cobre. → POR FAZER
2. F2B com ficheiro versionado próprio, nome e responsabilidade declarados. → POR FAZER
3. `games/gow2/build_macos.sh` compila as fontes extraídas e liga `host_gow2_factory.cpp`,
   verificável por `nm`/log. → POR FAZER no monorepo (feito no irmão)
4. Lift novo sem patches + fontes versionadas → **linka**. → JÁ MEDIDO, só falta citar
</specifics>

<deferred>
## Adiado

- Promoção do gate de sync a FAIL (D-2.5) — bloqueada por precisar de push+subtree pull.
- Reconciliar o subtree `games/gow2/` com o irmão — mesma dependência.
</deferred>
