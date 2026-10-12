# Fase 8, Plano 08-02 — Veredicto

## Veredicto

`REFUTADA-AMBAS`

Nem a pista dos patches nem o candidato de runtime (`f6708cb`,
`ppu_register_committed_range()`/`vm_uncommitted()`) sobrevivem à medição do
BINÁRIO. Ver "Próximo passo" no fim.

## Ramo PATCHES — medido, não confirmado (build sequer completa para um dos lados)

Per a decisão do Plano 08-01 (`patches candidato`, diff não-vazio), construíram-se
DOIS lifts limpos NOVOS (`recomp_macos_v3ab_hoje`, `recomp_macos_v3ab_e6d65a2`,
51 991 funções cada, mesmo lift base) com os patches COMPLETOS de cada conjunto
(89 hoje / 74 `e6d65a2`, `--include-probe` para fidelidade ao pipeline real).

### `recomp_macos_v3ab_hoje` (patches de hoje, completo) — MEDIDO

`smoke_chain_gate.sh recomp_macos_v3ab_hoje 6 /tmp/gate_hoje.tsv`:

| run | st620 | startseq | nopic | thr_end | r_perma | elo_stopped | class |
|---|---|---|---|---|---|---|---|
| 1-6 | 11 | 1 | 0 | 0 | 0 | 2o movie (StartSeq) | REGRESSAO |

`elo_stopped=nenhum em 0 de 6` (limiar: 4). **Reproduz exactamente a mesma
falha da produção**, mesmo partindo de um lift completamente limpo (não a
`recomp_macos_v2` desactualizada) — o binário constrói e liga sem erros.

### `recomp_macos_v3ab_e6d65a2` (patches de `e6d65a2`, completo) — NÃO MEDIDO, falha ao compilar

`smoke_chain_gate.sh recomp_macos_v3ab_e6d65a2 6 /tmp/gate_e6d65a2.tsv` abortou
no próprio `build_macos.sh` (rc=1), **antes de produzir um binário**:

```
ppu_recomp_000.cpp:167672:28: error: use of undeclared identifier 'ps3_timebase_now';
  did you mean 'ppu_timebase_now'?
ppu_recomp_002.cpp:19563:15: error: use of undeclared identifier 'ppu_giant_lock_release'
ppu_recomp_002.cpp:19564:15: error: use of undeclared identifier 'usleep'
ppu_recomp_002.cpp:19598:11: error: unknown type name 'jmp_buf'
```

Causa: o conjunto de patches de `e6d65a2` (74 patches, aplicados com
`--status`: 23 APPLIED, **38 FAILED**, 4 FAILED-PARTIAL, 2 NO-MATCH, 7
UNVERIFIED) depende de declarações/nomes (`ps3_timebase_now` — renomeado
para `ppu_timebase_now` numa sessão posterior — e os `#include`/`extern`
de giant-lock que outros patches, ausentes em `e6d65a2`, instalam) que já não
existem na forma esperada. **Isto é deriva de forma do lift/patches entre
épocas, não uma diferença de comportamento em tempo de execução** — já
identificado como risco de confundimento na nota do Plano 08-01
(`2026-07-30-fase8-patches-ab-real.md`, secção "Confundimento medido").

### Conclusão do ramo patches

Não se pode dizer "`gate_hoje` reprova e `gate_e6d65a2` aprova" nem "os dois
dão o mesmo resultado" no sentido literal do plano — `gate_e6d65a2` nunca
produziu um binário para medir. Na prática, isto equivale ao caso "mesmo
resultado" (nenhum dos dois lados prova o boot): `hoje` falha exactamente como
a produção, e `e6d65a2` falha ainda mais cedo (nem compila), por uma razão
comprovadamente não relacionada com o timing de `vm_commit`/`vm_read`/`vm_write`.
**Não há evidência de binário a favor da pista dos patches.** Combinado com a
nota do 08-01 (nenhum patch, em nenhuma das duas eras, toca
`func_00147038`/`thr_auto_load`), a pista dos patches está **REFUTADA por
medição do binário**, apesar do diff não-vazio do 08-01.

Per o plano, prossegue-se sem saltar para o candidato de runtime.

## Ramo RUNTIME — medido, refutado

Candidato: `runtime/ppu/ppu_loader.cpp` — commit `f6708cb` (merge `88dc92a`,
25 Jul 18:40:56), 3m49s depois de `boot_gow2.pre_v3` (último binário OK,
18:23). Adiciona `committed_lock()`/`committed_unlock()` (spinlock) e troca a
publicação/leitura de `g_committed_count` por `__atomic_store_n`/
`__atomic_load_n` em `ppu_register_committed_range()`/`vm_uncommitted()`.

### Procedimento

1. `git worktree add /tmp/ps3recomp_diag08_rt HEAD` (a partir de `ps3recomp`,
   isolado do checkout principal).
2. Confirmado por `grep -n` fresco dentro do worktree: linhas
   `g_committed_count:500`, `committed_lock:527`, `committed_unlock:534`,
   `ppu_register_committed_range:538`, `vm_uncommitted:586` — coincide com a
   evidência da sessão de planeamento.
3. `patch_diag08_committed_range_revert_test.py` (novo, neste plano): reverte
   cirurgicamente os 6 sítios introduzidos por `f6708cb` — 1×
   `committed_lock()`, 3× `committed_unlock()` (removidos, deixando as
   funções definidas mas não chamadas), 1× `__atomic_store_n(...,
   __ATOMIC_RELEASE)` → `g_committed_count = slot + 1;`, 1×
   `__atomic_load_n(..., __ATOMIC_ACQUIRE)` → `g_committed_count` simples.
   Guarda anti-produção: recusa correr fora de `/tmp/ps3recomp_diag08_rt`.
   Contagem exacta confirmada (6/6), sem "fixed 0" silencioso.
4. Rebuild só da lib runtime no worktree: `cmake -B build-macos -G Ninja
   -DCMAKE_BUILD_TYPE=Release . && cmake --build build-macos -j10` — **127
   objectos, sem erros** (só avisos pré-existentes não relacionados).
5. Relink (sem `FORCE_REBUILD_LIFT`, reaproveitando os `.o` já compilados de
   `recomp_macos_v3`): `PS3_ENGINE_ROOT=/tmp/ps3recomp_diag08_rt
   OUT=./boot_gow2_diag08_rt ./build_macos.sh recomp_macos_v3` — relink puro,
   segundos (os 7 chunks e os `.o` de runtime PPU já estavam em cache; só a
   `libps3recomp_runtime.a` do worktree revertido é nova).
   Nota operacional: o `.venv` não existe dentro do worktree (não é versionado
   em git) — `gen_hle_nids.py` é invocado via `$PS3_ENGINE_ROOT/.venv/bin/python`;
   resolvido com um symlink `/tmp/ps3recomp_diag08_rt/.venv ->
   .../ps3recomp/.venv` (não altera nenhum ficheiro-fonte, só o ambiente de
   invocação).
6. Medição: `./bisect_regression.sh --bin ./boot_gow2_diag08_rt`.

### Resultado medido

```
bin                    build_date     log_lines  startseq  thr_auto_load_end  r_perma_full  replay_nopic  class
boot_gow2_diag08_rt    31 Jul 08:43   3239       1         0                  0             0             REGRESSAO
```

Log (`/tmp/bisect_regression_boot_gow2_diag08_rt_20260731_084342.log`): preso
em `[MOVIEFSM] st620 1 -> 1` repetido — a MESMA assinatura da regressão
conhecida (produção, e `recomp_macos_v3ab_hoje` acima). **Reverter o
spinlock/atomic de `f6708cb` NÃO repõe o boot.**

### Conclusão do ramo runtime

`f6708cb` **REFUTADO** como causa da regressão — a reversão cirúrgica,
isolada (só a lib runtime mudou; lift e patches ficaram idênticos ao
`recomp_macos_v3` usado como base), não alterou o resultado. A concorrência
em `ppu_register_committed_range()`/`vm_uncommitted()` pode ainda ser um bug
real (a análise do commit original é sólida), mas não é O bug que produz esta
regressão específica.

## Funil completo de causas eliminadas (Fase 6 + Fase 8)

| Candidato | Fase | Método | Resultado |
|---|---|---|---|
| `truncated-bounds repair` | 6 | A/B de contagem | REFUTADO (adiciona 78 funções, não remove; nem existia no lift `v3`) |
| `invalid_instructions` (config do lifter) | 6 | leitura do commit + `build_macos.sh:77` | REFUTADO (no-op, lift sem `--config`) |
| Sub-emissão da cadeia `CD7B4` | 6 | contagem de chamadas | REFUTADO (19 no lift que falha vs 8 no que funciona — sobre-emitida, não sub-emitida) |
| TOCFIX / `ctx->lr` (`func_00147038`) | 6 | reversão real + rebuild | REFUTADA-EM-COMBINAÇÃO (produz a mesma REGRESSAO) |
| `strip_block` | 6 | (ver `2026-07-30-fase6-veredictos.md`) | REFUTADA |
| Regressão comportamental do lifter perto de `thr_auto_load` | 6 | comparação byte-a-byte de 3 níveis | MEDIDA-MAS-NÃO-CONFIRMADA (contagem cresce monotonicamente, vizinhança idêntica) |
| **Patches** (diff hoje vs `e6d65a2`) | 8 (08-01/08-02) | A/B de diff + medição de binário | **REFUTADA** — `hoje` completo falha como a produção; `e6d65a2` completo nem compila (deriva de nomes não relacionada); nenhum patch toca `thr_auto_load` em nenhuma das eras |
| **Runtime `f6708cb`** (`ppu_register_committed_range`/`vm_uncommitted`) | 8 (08-02) | reversão cirúrgica isolada + medição | **REFUTADO** — reversão não repõe o boot |

## Próximo passo recomendado

Bissecção automatizada do resto de `runtime/`+`libs/` dentro da janela exacta
da regressão (`8648805`→`4e815cf`, 25 Jul 17:53 → 26 Jul 02:59), usando
`bisect_regression.sh --bin` como oráculo, **commit a commit** (não só os dois
commits que tocam `ppu_loader.cpp` — a janela inteira, incluindo outros
ficheiros de `runtime/`/`libs/` que possam ter mudado no mesmo intervalo),
com `git worktree` isolado para cada candidato (mesmo padrão usado aqui,
barato: ~1 min de rebuild da lib + relink por candidato, já demonstrado
funcional). Isto ainda NÃO foi tentado nesta fase nem na Fase 6 — a Fase 6
olhou para o lifter e o `.cpp` liftado; este plano olhou para patches e para
os dois únicos commits que tocam `ppu_loader.cpp` na janela. O resto de
`runtime/`+`libs/` na mesma janela continua por eliminar.

## Limpeza de artefactos intermédios

`git worktree remove /tmp/ps3recomp_diag08_rt` executado (REFUTADA-AMBAS —
runtime não é a causa confirmada, worktree não precisa de sobreviver para o
Plano 08-03). `rm -rf` de: `recomp_macos_v3ab_hoje`, `recomp_macos_v3ab_e6d65a2`,
`boot_gow2_v3ab_hoje`, `boot_gow2_v3ab_e6d65a2`, `boot_gow2_diag08_rt`, e os
directórios `/tmp` intermédios (`lift_ab_*`, `patchdir_*_full`,
`status_full_*.tsv`, `gate_*_full.log`) — nenhum é necessário para o Plano
08-03 dado que nenhuma causa foi confirmada.
