# RDY-0 — Desbloquear o re-lift do GoW2

> **Para agentes executores:** SUB-SKILL OBRIGATÓRIA — use `superpowers:subagent-driven-development`
> (recomendado) ou `superpowers:executing-plans`. Os passos usam checkbox (`- [ ]`) para tracking.
> Plano-mestre: [`docs/PLAN_READY_FOR_GAME_RECOMPILATION.md`](../../PLAN_READY_FOR_GAME_RECOMPILATION.md) §3, track RDY-0.

**Objectivo:** fazer com que um re-lift limpo do EBOOT produza um `boot_gow2` funcionalmente
equivalente ao actual, sem nenhuma edição manual — hoje isso é impossível e o binário de
referência nem sequer é comparável.

**Arquitectura:** três frentes independentes que convergem num único aceite. (1) **Motor:** repor
as emissões do lifter que um reset ao upstream apagou. (2) **Código host órfão:** tirar de dentro
do lift gitignored tudo o que é código nosso e pô-lo em ficheiros versionados compilados à parte.
(3) **Catálogo:** transformar 71 scripts de reaplicação num gate que falha de verdade. O
encadeamento `re-lift → patches → build → smoke` passa a existir, com o lift novo escrito num
directório novo e promovido só depois do smoke passar.

**Stack:** Python ≥3.11 (`$PS3/.venv/bin/python` = 3.14.6), clang++ 20, bash, `ppu_lifter.py`,
`apply_all_patches.sh`, `count_menu_gate.py`.

## Restrições globais

- **Nunca commitar dados de jogo.** `EBOOT.ELF`, `extracted/`, `recomp_*/` são gitignored e assim
  ficam. Nunca `git add -A`. O que este plano versiona é **código host nosso**, não output do lift.
- **Commits em português, sem `Co-Authored-By`.** Push só para `origin` (gow2-recomp) e `personal`
  (ps3recomp). Nunca `sp00nznet`.
- **Dois repos.** Alterações ao motor → `ps3recomp`. Alterações ao port → `gow2-recomp`. Nunca
  misturar num commit.
- **Matar sempre por PID** (`kill -TERM` → `kill -9`). Nunca `pkill -f boot_gow2`.
- **Probes gated por env var, OFF por default.**
- **Nada de `git clean -fdx` em `gow2-recomp`** enquanto este plano não fechar: destrói 4,4 GB de
  lifts e, com eles, a única cópia de 21 comportamentos.

## Decisões fixadas (não reabrir durante a execução)

| Decisão | Valor | Porquê |
|---|---|---|
| `--code-end` no lift canónico | **Não usar** | mantém o conjunto de símbolos do lift de produção (incl. 418 refs a `func_FFFFFFF4`), para o baseline continuar comparável |
| Recipe de aceite | **braço M0 headless**, `PS3_MOVIE_EOS=0`, `PS3_NO_RSX=1`, `PS3_PERF_FSM=1`, probes OFF, 25 s | único gate DURO existente (`smoke_intro_vdec_wad.sh:80-84`); corre sem sessão gráfica |
| Nº de corridas | **6 por lado** (pré e pós) | `st620` mede-se não-determinístico entre 1/8 e 6/6 sem mudança de código |
| Patches de PROBE (19) | **manter, fora do gate** | continuam como instrumentação gated; classe `PROBE` no catálogo, não contam para `FAILED=0` |
| Destino do re-lift | **directório novo + promoção** | `recomp_macos_v2/` é a única cópia de 21 comportamentos; o lifter apaga `ppu_recomp_*.cpp` do dir de saída (`tools/ppu_lifter.py:4006-4013`) |
| Engine root canónico | **`$PS3` = este checkout** (`macos-arm64-port-f0-f2`) | o `boot_gow2` actual foi linkado contra `../ps3recomp-bringup`; ver Task 1 passo 6 |

---

## Estrutura de ficheiros

**Criar (gow2-recomp)**
- `lift_baseline/injected_<NNN>.cpp` — as linhas de host injectadas em cada chunk, congeladas
- `lift_baseline/MANIFEST.tsv` — chunk, nº de linhas injectadas, md5, primeiro `^void func_`
- `lift_baseline/counters_pre.tsv` — contadores do baseline (uma linha por métrica)
- `verify_lift_baseline.sh` — compara o lift actual contra o congelado; é o vermelho/verde do plano
- `host_gow2_f2b_stream.cpp` — o subsistema F2B (~197 linhas) que hoje só existe no lift
- `smoke_relift_equiv.sh` — agregador N-de-M do aceite
- `recomp_mid_v2/patch_strip_host_block.py` — remove do lift os blocos que passaram a ficheiro host
- `recomp_mid_v2/patch_host_factory_callsites.py` — repõe os call sites + declarações
- `recomp_mid_v2/PATCH_CATALOG.tsv` — triagem dos 71, fechada, sem resto

**Modificar (gow2-recomp)**
- `host_gow2_factory.cpp` — cabeçalho errado (:7-9), `-Wcomment` (:43), 30 linhas redundantes, statics
- `apply_all_patches.sh` — snapshot dos 31 chunks (:120), status por patch, `run_checks` do glob
- `build_macos.sh` — `RELIFT=1` (entre :44 e :46), objectos host (:113-130 e :211-227), gate `lifter-rev`
- `count_menu_gate.py` — campos novos, mesmos nomes
- `recomp_mid_v2/patch_cc9d0_live_yield.py` — único dos 71 que ignora `sys.argv` (:25-27)
- `recomp_mid_v2/patch_{14a01c,2b0fb4,39e5d8}_opd.py` — falham quando `sites == 0`
- `recomp_mid_v2/patch_{2b3d1c_movie_io,fios_f2a_f2b_wad}.py` — `return 0` incondicional
- `.gitignore` — `recomp_pre/` (1,4 GB untracked e não ignorado) e o padrão `recomp_*/`

**Modificar (ps3recomp)**
- `tools/ppu_lifter.py` — repor `#include <stdlib.h>`, ramo `__APPLE__` do TLS, geração de `ppu_stubs.cpp`
- `runtime/ppu/tests/test_ppu_lifter_emit.py` — golden do preâmbulo que falha se voltarem a sair

---

## Task 1: Congelar o baseline forense

**Porque é primeiro:** `recomp_macos_v2/` contém 1603 linhas de host escritas à mão e é a **única
cópia** de 21 comportamentos. Qualquer passo seguinte pode destruí-las. Nada mais começa antes disto.

**Files:**
- Create: `gow2-recomp/lift_baseline/` (MANIFEST.tsv, counters_pre.tsv, injected_*.cpp)
- Create: `gow2-recomp/verify_lift_baseline.sh`
- Modify: `gow2-recomp/.gitignore`

**Interfaces:**
- Produz: `verify_lift_baseline.sh [LIFT_DIR]` → exit 0 se todas as linhas injectadas do manifesto
  estão presentes no lift; exit 1 e lista o que falta. Consumido pela Task 9 e pelo aceite final.

- [ ] **Passo 1: Registar o estado exacto do que vai ser congelado**

```bash
cd ~/Documents/PESSOAL/gow2-recomp
md5 -q boot_gow2
stat -f '%Sm %N' boot_gow2 recomp_macos_v2/ppu_recomp_001.cpp recomp_macos_v2/ppu_loader.o
grep -c 'lifter-rev' recomp_macos_v2/ppu_recomp_000.cpp   # esperado: 0 (lift anterior a bc3707d)
```

Esperado: `grep` devolve 0 — é a prova de que o lift de produção é anterior ao carimbo.

- [ ] **Passo 2: Extrair as linhas injectadas de cada chunk**

O preâmbulo de `ppu_recomp_002.cpp` (151 linhas) tem **zero** injecções — serve de referência do
preâmbulo puro do lifter. Para cada chunk, o injectado é o diff contra essa referência.

```bash
mkdir -p lift_baseline
ref=$(awk '/^void func_/{print NR-1; exit}' recomp_macos_v2/ppu_recomp_002.cpp)
sed -n "1,${ref}p" recomp_macos_v2/ppu_recomp_002.cpp > /tmp/pre_ref.txt
for f in recomp_macos_v2/ppu_recomp_*.cpp; do
  n=$(basename "$f" .cpp | sed 's/ppu_recomp_//')
  k=$(awk '/^void func_/{print NR-1; exit}' "$f")
  sed -n "1,${k}p" "$f" > /tmp/pre_$n.txt
  diff /tmp/pre_ref.txt /tmp/pre_$n.txt | grep '^>' | cut -c3- > "lift_baseline/injected_$n.cpp"
done
wc -l lift_baseline/injected_*.cpp | tail -1
```

Esperado: total **1603** linhas, concentradas em `injected_000.cpp` (698), `injected_001.cpp` (897)
e `injected_003.cpp` (8). Os restantes 28 ficheiros ficam vazios — apagá-los.

- [ ] **Passo 3: Escrever o MANIFEST.tsv**

Uma linha por chunk com injecção: `chunk`, `linhas`, `md5`, `primeiro_func_linha`. Mais uma secção
de marcadores nomeados — os 21 que não têm escritor — um por linha, com o chunk e a linha onde
aparecem. Lista mínima obrigatória (medida): `F2B-STREAM-ENSURE`, `F2B-STREAM-EOF-DONE`,
`F2B-STREAM-CLAMP`, `F2B-STREAM-FILL`, `F2B-STREAM-PUMP`, `F2B-FO-CTOR`, `F2B-FO-SIZE`,
`F2B-KEEP-DONE`, `F2B-RESTATUS`, `PS3_FACT_SNAP_MAX`, `AREAD-HLE`, `g_b71_product_reused`,
`PS3_TYPE15_CB56C`, `FREELIST-TAG-GUARD`, `ALLOC-NULL-GUARD`, `[FACTORY]`, `[WADLD-ALLOC]`,
`[BA808]`, `WADLD-T1SZ`, `g_wadld_eof_ea`, `f2b_fo_mfd_put`.

- [ ] **Passo 4: Escrever `verify_lift_baseline.sh` e vê-lo passar a VERDE hoje**

O script lê o MANIFEST e, para cada marcador, faz `grep -l` no chunk indicado do lift passado como
argumento. Falha com a lista dos ausentes.

```bash
./verify_lift_baseline.sh recomp_macos_v2 ; echo "RC=$?"
```

Esperado: `RC=0` — verde contra o lift de produção. Se der vermelho aqui, o manifesto está errado;
corrigir antes de avançar.

- [ ] **Passo 5: Provar que o script discrimina (o vermelho)**

```bash
mkdir -p /tmp/lift_vazio && for i in 000 001 003; do
  : > /tmp/lift_vazio/ppu_recomp_$i.cpp; done
./verify_lift_baseline.sh /tmp/lift_vazio ; echo "RC=$?"
```

Esperado: `RC=1` e a lista dos 21 marcadores em falta. **Um script que não sabe falhar não é gate.**

- [ ] **Passo 6: Fixar o engine root e regravar o binário de referência**

O `boot_gow2` actual foi linkado contra outra worktree — prova:

```bash
nm -m recomp_macos_v2/ppu_loader.o | grep -E 'checkpoint_fire|mem_oob_report'
grep -rn 'ps3_checkpoint_fire' ~/Documents/PESSOAL/ps3recomp --include=*.c --include=*.cpp
```

Esperado: dois símbolos `(undefined) external` no `.o` e **zero** hits no monorepo. Reconstruir com
`PS3_ENGINE_ROOT=$PS3` explícito; se o link falhar por esses dois símbolos, acrescentar stubs
fracos em `gow2-recomp/host_gow2_stubs.c` (no-op, gated) e **declarar isso no commit** — não
cherry-pickar da worktree `bringup` neste plano.

- [ ] **Passo 7: Gravar os contadores do baseline (6 corridas)**

Ver Task 8 para o agregador; aqui basta a forma manual, porque o agregador ainda não existe:

```bash
for i in 1 2 3 4 5 6; do
  sysctl -n vm.swapusage >> lift_baseline/swap.txt
  ( set -a; . ./env_gow2.sh; set +a
    unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
    export PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
    unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
    exec ./boot_gow2 EBOOT.ELF ) > /tmp/rdy0_pre_$i.log 2>&1 & BPID=$!
  sleep 25; kill -TERM $BPID 2>/dev/null; sleep 1; kill -9 $BPID 2>/dev/null
  grep -oE '\[MOVIEFSM\] st620 [0-9]+ -> [0-9]+' /tmp/rdy0_pre_$i.log | tail -1
done
```

Gravar em `counters_pre.tsv`: por corrida, `st620_max`, `function_table_count`, `imports`,
`modules`, `R_PermA_bytes_read`, `swap_mb`. Valores de referência já medidos: `56072` funções,
`151 imports / 13 modules`, `R_PermA bytes_read == size == 20169344`.

- [ ] **Passo 8: Fechar o buraco do `.gitignore` e commitar**

`recomp_pre/` são 1,4 GB untracked **não ignorados** (`git check-ignore -v recomp_pre` → rc=1).

```bash
cd ~/Documents/PESSOAL/gow2-recomp
printf 'recomp_pre/\nrecomp_macos_v3/\n' >> .gitignore
git add .gitignore lift_baseline verify_lift_baseline.sh
git commit -m "rdy0: congela o baseline forense do lift antes do re-lift

Extrai as 1603 linhas de host injectadas nos preambulos (698 no chunk 000,
897 no 001, 8 no 003) e os 21 marcadores sem script escritor para
lift_baseline/, versionado. verify_lift_baseline.sh passa contra o lift de
producao e falha contra um lift vazio.

recomp_pre/ (1,4 GB) estava untracked e fora do .gitignore."
```

---

## Task 2: Repor as emissões do lifter que o `11a1c3c` apagou

**Files:**
- Modify: `ps3recomp/tools/ppu_lifter.py` (preâmbulo em `_preamble_lines()`, :3036-3053)
- Modify: `ps3recomp/runtime/ppu/tests/test_ppu_lifter_emit.py`

**Interfaces:**
- Produz: preâmbulo com `#include <stdlib.h>`, ramo `__APPLE__` para `g_trampoline_fn`, e
  `ppu_stubs.cpp` escrito por `write_source_files()`. Consumido pela Task 9 (`RELIFT=1`).

- [ ] **Passo 1: Ver o que o commit apagou**

```bash
cd ~/Documents/PESSOAL/ps3recomp
git show 11a1c3c -- tools/ppu_lifter.py | grep -E '^-.*(stdlib\.h|__APPLE__|thread_local|ppu_stubs)'
sed -n '371p' tools/ppu_lifter.py    # hoje: __declspec(thread) — sintaxe MSVC em POSIX
```

- [ ] **Passo 2: Escrever os asserts que falham**

Em `runtime/ppu/tests/test_ppu_lifter_emit.py`, um teste por emissão:

```python
def test_preambulo_tem_stdlib_e_tls_apple():
    pre = "\n".join(_preamble_lines_for_test())
    assert "#include <stdlib.h>" in pre
    assert "__APPLE__" in pre and "thread_local" in pre
    assert "__declspec(thread)" not in pre or "_MSC_VER" in pre

def test_write_source_files_emite_ppu_stubs():
    out = _run_lifter_on_tiny_elf(tmp_path)
    assert (out / "ppu_stubs.cpp").exists()
    assert "func_FFFFFFF4" in (out / "ppu_stubs.cpp").read_text()
```

- [ ] **Passo 3: Correr e confirmar que falha**

```bash
ctest --test-dir <build> -R py.ppu_lifter_emit --output-on-failure
```

Esperado: FAIL nos três asserts novos.

- [ ] **Passo 4: Repor as emissões**

O `#include <stdlib.h>` e o ramo `__APPLE__` vêm de `0cf3549`/`527156b`; a geração do
`ppu_stubs.cpp` vem de `e52da4a`. Reaplicar os hunks, não reverter o commit inteiro (`11a1c3c`
trouxe também melhorias reais do upstream).

```bash
git show 0cf3549 -- tools/ppu_lifter.py
git show 527156b -- tools/ppu_lifter.py
git show e52da4a -- tools/ppu_lifter.py
```

- [ ] **Passo 5: Correr e confirmar que passa**

```bash
ctest --test-dir <build> -R py.ppu_lifter_emit --output-on-failure
```

Esperado: PASS. Os testes existentes continuam verdes.

- [ ] **Passo 6: Commit**

```bash
git add tools/ppu_lifter.py runtime/ppu/tests/test_ppu_lifter_emit.py
git commit -m "fix(lifter): repoe stdlib.h, TLS __APPLE__ e ppu_stubs.cpp apagados no 11a1c3c

O reset das tools ao upstream removeu tres emissoes de que o lift macOS
depende: sem elas um re-lift limpo nao compila (declspec(thread) em POSIX)
nem linka (418 refs a func_FFFFFFF4 sem ppu_stubs.cpp).

Golden no test_ppu_lifter_emit para o proximo reset nao as apagar em silencio."
```

---

## Task 3: Extrair o subsistema F2B para ficheiro versionado

**Files:**
- Create: `gow2-recomp/host_gow2_f2b_stream.cpp`
- Modify: `gow2-recomp/lift_baseline/MANIFEST.tsv` (os marcadores F2B passam a ter casa)

**Interfaces:**
- Produz: `f2b_stream_ensure()`, `f2b_stream_eof_try_complete()`, `g_wadld_eof_ea` (todos
  `extern "C"`), mais 5 helpers `static`. Consumido pela Task 4 (link) e Task 5 (call sites).

- [ ] **Passo 1: Delimitar o bloco na origem**

```bash
cd ~/Documents/PESSOAL/gow2-recomp
sed -n '33,255p' recomp_macos_v2/ppu_recomp_001.cpp | head -30
nm -g recomp_macos_v2/ppu_recomp_001.cpp.o | grep -E 'f2b_|wadld_eof'
```

Esperado: `T _f2b_stream_ensure`, `T _f2b_stream_eof_try_complete`, `S _g_wadld_eof_ea`.

- [ ] **Passo 2: Copiar literalmente para o novo ficheiro**

Mesmo método do `host_gow2_factory.cpp` (cópia byte-fiel + includes adaptados). O corte é
`:33-255` (197 linhas úteis: 2 funções `extern "C"`, 1 global, 5 static, 10 globais de estado).

- [ ] **Passo 3: Provar que compila e exporta o esperado**

```bash
clang++ -std=c++20 -c -I ~/Documents/PESSOAL/ps3recomp/include \
        -I ~/Documents/PESSOAL/ps3recomp/runtime/ppu \
        host_gow2_f2b_stream.cpp -o /tmp/f2b.o && nm -g /tmp/f2b.o | grep ' T \| S '
```

Esperado: exactamente `T _f2b_stream_ensure`, `T _f2b_stream_eof_try_complete`, `S _g_wadld_eof_ea`.
Undefined aceitáveis: só `vm_*` e `fprintf`.

- [ ] **Passo 4: Registar os 3 call sites cross-chunk que ninguém cobre**

`g_wadld_eof_ea` é lido em `ppu_recomp_002.cpp:223381`, `:223430` e `ppu_recomp_005.cpp:53136`, cada
um com o seu `extern uint32_t g_wadld_eof_ea;` local, e **zero** patches os mencionam. Acrescentar
os três ao MANIFEST como marcadores obrigatórios (Task 5 escreve-os).

- [ ] **Passo 5: Commit**

```bash
git add host_gow2_f2b_stream.cpp lift_baseline/MANIFEST.tsv
git commit -m "rdy0: extrai o subsistema F2B stream do lift para ficheiro versionado

197 linhas que so existiam em recomp_macos_v2/ppu_recomp_001.cpp:33-255
(gitignored): f2b_stream_ensure, f2b_stream_eof_try_complete, g_wadld_eof_ea
e 5 helpers. Nenhum dos 71 patch_*.py sabia repo-las."
```

---

## Task 4: Higienizar e ligar os dois ficheiros host ao build

**Files:**
- Modify: `gow2-recomp/host_gow2_factory.cpp`
- Create: `gow2-recomp/recomp_mid_v2/patch_strip_host_block.py`
- Modify: `gow2-recomp/build_macos.sh` (etapa 2, :113-130; link, :211-227)

**Interfaces:**
- Consome: `host_gow2_f2b_stream.cpp` (Task 3).
- Produz: `$LIFT/host_gow2_factory.o` e `$LIFT/host_gow2_f2b_stream.o` na linha de link;
  `patch_strip_host_block.py <LIFT>` idempotente que remove do lift os blocos duplicados.

- [ ] **Passo 1: Provar o vermelho — símbolo duplicado**

Compilar e linkar com o bloco ainda dentro do lift:

```bash
clang++ -std=c++20 -c host_gow2_factory.cpp -o /tmp/hgf.o -I ...
# adicionar /tmp/hgf.o à linha de link do build e correr
```

Esperado: `duplicate symbol '_ps3_type15_block_stomp'` (e mais 12). **É este o vermelho.**

- [ ] **Passo 2: Higienizar o ficheiro versionado**

Quatro correcções medidas, todas no mesmo commit:
1. `:43` — comentário duplicado gera `-Wcomment` (`'/*' within block comment`).
2. `:7-9` — o cabeçalho diz "~780 linhas" e "9 funções"; são **700 linhas extraídas e 13 funções**.
3. `:704-729` — `__builtin_clz/clzll` e `ppc_mulhd/mulhdu` são cópia do preâmbulo que o lifter
   gera (`tools/ppu_lifter.py:239-269`). 30 linhas mortas — remover, senão divergem sem ninguém notar.
4. `:738` e `:740-742` — `extern ps3_timebase_now` não usada e comentário órfão do corte em 957.

- [ ] **Passo 3: Decidir as 7 funções `static` de probe**

`wavdrv_on/cstr` e `snd_probe_on/ea_ok/cstr/scan/opd` estão no ficheiro extraído mas os 12 call
sites (`3+5+4`) só existem no lift — `static` não atravessa TU. **Decisão:** saem do
`host_gow2_factory.cpp` e ficam no lift, porque são regeneráveis (`patch_wavdrv_probe.py` e
`patch_snd_open_probe.py` escrevem a definição completa). Verificar antes de remover:

```bash
grep -cE 'write_text|\.write\(' recomp_mid_v2/patch_snd_open_probe.py   # esperado: >=1
grep -c 'snd_probe_scan(' recomp_macos_v2/ppu_recomp_001.cpp            # esperado: 5
```

- [ ] **Passo 4: Escrever o `patch_strip_host_block.py`**

Idempotente, com marcador de início/fim, aceita o dir de lift em `sys.argv[1]`, e **falha
(`rc != 0`) se remover 0 blocos num lift que os tem** — o padrão inverso dos três patches
falso-verde que a Task 6 corrige.

- [ ] **Passo 5: Ligar ao build**

Em `build_macos.sh`, junto ao bloco de fontes do runtime (`:113-130`), compilar os dois ficheiros;
e acrescentá-los à lista explícita de objectos do link (`:211-217`). O ciclo
`host_gow2_factory.o → vm_write32 → ppu_loader.o → ps3_type15_block_stomp` já existia dentro da
mesma TU; passa a cross-TU, o que no macOS resolve independentemente da ordem — mas fixar a ordem
na lista explícita para o dia em que virar `.a`.

- [ ] **Passo 6: Verde — linka e o binário exporta os símbolos**

```bash
./build_macos.sh && nm -g boot_gow2 | grep -cE 'ps3_(factory|type15)_|f2b_stream'
```

Esperado: build limpo (0 duplicados) e ≥ 15 símbolos. Depois, o smoke estrutural:

```bash
./smoke_boot_mac.sh 30 ; echo "RC=$?"
```

Esperado: `RC=0`.

- [ ] **Passo 7: Commit**

```bash
git add host_gow2_factory.cpp host_gow2_f2b_stream.cpp build_macos.sh \
        recomp_mid_v2/patch_strip_host_block.py
git commit -m "rdy0: liga os ficheiros host versionados ao build e remove os duplicados do lift

host_gow2_factory.cpp (742 linhas, 1f651aa) e host_gow2_f2b_stream.cpp
passam a ser compilados e linkados; patch_strip_host_block.py remove as
definicoes correspondentes do lift para nao haver simbolo duplicado.

Higiene: cabecalho corrigido (13 funcoes, nao 9), -Wcomment em :43,
30 linhas duplicadas do preambulo do lifter, decl morta de ps3_timebase_now.
As 7 probes static ficam no lift (patch_snd_open_probe/wavdrv escrevem-nas)."
```

---

## Task 5: Writer dos call sites factory/TYPE15

**Files:**
- Create: `gow2-recomp/recomp_mid_v2/patch_host_factory_callsites.py`

**Interfaces:**
- Consome: os símbolos de `host_gow2_factory.cpp` e `host_gow2_f2b_stream.cpp` (Task 4).
- Produz: reposição de 10 call sites + 10 declarações forward num lift fresco.

**Porquê:** ligar as definições não repõe as **chamadas**. Os dois patches que "cobrem" a área
(`patch_factory_snap_ty15_pin.py`, `patch_type15_cb56c_product.py`) são verificadores puros — zero
escritas. Sem esta task, `RELIFT=1` entrega um boot sem o pin de TYPE15 e o smoke regride.

- [ ] **Passo 1: Enumerar os sítios exactos**

```bash
cd ~/Documents/PESSOAL/gow2-recomp
grep -nE '\b(ps3_factory_[a-z_]+|ps3_type15_[a-z0-9_]+)\(' recomp_macos_v2/ppu_recomp_000.cpp
grep -nE '\b(ps3_factory_[a-z_]+|ps3_type15_[a-z0-9_]+)\(' recomp_macos_v2/ppu_recomp_003.cpp
awk 'NR>1041' recomp_macos_v2/ppu_recomp_001.cpp | grep -nE 'ps3_(factory|type15)_'
```

Esperado: chunk 000 → decls `:3-8` e `:549786`, calls `:143929`, `:143932`, `:143953`, `:162325`,
`:162439`, `:549790`; chunk 003 → decls `:3-4`, call `:402428`; chunk 001 pós-preâmbulo → calls
`:257752`, `:257848`, `:257855` + decl local `:257852`. Mais os 3 de `g_wadld_eof_ea` (Task 3).

- [ ] **Passo 2: Escrever o teste primeiro**

Um caso por call site: dado um chunk sem a chamada, o patch insere-a na função certa; corrido duas
vezes, o ficheiro fica igual (idempotência). E: **`rc != 0` se converter 0 sítios.**

- [ ] **Passo 3: Correr contra o lift de produção — deve dar ALREADY, não APPLIED**

```bash
$PS3/.venv/bin/python recomp_mid_v2/patch_host_factory_callsites.py recomp_macos_v2 ; echo "RC=$?"
```

Esperado: `RC=0`, "já aplicado", ficheiro inalterado (o lift de produção já tem os sítios).

- [ ] **Passo 4: Resolver o bloco `[FACTORY]` sem gate — viola a regra 6 do CLAUDE.md**

Dos 96 tags presentes nos chunks patcheados, três não aparecem em nenhum `patch_*.py`: `[FACTORY]`
(9 sítios), `[WADLD-ALLOC]` (1) e `[BA808]` (1). O `[FACTORY]` **não é só probe**:
`recomp_macos_v2/ppu_recomp_001.cpp:353-378` faz `vm_write32` na memória guest **sem gate de env**.

```bash
sed -n '353,378p' recomp_macos_v2/ppu_recomp_001.cpp | grep -n 'vm_write32\|getenv'
```

Decisão a tomar aqui, não adiar: ou a alteração entra no `host_gow2_factory.cpp` já ligado ao build
**com gate declarado** (`getenv`, OFF por default), ou é declarada intencional e permanente com
justificação no commit. O que não pode ficar é alteração funcional de memória guest sem script,
sem gate e invisível a um re-lift.

- [ ] **Passo 5: Correr contra um lift fresco — deve escrever**

Depende da Task 9 existir. Até lá, validar contra uma cópia do chunk com os sítios removidos à mão
no scratchpad.

- [ ] **Passo 6: Commit**

```bash
git add recomp_mid_v2/patch_host_factory_callsites.py
git commit -m "rdy0: writer dos 10 call sites e 10 declaracoes do API factory/TYPE15

Nenhum dos 71 patches escrevia estes sitios: os dois que cobrem a area sao
verificadores puros. Sem este writer, um re-lift entrega um boot sem o pin
de TYPE15 e sem as chamadas de f2b/g_wadld_eof_ea."
```

---

## Task 6: Endurecer o catálogo — matar os falsos-verdes

**Files:**
- Modify: `gow2-recomp/apply_all_patches.sh` (:113-127 snapshot, :146-233 checks, :263-299 loop)
- Modify: `recomp_mid_v2/patch_cc9d0_live_yield.py`
- Modify: `recomp_mid_v2/patch_{14a01c,2b0fb4,39e5d8}_opd.py`
- Modify: `recomp_mid_v2/patch_{2b3d1c_movie_io,fios_f2a_f2b_wad}.py`

**Interfaces:**
- Produz: `apply_all_patches.sh --status <FICHEIRO.tsv>` — uma linha por patch com
  `nome<TAB>classe<TAB>rc<TAB>estado<TAB>ficheiros_tocados`. Consumido pela Task 7 e pelo aceite.

- [ ] **Passo 1: Provar cada falso-verde**

```bash
cd ~/Documents/PESSOAL/gow2-recomp
sed -n '21,28p' recomp_mid_v2/patch_fios_f2a_f2b_wad.py   # return 0 sem ler ficheiro
sed -n '38,41p' recomp_mid_v2/patch_2b3d1c_movie_io.py    # return 0 incondicional
sed -n '92,98p' recomp_mid_v2/patch_39e5d8_opd.py         # imprime "OK patched 0"
sed -n '25,27p' recomp_mid_v2/patch_cc9d0_live_yield.py   # TARGET absoluto, ignora argv
```

- [ ] **Passo 2: Fechar o furo do snapshot**

`:120` calcula `TRACKED` a partir dos nomes de chunk citados nos próprios patches → **6 ficheiros**.
Mas 18 patches escrevem por `glob("ppu_recomp_*.cpp")` nos 31. Prova: `ppu_recomp_004.cpp` tem 12 e
`ppu_recomp_006.cpp` tem 7 sítios `ps3_indirect_tail` — 19 conversões em chunks não vigiados, que
podem ser classificadas `ALREADY-APPLIED`. Trocar `TRACKED` pelo glob completo.

- [ ] **Passo 3: `patch_cc9d0_live_yield.py` passa a respeitar `sys.argv[1]`**

É o **único** dos 71 que não o faz (70/71 já normalizados: 61 via `sys.argv[1]`, 10 via
`resolve_lift_paths`). Com o re-lift a escrever num dir novo, este escreveria no lift antigo e o
harness reportaria APPLIED.

- [ ] **Passo 4: Os 3 escritores e os 2 verificadores passam a falhar quando não fazem nada**

`sites == 0` → `rc != 0`. Isto vai tornar 3 patches vermelhos hoje se o padrão não existir — **é o
sinal correcto**, e a Task 7 classifica-os.

- [ ] **Passo 5: Emitir status por patch**

O rc actual é um bit agregado. Acrescentar `--status FILE.tsv` ao loop `:263-299`, reutilizando a
classificação já existente (`rc + diff de hashes` → `APPLIED` / `ALREADY-APPLIED` / `FAILED` /
`FAILED-PARTIAL`, `:271-292`).

- [ ] **Passo 6: Correr o modo apply e registar a linha de base**

```bash
./apply_all_patches.sh recomp_macos_v2 --status /tmp/status_pre.tsv ; echo "RC=$?"
cut -f4 /tmp/status_pre.tsv | sort | uniq -c
```

Esperado contra o lift de produção: maioria `ALREADY-APPLIED`, e os `FAILED` que sobrarem são os
que a Task 7 classifica. Registar os números exactos — são o alvo do aceite.

- [ ] **Passo 7: Commit**

```bash
git add apply_all_patches.sh recomp_mid_v2/patch_*.py
git commit -m "rdy0: catalogo deixa de reportar verde onde nao fez nada

- snapshot passa a cobrir os 31 chunks (eram 6): 18 patches escrevem por glob
  e 19 conversoes de bctr ficavam invisiveis, classificadas ALREADY-APPLIED
- patch_cc9d0_live_yield.py passa a respeitar sys.argv[1] (era o unico dos 71
  com alvo absoluto: escreveria no lift antigo apos o re-lift)
- 3 escritores (14a01c/2b0fb4/39e5d8 OPD) e 2 verificadores (2b3d1c_movie_io,
  fios_f2a_f2b_wad) passam a falhar quando convertem 0 sitios
- novo --status FILE.tsv com estado por patch"
```

---

## Task 7: `PATCH_CATALOG.tsv` — triagem fechada em 71

**Files:**
- Create: `gow2-recomp/recomp_mid_v2/PATCH_CATALOG.tsv`

**Interfaces:**
- Consome: `--status` da Task 6.
- Produz: uma linha por patch — `nome`, `escreve`, `classe`, `chunk_fixo`, `marcador`, `destino`.

**Regra de desempate (escrever no cabeçalho do ficheiro):** *"escreve/verifica" é propriedade do
SCRIPT; "funcional/probe/obsoleto" é propriedade do COMPORTAMENTO que ele guarda.* Sem esta regra o
catálogo não fecha — o ledger tem 5 patches com classes conflituantes
(`.superpowers/sdd/progress.md:511-518` vs `:593-598`, com auto-correcção em `:586-589`).

- [ ] **Passo 1: Gerar o eixo mecânico (não precisa de re-lift)**

```bash
cd ~/Documents/PESSOAL/gow2-recomp/recomp_mid_v2
for f in patch_*.py; do
  w=$(grep -cE 'write_text|\.write\(|open\([^)]*["'"'"']w|os\.replace|os\.rename' "$f")
  c=$(grep -cE 'ppu_recomp_[0-9]+' "$f")
  r=$(grep -c 'resolve_lift_paths' "$f")
  printf '%s\t%s\t%s\t%s\n' "$f" "$([ "$w" -gt 0 ] && echo ESCREVE || echo VERIFICA)" \
         "$([ "$c" -gt 0 ] && [ "$r" -eq 0 ] && echo CHUNK_FIXO || echo RESOLVIDO)" "$w"
done
```

Esperado: **60 ESCREVE / 11 VERIFICA**, batendo com o ledger por medição independente. E ~42 com
`CHUNK_FIXO` — o lifter actual produz 7 chunks contra os 31 de produção, logo esses são frágeis por
construção.

- [ ] **Passo 2: Migrar os `CHUNK_FIXO` para `resolve_lift_paths()`**

`recomp_mid_v2/lift_paths.py` já existe (commit `cf08fbc`) e é usado por 10 patches. Task mecânica,
um patch de cada vez. **Cuidado medido:** `lift_paths.py:43-44` expande um directório para TODOS os
chunks, e 4 verificadores exigem o marcador em CADA ficheiro → marcam `MISSING` nos 30 chunks onde
a função nem vive. Corrigir esse comportamento antes de migrar mais patches, senão a triagem
classifica mal 4 à cabeça.

- [ ] **Passo 3: Classificar o eixo comportamental**

`FUNCIONAL` (perdê-lo = regressão) / `PROBE` (instrumentação gated) / `OBSOLETO` (agulha
insatisfazível). Herdar do ledger onde ele nomeia (45 dos 71) e classificar os 26 restantes — dos
quais 23 não têm registo em fonte nenhuma. Dois já são fecháveis sem re-lift: `PS3_TRACE_SHREG`
(referido por `patch_tymap_18e814.py` e ausente do lift) e a cauda do `patch_2b0fb4_trace.py`.

- [ ] **Passo 4: Reparar os `FUNCIONAL` que continuam vermelhos**

Depois da migração mecânica do passo 2 e dos fixes de harness da Task 6, correr o catálogo contra
um lift fresco e atacar o que sobrar, **um patch de cada vez**, do mais bloqueante para o menos:

```bash
./apply_all_patches.sh recomp_macos_v3 --status /tmp/status_iter.tsv
join -t$'\t' -1 1 -2 1 <(sort /tmp/status_iter.tsv) <(sort PATCH_CATALOG.tsv) \
  | awk -F'\t' '$4 ~ /FAILED/ && $6=="FUNCIONAL" {print $1}'
```

Baseline do ledger: dos 49 `FAILED` medidos num re-lift limpo, **13 são FUNCIONAL** (perdê-los é
regressão), 8 são cascata de outros, 8 eram bug do harness (Task 6) e 19 são PROBE (fora do gate).
Por cada um: reproduzir a falha, reescrever a agulha contra o shape novo, e provar idempotência
(duas corridas seguidas → ficheiro igual). A lista encolhe a cada iteração — repetir o comando até
a saída ficar vazia.

- [ ] **Passo 5: O aceite do catálogo**

```bash
wc -l PATCH_CATALOG.tsv                      # 71 + cabeçalho
cut -f3 PATCH_CATALOG.tsv | sort | uniq -c   # sem "INDETERMINADO"
```

Esperado: 71 linhas, soma das classes = 71, zero indeterminados.

- [ ] **Passo 6: Commit**

```bash
git add recomp_mid_v2/PATCH_CATALOG.tsv recomp_mid_v2/lift_paths.py recomp_mid_v2/patch_*.py
git commit -m "rdy0: catalogo de triagem dos 71 patches, fechado e versionado

60 escrevem / 11 verificam (medido, coincide com o ledger). Eixo
comportamental: FUNCIONAL / PROBE / OBSOLETO, com regra de desempate escrita
(escreve-vs-verifica e' propriedade do script; funcional e' do comportamento).

42 patches com chunk fixo migrados para resolve_lift_paths(): o lifter actual
produz 7 chunks contra os 31 do lift de producao."
```

---

## Task 8: Agregador de smoke N-de-M

**Files:**
- Create: `gow2-recomp/smoke_relift_equiv.sh`
- Modify: `gow2-recomp/count_menu_gate.py`

**Porquê:** nenhum script existente implementa o aceite. O trio do `CLAUDE.md` está esparramado por
três scripts que nunca correm juntos, `smoke_perf_macos.sh` reprova sempre (lê uma linha `[PERF]`
que não existe no motor), e `grep sticky` dá falso positivo (`[INTROSEQ] CE03C clear sticky EOS
hook` não tem nada a ver com o sticky do done-word FIOS).

**Interfaces:**
- Produz: `smoke_relift_equiv.sh <LIFT_DIR> <M> <OUT.tsv>` → exit 0 se os contadores baterem no
  baseline. Consumido pela Task 9 e pelo aceite final.

- [ ] **Passo 1: Estender o `count_menu_gate.py` (mesmos nomes, campos novos)**

Já devolve 16 métricas nomeadas e reproduz o baseline ao número. Acrescentar: `st620_max`,
`function_table_count`, `imports`, `modules`, `sticky_pub`, `orfaos`. **Strings exactas** (nunca a
palavra solta): `[MOVIEFSM] st620`, `F2B-RESTATUS`, `F2B-DONEFORCE`, `F2B-KEEP-DONE`.

- [ ] **Passo 2: Reutilizar literalmente o `run_boot` do braço M0**

`smoke_intro_vdec_wad.sh:46-63` — `unset PS3_TRACE_*`, `PS3_NO_RSX=1 PS3_PERF_FSM=1`, kill por PID
(`TERM` → `-9`). Mesma recipe = comparabilidade. Não inventar recipe nova.

- [ ] **Passo 3: Gates duros do agregador**

| Gate | Critério | Fonte |
|---|---|---|
| Estrutural (pré-boot) | `function_table_count == 56072` | `grep '^const uint64_t function_table_count' $LIFT/ppu_recomp_*.cpp` |
| Estrutural (boot) | `56223 lifted functions registered` | linha `[boot]` — **número diferente do de cima**, ambos medidos |
| Estrutural | `151 imports / 13 modules` | linha `[imp]` |
| Progresso | `st620_max >= 3` em **≥4 de 6** corridas | `[MOVIEFSM] st620` |
| Higiene | `pgrep -f boot_gow2 \| wc -l == 0` no fim | assert duro (`pgrep -cf` não é portável no macOS) |
| Ambiente | `sysctl -n vm.swapusage` gravado por corrida | acima de ~4000 MB o `st620` prende em 1 e **não é regressão** |

**Fora do gate M0 (medido em 2026-07-25):** `R_PermA bytes_read` tem **zero** ocorrências no log da
recipe M0 — o boot não chega lá em 25 s com `PS3_MOVIE_EOS=0`. O valor `20169344` vem da recipe
menu-fast, que é outro baseline. Não usar como aceite deste track.

- [ ] **Passo 4: Correr contra o baseline e confirmar verde**

```bash
./smoke_relift_equiv.sh recomp_macos_v2 6 /tmp/equiv_pre.tsv ; echo "RC=$?"
```

Esperado: `RC=0`, e os valores batem com `lift_baseline/counters_pre.tsv` da Task 1.

- [ ] **Passo 5: Confirmar que sabe reprovar**

Correr contra um `boot_gow2` construído sem o `host_gow2_factory.o` (Task 4 desligada) — deve dar
`RC=1` e nomear o gate que caiu.

- [ ] **Passo 6: Commit**

```bash
git add smoke_relift_equiv.sh count_menu_gate.py
git commit -m "rdy0: agregador N-de-M do aceite de equivalencia do re-lift

Nenhum smoke existente media o trio do CLAUDE.md junto. Reutiliza o run_boot
do braco M0 (unica recipe com gate duro), corre M=6, e agrega por contadores
nomeados com strings exactas -- 'grep sticky' dava falso positivo do INTROSEQ.

Grava vm.swapusage por corrida: acima de ~4000 MB o st620 prende em 1 e isso
nao e regressao de codigo."
```

---

## Task 9: `RELIFT=1` no `build_macos.sh`

**Files:**
- Modify: `gow2-recomp/build_macos.sh`

**Interfaces:**
- Consome: Tasks 2, 4, 5, 6, 8.
- Produz: `RELIFT=1 ./build_macos.sh` → lift para dir novo, patches, build, smoke, promoção.

- [ ] **Passo 1: Fixar o comando canónico de lift**

Existem 6 variantes no checkout, duas com flags que o lifter já não aceita (`--max-mid-passes`,
`--chunk-lines` → 0 ocorrências no argparse). A que produziu o lift de produção está só numa nota
(`notes/2026-07-20-wall-m2-vencido.md:53-56`) e o comentário do próprio build aponta o **dir errado**
(`:6-7` diz `--output recomp_macos`, mas o default é `recomp_macos_v2` em `:33`):

```bash
$PS3/.venv/bin/python $PS3/tools/ppu_lifter.py EBOOT.ELF \
    --functions functions.json --output "$LIFT_NEW" -j 8
```

Sem `--code-end`, `--hle-stubs`, `--names` nem `--symbol-prefix` (decisão fixada). O
`functions.json` é **input versionado e congelado** (20.750 entradas, md5 `b151f319…`, commit
`40d4f13`) — o `RELIFT=1` consome-o, nunca o regenera.

- [ ] **Passo 2: Inserir o bloco**

Âncoras textuais estáveis: entre `HOST_OPT must be -O0|-O1|-O2|-Os` (`:43`) e
`no lift output in $LIFT -- run ppu_lifter.py first` (`:47`). `set -euo pipefail` (`:21`) já garante
aborto em lift falhado.

Encadeamento: **re-lift → `apply_all_patches.sh` (modo apply) → build**. O catálogo faz `exit 2` se
o dir de lift não existir (`:69-72`), logo a ordem não é negociável. O lift novo vai para
`recomp_macos_v3` e só é promovido a default depois do smoke.

- [ ] **Passo 3: Gate do carimbo `lifter-rev`**

O lifter emite `/* lifter-rev: … */` em cada chunk (`tools/ppu_lifter.py:3053`, dentro de
`_preamble_lines()`), mas nada o lê e o lift de produção tem **zero** carimbos — prova directa de
que é anterior a `bc3707d`. Política: ausente ou `sha != HEAD` de `tools/` → erro (ou re-lift
automático se `RELIFT=1`); sufixo `-dirty` → aviso.

- [ ] **Passo 4: Limpar os `.o` órfãos**

O lifter apaga os `.cpp` antigos do dir de saída mas **não** os `.o` (`:4006-4013`). Hoje
`recomp_macos_v2` tem 83 `.o` para 31 `.cpp`. Acrescentar `rm -f "$LIFT_NEW"/ppu_recomp_*.cpp.o`.

- [ ] **Passo 5: Cronometrar (não há nenhum número medido no macOS)**

O lift em si é barato — 11 s medidos. O compile não está documentado em lado nenhum; o único número
no repo é do Windows. Gravar o `dur=` que o próprio build já imprime (`:111`) em
`lift_baseline/timing.tsv`, por chunk. **Não prometer aceite temporal antes disto.**

- [ ] **Passo 6: Commit**

```bash
git add build_macos.sh
git commit -m "rdy0: RELIFT=1 encadeia lift -> patches -> build no build_macos.sh

Ate aqui nao existia encadeamento nenhum: 0 chamadas ao lifter e 0 ao
apply_all_patches.sh; a reaplicacao dos 71 patches era 100% manual.

Escreve para recomp_macos_v3 e so promove apos o smoke (o v2 e a unica copia
do que ainda nao foi extraido). Gate do carimbo lifter-rev, que existia mas
ninguem lia. Limpa os .o orfaos que o lifter nao apaga."
```

---

## Task 10: O aceite

**Interfaces:** consome tudo.

- [ ] **Passo 1: Re-lift limpo**

```bash
cd ~/Documents/PESSOAL/gow2-recomp
RELIFT=1 ./build_macos.sh
```

- [ ] **Passo 2: O baseline forense tem de sobreviver**

```bash
./verify_lift_baseline.sh recomp_macos_v3 ; echo "RC=$?"
```

Esperado: `RC=0` — os 21 marcadores repostos por scripts, nenhum perdido.

- [ ] **Passo 3: O catálogo tem de fechar**

```bash
./apply_all_patches.sh recomp_macos_v3 --status /tmp/status_pos.tsv
awk -F'\t' '$4=="FAILED" || $4=="FAILED-PARTIAL"' /tmp/status_pos.tsv | \
  grep -vF -f <(awk -F'\t' '$3=="PROBE"{print $1}' recomp_mid_v2/PATCH_CATALOG.tsv)
```

Esperado: **saída vazia** — zero falhas fora da classe `PROBE`. Baseline medido antes do plano:
`APPLIED 13 / ALREADY 9 / FAILED 49`.

- [ ] **Passo 4: Equivalência funcional**

```bash
./smoke_relift_equiv.sh recomp_macos_v3 6 /tmp/equiv_pos.tsv ; echo "RC=$?"
diff <(cut -f1,2 lift_baseline/counters_pre.tsv) <(cut -f1,2 /tmp/equiv_pos.tsv)
```

Esperado: `RC=0` e diff vazio nos contadores estruturais.

- [ ] **Passo 5: Higiene**

```bash
git -C ~/Documents/PESSOAL/gow2-recomp status --short   # sem ' M' de ficheiros versionados
git -C ~/Documents/PESSOAL/ps3recomp status --short
pgrep -c -f boot_gow2                                   # 0
```

- [ ] **Passo 6: Promover e registar**

Promover `recomp_macos_v3` a default em `build_macos.sh:33`, **mantendo `recomp_macos_v2` em disco**
até uma segunda leva confirmar. Actualizar `.superpowers/sdd/progress.md` e marcar RDY-0 fechado no
plano-mestre.

---

## Definition of Done

- Um re-lift limpo (`RELIFT=1`, `recomp_macos_v2` intocado) produz um `boot_gow2` que passa o smoke
  N-de-M contra os contadores gravados na Task 1.
- `verify_lift_baseline.sh` verde contra o lift novo: nenhum dos 21 comportamentos órfãos se perdeu.
- Zero patches `FAILED` fora da classe `PROBE`, com estado por patch em ficheiro máquina.
- `PATCH_CATALOG.tsv` fecha em 71 sem "indeterminado".
- Nenhum símbolo host vive apenas dentro de `recomp_*/` gitignored — ou está em ficheiro versionado
  compilado, ou tem um script escritor que o repõe.
- `git status` limpo nos dois repos; nenhum ficheiro de jogo em nenhum commit.

## Riscos registados

1. **O `st620` é não-determinístico.** Taxas medidas de 1/8 a 6/6 sem mudança de código, com limiar
   de swap documentado. Por isso o aceite é 4-de-6 e grava `vm.swapusage`. Um vermelho isolado com
   swap alto **não é regressão** — repetir antes de investigar.
2. **`recomp_macos_v2` é insubstituível até à Task 4 fechar.** Nada de `git clean -fdx`, nada de
   re-lift in-place.
3. **O aceite não cobre o caminho de present.** A recipe M0 é headless (`PS3_NO_RSX=1`); um re-lift
   que parta o flip passa este gate. Follow-up: um braço `metal` corrido pelo utilizador, fora do
   caminho do agente.
4. **`Gate A` continua RED** (`SetFlip_after_R_Perm = 0`) — é a parede activa, não regressão deste
   plano. Entra no agregador como valor esperado, não como falha.
