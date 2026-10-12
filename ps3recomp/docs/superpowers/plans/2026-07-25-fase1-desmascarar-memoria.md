# Fase 1 (1º entregável) — Desmascarar a memória guest: plano de implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: usar **superpowers:subagent-driven-development**
> (recomendado) ou superpowers:executing-plans para executar tarefa a tarefa.
> Os passos usam checkbox (`- [ ]`) para acompanhamento.

**Objectivo:** medir quanto e o quê as duas máscaras sempre-ligadas do motor
engolem (com atribuição à função guest), corrigir o mapa de regiões que hoje
reporta a heap real do jogo como `unmapped`, e registar as ranges comitadas que
o `sys_memory` nunca regista — terminando com re-medição do baseline.

**Arquitectura:** um módulo novo e pequeno (`ps3_mask_trace`) observa as
máscaras existentes sem lhes inverter o controlo; as máscaras ganham um gate de
desactivação para o experimento E2; `vm.h` passa a ser a única fonte de verdade
do mapa de regiões, com `_Static_assert` a impedir drift nos ficheiros que hoje
duplicam as constantes. **Nenhum re-lift** — o lifter tem uma regressão que
apagaria a instrumentação dos atómicos da Fase 0.

**Stack:** C11 (`runtime/trace/`, `runtime/syscalls/`), C++20 (`ppu_loader.cpp`),
CMake+Ninja em `build-macos`, testes standalone no padrão de `runtime/trace/tests/`.

**Spec:** [`docs/superpowers/specs/2026-07-25-fase1-desmascarar-memoria-design.md`](../specs/2026-07-25-fase1-desmascarar-memoria-design.md)

## Global Constraints

Copiadas verbatim do spec (§2). Ligam **todas** as tarefas.

- **G1 — Formato.** `[PS3T] <tb> | <thr> | <fn> | <subsys> | <event> | <data>`;
  separador exactamente ` | `; nenhum campo contém `|`; `<data>` são pares `k=v`
  separados por espaço simples; uma linha = **uma** chamada de escrita; destino
  `stderr`. Sanitização é central em `ps3_trace_emit` — **não reimplementar**.
- **G2 — Omissões.** `MEMORY/SWALLOWED` OFF (via `PS3_TRACE_MEMORY`);
  `PS3_MASK_OFF_*` omissão = **máscara LIGADA**; correcções de `vm_region_name`
  e registo de ranges **ON** (são bugs). Com `PS3_TRACE_MEMORY` por definir e
  nenhum `PS3_MASK_OFF_*` definido: **zero bytes emitidos pela camada nova e
  zero alteração de comportamento**, excepto o efeito declarado das **três**
  correcções de bug: as duas de mapa/registo acima **e**
  `sys_mmapper_allocate_address` (`runtime/syscalls/sys_memory.c:531-533`),
  que passou a devolver `CELL_ENOMEM` e a não escrever `addr_out` quando o
  `vm_commit` falha (antes: `CELL_OK` com `addr_out` apontando para memória
  nunca comitada) — mudança de contrato de sucesso da syscall, revisão final
  de branch (I4).
- **G3 — Não inverter o controlo.** Quem decide engolir continua a decidir. A
  camada nova observa; só altera a decisão quando o `PS3_MASK_OFF_*` respectivo
  está ligado.
- **G4 — Plataforma.** macOS/arm64 primário; Windows não pode regredir.
  Proibido fora de `#ifdef _WIN32`: `<intrin.h>`, `Sleep(`, `__declspec`,
  `GetModuleHandleA`. Nada POSIX-only desguardado em código que entra na
  `libps3recomp_runtime.a`.
- **G5 — Honestidade.** Todo aceite declara **in-boot**, **offline-unit** ou
  **não-exercitado**. Sobre-declarar nível de evidência é defeito.
- **G6 — Runs.** `PS3_NO_RSX=1` **e** `PS3_RSX_BACKEND=trace`; morte **por PID**
  (TERM, depois `-9`); **nunca** `pkill -f boot_gow2`; logs fora dos dois repos,
  nunca commitados; `PS3_ENGINE_ROOT` obrigatório ao construir o `boot_gow2`.
- **G7 — Git.** Commits em **português**, sem `Co-Authored-By`; `git add` com
  paths explícitos, **nunca** `git add -A`; nunca dados de jogo, logs de boot,
  nem `runtime/spu/tests/out_*/spu_recomp.{c,h}`.
- **G8 — Fronteira.** Apenas o repo do motor. Nenhum commit em `gow2-recomp`.

**Suites que têm de continuar verdes em todas as tarefas** (a Fase 0 partiu-se
duas tarefas em silêncio por não as correr):

```bash
runtime/spu/tests/run_tests.sh                    # 21 passed, 0 failed
runtime/trace/tests/run_tests.sh                  # 4
runtime/trace/tests/run_tests_checkpoint.sh       # 6
runtime/trace/tests/run_tests_mem_trace.sh        # 5
runtime/trace/tests/run_tests_heap.sh             # 4
runtime/spu/tests/run_tests_dma_trace.sh          # 2
runtime/syscalls/tests/run_tests_threads_trace.sh # 2
libs/video/tests/run_tests_rsx_trace.sh           # 2
scripts/smoke_macos.sh --no-gui                   # PASS integral
```

## Estrutura de ficheiros

| Ficheiro | Responsabilidade | Tarefa |
|---|---|---|
| `include/ps3_mask_trace.h` (novo) | contrato: enum das máscaras, `report`, `disabled` | 1 |
| `runtime/trace/ps3_mask_trace.c` (novo) | emissão `SWALLOWED` + cache dos gates | 1 |
| `runtime/trace/tests/test_ps3_mask_trace.c` (novo) | unit test do módulo | 1 |
| `runtime/trace/tests/run_tests_mask_trace.sh` (novo) | runner standalone | 1 |
| `runtime/ppu/ppu_loader.cpp` (modificar) | ligar as duas máscaras | 2, 3 |
| `runtime/memory/vm.h` (modificar) | mapa de regiões — fonte única | 4 |
| `runtime/syscalls/sys_memory.c` (modificar) | `_Static_assert` + registar ranges | 4, 5 |
| `runtime/syscalls/sys_vm.h` (modificar) | `_Static_assert` do mapa | 4 |
| `scripts/smoke_macos.sh` (modificar) | ligar a suite nova | 1 |
| `docs/bringup/gow2/FASE1/` (novo) | relatórios E1/E2 e baseline | 6, 7 |

---

## Task 1: Módulo `ps3_mask_trace`

**Ficheiros:**
- Criar: `include/ps3_mask_trace.h`
- Criar: `runtime/trace/ps3_mask_trace.c`
- Criar: `runtime/trace/tests/test_ps3_mask_trace.c`
- Criar: `runtime/trace/tests/run_tests_mask_trace.sh`
- Modificar: `scripts/smoke_macos.sh` (secção 2b, lista de suites)

**Interfaces:**
- Consome: `ps3_trace_on`, `ps3_trace_emit`, `PS3_TS_MEMORY` de `include/ps3_trace.h`;
  `vm_region_name` de `runtime/memory/vm.h`.
- Produz (as tarefas 2 e 3 dependem destas assinaturas exactas):
  ```c
  typedef enum { PS3_MASK_UNCOMMITTED = 0, PS3_MASK_TBLSIZE, PS3_MASK__COUNT } ps3_mask_id;
  void ps3_mask_swallowed_report(ps3_mask_id mask, uint32_t ea, uint32_t len,
                                 const char* op, void* caller_ra);
  int  ps3_mask_disabled(ps3_mask_id mask);
  ```

**Porquê em `runtime/trace/`:** `CMakeLists.txt:118-129` faz `GLOB_RECURSE`
de `runtime/*.c` e exclui apenas `/tests/`, `/runtime/ppu/` e `/runtime/host/`.
Um `.c` aqui entra na `libps3recomp_runtime.a` sem tocar no CMake — e, ao
contrário de `ppu_loader.cpp` (que inclui o `ppu_recomp.h` gerado por jogo),
compila standalone para o teste unitário. Mesmo motivo de `ps3_mem_trace.c`.

- [ ] **Passo 1: escrever o cabeçalho**

`include/ps3_mask_trace.h`:

```c
/*
 * ps3recomp - ps3_mask_trace: observacao das mascaras de memoria sempre-ligadas.
 *
 * PROBLEMA: o motor tem guards que ENGOLEM acessos guest silenciosamente (ou
 * com log capado e nao-canonico). Nao sabemos quantos engolem, nem de que
 * funcoes guest vem. Este modulo mede isso SEM alterar a decisao (G3): quem
 * decide engolir continua a decidir.
 *
 * Emite no subsistema MEMORY, que ja existe -- o plano de origem fixa dez
 * subsistemas, nao se cria um decimo-primeiro.
 */
#ifndef PS3_MASK_TRACE_H
#define PS3_MASK_TRACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ordem e' o indice das tabelas internas de nome e de gate. Acrescentar SO'
 * antes de PS3_MASK__COUNT, e actualizar as duas tabelas em ps3_mask_trace.c
 * (ha um _Static_assert de contagem la que falha se esquecer). */
typedef enum {
    PS3_MASK_UNCOMMITTED = 0,   /* vm_uncommitted_guard  (ppu_loader.cpp:531) */
    PS3_MASK_TBLSIZE,           /* tblsize_guard         (ppu_loader.cpp:968) */
    PS3_MASK__COUNT
} ps3_mask_id;

/* Cap de linhas SWALLOWED por mascara, com agenda de crescimento -- ver
 * ps3_mask_trace.c. Mesmo valor e mesma politica do MEMORY/OOB da Fase 0
 * (PS3_MEM_OOB_CANONICAL_CAP), para o volume ser previsivel quando os dez
 * subsistemas correm juntos no baseline. */
#define PS3_MASK_SWALLOWED_CAP 64

/*
 * Emite uma linha canonica G1:
 *   [PS3T] <tb> | <thr> | <fn> | MEMORY | SWALLOWED |
 *          ea=0x%08X len=%u mask=<nome> op=<op> region=<nome>
 *
 * No-op estrito se PS3_TRACE_MEMORY estiver desligada: nao formata, nao conta,
 * nao aloca. `caller_ra` e' resolvido por dladdr para o simbolo da funcao
 * guest liftada (as funcoes do lift sao simbolos globais T no boot_gow2);
 * NULL ou nao-resolvivel -> campo <fn> = "-".
 * `op` e' um token curto sem espacos: "read" ou "write".
 */
void ps3_mask_swallowed_report(ps3_mask_id mask, uint32_t ea, uint32_t len,
                               const char* op, void* caller_ra);

/*
 * 1 se a mascara deve ser DESLIGADA nesta execucao, i.e. se
 * PS3_MASK_OFF_<NOME> esta definida e o primeiro caracter nao e' '\0' nem '0'.
 * Omissao (variavel por definir) = 0 = mascara LIGADA = comportamento actual.
 * Resolvido uma vez e cacheado; sem getenv no caminho quente.
 */
int ps3_mask_disabled(ps3_mask_id mask);

/* Exposto para o teste unitario conferir a tabela contra o enum. */
const char* ps3_mask_name(ps3_mask_id mask);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PS3_MASK_TRACE_H */
```

- [ ] **Passo 2: escrever o teste que falha**

`runtime/trace/tests/test_ps3_mask_trace.c` — segue o padrão de
`test_ps3_mem_trace.c` (captura de `stderr` por `mkstemp`+`dup2`, cenários
seleccionados por `argv[1]` porque o cache de `getenv` é por processo):

```c
/*
 * Unit test de ps3_mask_trace (Fase 1, Task 1).
 *
 * Cenarios sao processos separados: ps3_trace_on() e ps3_mask_disabled()
 * cacheiam o getenv na primeira chamada, por isso ligar/desligar dentro do
 * mesmo processo nao testaria nada.
 */
#include "ps3_mask_trace.h"
#include "ps3_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>

static char  s_cap_path[] = "/tmp/mask_trace_capXXXXXX";
static int   s_saved_fd = -1;
static FILE* s_cap_fp = NULL;

static void cap_begin(void)
{
    int fd = mkstemp(s_cap_path);
    if (fd < 0) { perror("mkstemp"); exit(97); }
    fflush(stderr);
    s_saved_fd = dup(fileno(stderr));
    dup2(fd, fileno(stderr));
    close(fd);
}

static char* cap_end(void)
{
    fflush(stderr);
    dup2(s_saved_fd, fileno(stderr));
    close(s_saved_fd);
    s_cap_fp = fopen(s_cap_path, "rb");
    if (!s_cap_fp) { perror("fopen"); exit(97); }
    static char buf[1 << 16];
    size_t n = fread(buf, 1, sizeof(buf) - 1, s_cap_fp);
    buf[n] = '\0';
    fclose(s_cap_fp);
    unlink(s_cap_path);
    return buf;
}

static int fails = 0;
static void check(int cond, const char* what)
{
    if (!cond) { printf("[FAIL] %s\n", what); fails++; }
}

/* T1: gate MEMORY desligado -> zero bytes. */
static void t_gate_off(void)
{
    cap_begin();
    ps3_mask_swallowed_report(PS3_MASK_UNCOMMITTED, 0x40001000u, 4, "write", NULL);
    char* out = cap_end();
    check(strstr(out, "[PS3T]") == NULL, "gate off: esperava 0 linhas [PS3T]");
}

/* T2: gate ligado -> linha no formato G1 exacto, com todos os campos. */
static void t_format(void)
{
    regex_t re;
    const char* pat =
      "^\\[PS3T\\] [0-9]+ \\| (0x[0-9A-F]{8}|-) \\| [^|]* \\| MEMORY \\| SWALLOWED \\| "
      "ea=0x40001000 len=4 mask=uncommitted_guard op=write region=[a-z_]+$";
    check(regcomp(&re, pat, REG_EXTENDED | REG_NEWLINE) == 0, "regcomp");

    cap_begin();
    ps3_mask_swallowed_report(PS3_MASK_UNCOMMITTED, 0x40001000u, 4, "write", NULL);
    char* out = cap_end();

    char* nl = strchr(out, '\n'); if (nl) *nl = '\0';
    check(regexec(&re, out, 0, NULL, 0) == 0, "formato G1 da linha SWALLOWED");
    regfree(&re);
}

/* T3: os nomes das mascaras sao os tokens fechados do contrato. */
static void t_names(void)
{
    check(strcmp(ps3_mask_name(PS3_MASK_UNCOMMITTED), "uncommitted_guard") == 0,
          "nome de PS3_MASK_UNCOMMITTED");
    check(strcmp(ps3_mask_name(PS3_MASK_TBLSIZE), "tblsize_guard") == 0,
          "nome de PS3_MASK_TBLSIZE");
    check(strchr(ps3_mask_name(PS3_MASK_UNCOMMITTED), ' ') == NULL,
          "nome sem espacos (senao parte o k=v de G1)");
}

/* T4: cheia -> cap respeitado e total recuperavel por SWALLOWED_TRUNCATED. */
static void t_flood(void)
{
    cap_begin();
    for (int i = 0; i < 3 * PS3_MASK_SWALLOWED_CAP + 3; i++)
        ps3_mask_swallowed_report(PS3_MASK_TBLSIZE, 0x572BB4u, 4, "write", NULL);
    char* out = cap_end();

    int swallowed = 0, truncated = 0;
    for (char* p = out; (p = strstr(p, "| SWALLOWED |")) != NULL; p++) swallowed++;
    for (char* p = out; (p = strstr(p, "| SWALLOWED_TRUNCATED |")) != NULL; p++) truncated++;

    check(swallowed == PS3_MASK_SWALLOWED_CAP, "cheia: exactamente CAP linhas SWALLOWED");
    check(truncated == 3, "cheia: 3 marcos TRUNCATED (65, 129, 193)");
    check(strstr(out, "count=65") != NULL,  "TRUNCATED com count=65");
    check(strstr(out, "count=193") != NULL, "TRUNCATED com count=193");
}

/* T5: gate por mascara e' independente. */
static void t_disabled_independente(void)
{
    check(ps3_mask_disabled(PS3_MASK_UNCOMMITTED) == 1,
          "PS3_MASK_OFF_UNCOMMITTED=1 -> disabled");
    check(ps3_mask_disabled(PS3_MASK_TBLSIZE) == 0,
          "PS3_MASK_OFF_TBLSIZE por definir -> NAO disabled");
}

int main(int argc, char** argv)
{
    const char* mode = (argc > 1) ? argv[1] : "off";
    if      (!strcmp(mode, "off"))      t_gate_off();
    else if (!strcmp(mode, "format"))   t_format();
    else if (!strcmp(mode, "names"))    t_names();
    else if (!strcmp(mode, "flood"))    t_flood();
    else if (!strcmp(mode, "disabled")) t_disabled_independente();
    else { printf("modo desconhecido: %s\n", mode); return 2; }

    printf("Results: %d passed, %d failed\n", fails == 0 ? 1 : 0, fails);
    return fails ? 1 : 0;
}
```

- [ ] **Passo 3: correr o teste e confirmar que FALHA**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp-bringup
clang -std=c11 -I include -I runtime/memory \
  runtime/trace/tests/test_ps3_mask_trace.c runtime/trace/ps3_trace.c \
  -o /tmp/t_mask 2>&1 | head -5
```

Esperado: **FALHA** de compilação — `ps3_mask_trace.h` não existe e
`ps3_mask_swallowed_report` não está definido.

- [ ] **Passo 4: implementar o módulo**

`runtime/trace/ps3_mask_trace.c`:

```c
/*
 * ps3recomp - ps3_mask_trace. Ver include/ps3_mask_trace.h para o contrato.
 *
 * Vive em runtime/trace/ pelo mesmo motivo de ps3_trace.c / ps3_mem_trace.c:
 * o CMakeLists.txt faz GLOB_RECURSE de runtime dot-c e so' exclui tests/,
 * runtime/ppu/ e runtime/host/ -- um .c aqui entra na lib sem tocar no CMake,
 * e compila standalone para o teste unitario.
 */
#include "ps3_mask_trace.h"
#include "ps3_trace.h"
#include "../memory/vm.h"

/* dladdr()/<dlfcn.h> sao POSIX-only. Mesmo tratamento de ps3_mem_trace.c:
 * em Windows o campo <fn> fica "-" em vez de partir o build da lib
 * (CMakeLists.txt:118-129 mete este ficheiro na lib em TODAS as plataformas).
 * __sync_add_and_fetch nao precisa de guard: e' builtin GCC/Clang disponivel
 * em MinGW, ja documentado em ps3_checkpoint.c. */
#ifndef _WIN32
#include <dlfcn.h>
#endif
#include <stdlib.h>
#include <stdio.h>

static const char* const k_mask_names[] = {
    "uncommitted_guard",   /* PS3_MASK_UNCOMMITTED */
    "tblsize_guard",       /* PS3_MASK_TBLSIZE     */
};
static const char* const k_mask_env[] = {
    "PS3_MASK_OFF_UNCOMMITTED",
    "PS3_MASK_OFF_TBLSIZE",
};

/* Falha a compilar se alguem acrescentar ao enum sem actualizar as tabelas. */
_Static_assert(sizeof(k_mask_names) / sizeof(k_mask_names[0]) == PS3_MASK__COUNT,
               "k_mask_names desalinhada com ps3_mask_id");
_Static_assert(sizeof(k_mask_env) / sizeof(k_mask_env[0]) == PS3_MASK__COUNT,
               "k_mask_env desalinhada com ps3_mask_id");

const char* ps3_mask_name(ps3_mask_id mask)
{
    if ((unsigned)mask >= (unsigned)PS3_MASK__COUNT) return "?";
    return k_mask_names[mask];
}

int ps3_mask_disabled(ps3_mask_id mask)
{
    if ((unsigned)mask >= (unsigned)PS3_MASK__COUNT) return 0;

    /* Cache por mascara: -1 = por resolver. Mesmo idioma dos gates de
     * ps3_trace.c; a corrida check-then-set e' benigna (dois escritores
     * calculam o mesmo valor) e espelha o padrao ja existente no repo. */
    static int s_state[PS3_MASK__COUNT] = { -1, -1 };
    int v = s_state[mask];
    if (v < 0) {
        const char* e = getenv(k_mask_env[mask]);
        v = (e && e[0] && e[0] != '0') ? 1 : 0;
        s_state[mask] = v;
    }
    return v;
}

void ps3_mask_swallowed_report(ps3_mask_id mask, uint32_t ea, uint32_t len,
                               const char* op, void* caller_ra)
{
    /* Gate primeiro: no-op estrito (G2) -- nada formatado, nada contado. */
    if (!ps3_trace_on(PS3_TS_MEMORY)) return;
    if ((unsigned)mask >= (unsigned)PS3_MASK__COUNT) return;

    /* Contador por mascara, para o cap de uma nao consumir o da outra. */
    static uint64_t s_count[PS3_MASK__COUNT];
    uint64_t n = __sync_add_and_fetch(&s_count[mask], (uint64_t)1);

    int emit_line = (n <= (uint64_t)PS3_MASK_SWALLOWED_CAP);
    /* Agenda de crescimento identica a do MEMORY/OOB (ps3_mem_trace.c): sem
     * ela, o total acima do cap ficaria irrecuperavel do trace -- e "quanto
     * cada mascara engole" e' precisamente a metrica deste entregavel.
     * Emite em n = CAP+1, 2*CAP+1, 3*CAP+1, ... O total lido e' um LIMITE
     * INFERIOR, com erro maximo CAP-1. */
    int emit_trunc = !emit_line &&
                     (((n - (uint64_t)PS3_MASK_SWALLOWED_CAP - 1u) %
                       (uint64_t)PS3_MASK_SWALLOWED_CAP) == 0u);
    if (!emit_line && !emit_trunc) return;

    const char* fn = NULL;
#ifndef _WIN32
    Dl_info di;
    char fn_buf[256];
    if (caller_ra && dladdr(caller_ra, &di) && di.dli_sname) {
        unsigned long off = (unsigned long)((const char*)caller_ra -
                                            (const char*)di.dli_saddr);
        snprintf(fn_buf, sizeof(fn_buf), "%s+0x%lX", di.dli_sname, off);
        fn = fn_buf;
    }
#else
    (void)caller_ra;
#endif

    if (emit_trunc) {
        ps3_trace_emit(PS3_TS_MEMORY, fn, "SWALLOWED_TRUNCATED",
                       "mask=%s count=%llu",
                       k_mask_names[mask], (unsigned long long)n);
        return;
    }

    ps3_trace_emit(PS3_TS_MEMORY, fn, "SWALLOWED",
                   "ea=0x%08X len=%u mask=%s op=%s region=%s",
                   ea, len, k_mask_names[mask],
                   (op && op[0]) ? op : "-", vm_region_name(ea));
}
```

- [ ] **Passo 5: escrever o runner**

`runtime/trace/tests/run_tests_mask_trace.sh` (`chmod +x`):

```bash
#!/usr/bin/env bash
# Build + run do unit test de ps3_mask_trace (Fase 1, Task 1).
#
# Standalone: compila runtime/trace/ps3_trace.c (Fase 0) + ps3_mask_trace.c +
# o teste. NAO liga ppu_loader.cpp (inclui o ppu_recomp.h gerado por jogo).
#
# Cada cenario e' um processo separado: os gates cacheiam o getenv.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
CC="${CC:-clang}"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
EXE="$WORK/test_ps3_mask_trace"

"$CC" -std=c11 -O1 -Wall -Wextra -Wno-unused-parameter \
    -I "$ROOT/include" -I "$ROOT/runtime/memory" \
    "$ROOT/runtime/trace/tests/test_ps3_mask_trace.c" \
    "$ROOT/runtime/trace/ps3_trace.c" \
    "$ROOT/runtime/trace/ps3_mask_trace.c" \
    -o "$EXE"

pass=0; fail=0
run() {  # run <nome> <modo> [VAR=val ...]
    local name="$1" mode="$2"; shift 2
    local out
    if out=$(env -u PS3_TRACE_MEMORY -u PS3_MASK_OFF_UNCOMMITTED \
                 -u PS3_MASK_OFF_TBLSIZE "$@" "$EXE" "$mode" 2>&1); then
        echo "  [PASS] $name"; pass=$((pass+1))
    else
        echo "  [FAIL] $name"; echo "$out" | sed 's/^/      /'; fail=$((fail+1))
    fi
}

run "off"      off                                        # sem PS3_TRACE_MEMORY
run "format"   format   PS3_TRACE_MEMORY=1
run "names"    names    PS3_TRACE_MEMORY=1
run "flood"    flood    PS3_TRACE_MEMORY=1
run "disabled" disabled PS3_MASK_OFF_UNCOMMITTED=1

echo "Results: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
```

- [ ] **Passo 6: correr e confirmar que PASSA**

```bash
./runtime/trace/tests/run_tests_mask_trace.sh
```

Esperado: `Results: 5 passed, 0 failed`.

- [ ] **Passo 7: ligar ao smoke** (uma suite que nada executa apodrece em silêncio)

Em `scripts/smoke_macos.sh`, acrescentar `runtime/trace/tests/run_tests_mask_trace.sh`
à lista do `for TRACE_SUITE in ...` da secção 2b, e actualizar o texto do
cabeçalho da secção para incluir a suite nova.

- [ ] **Passo 8: build da lib + suites + commit**

```bash
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
./scripts/smoke_macos.sh --no-gui
git add include/ps3_mask_trace.h runtime/trace/ps3_mask_trace.c \
        runtime/trace/tests/test_ps3_mask_trace.c \
        runtime/trace/tests/run_tests_mask_trace.sh scripts/smoke_macos.sh
git commit -m "feat(fase1): modulo ps3_mask_trace — evento MEMORY/SWALLOWED

Mede quanto e o que as mascaras de memoria sempre-ligadas engolem, com
atribuicao a funcao guest por dladdr. Nao inverte o controlo (G3): quem
decide engolir continua a decidir. Cap 64 com agenda de crescimento, igual
ao MEMORY/OOB da Fase 0, para o total ser sempre recuperavel."
```

---

## Task 2: Ligar o `vm_uncommitted_guard`

**Ficheiros:**
- Modificar: `runtime/ppu/ppu_loader.cpp:531-568` (corpo de `vm_uncommitted_guard`)

**Interfaces:**
- Consome: `ps3_mask_swallowed_report`, `ps3_mask_disabled`, `PS3_MASK_UNCOMMITTED` (Task 1).
- Produz: nada de novo. Comportamento inalterado com omissões.

**Contexto:** `ppu_loader.cpp` é ficheiro **protegido** e hand-maintained —
inserções cirúrgicas, sem reformatar, sem reestruturar. A função já recebe
`caller_ra`, por isso **não há mudança de assinatura**. É chamada de
`vm_read8/16/32/64` e `vm_write8/16/32/64` (`:743`, `:758`, `:785`, …), e o
parâmetro `who` já distingue a operação (`"read8"`, `"write32"`, …).

- [ ] **Passo 1: acrescentar o include**

Junto aos includes existentes de `ppu_loader.cpp` (perto de `ps3_mem_trace.h`):

```c
#include "ps3_mask_trace.h"   /* Fase 1: observacao das mascaras (SWALLOWED) */
```

- [ ] **Passo 2: gate de desactivação e emissão**

No corpo de `vm_uncommitted_guard`, **imediatamente após** a linha
`if (!vm_uncommitted(a, n)) return false;` (`:533`), inserir:

```c
    /* Fase 1: PS3_MASK_OFF_UNCOMMITTED=1 desliga a mascara -- o guard deixa de
     * interceptar e o acesso PROSSEGUE PARA O DEREFERENCE BRUTO. Como vm_init
     * faz mmap do espaco todo com PROT_NONE (vm.h:138-139) e so' main e stack
     * ganham RW (vm.h:148,156), um acesso a memoria nao comitada GERA FALTA
     * REAL (SIGBUS no macOS -- ver ppu_loader.cpp:1744-1753). Isso e' o valor
     * diagnostico do E2, nao um bug: mostra onde o jogo morreria sem a
     * mascara, com o EA e a funcao guest atribuidos.
     * Nota: este return corta ANTES do bloco vm_commit_high_on() abaixo, logo
     * combinar esta flag com PS3_COMMIT_HIGH=1 NAO produz um acesso seguro.
     * Omissao = 0 = comportamento actual (G2). Sai antes de qualquer emissao:
     * se a mascara nao engole, nao ha SWALLOWED que reportar. */
    if (ps3_mask_disabled(PS3_MASK_UNCOMMITTED)) return false;
```

E **imediatamente antes** do `return true;` final (`:567`), inserir:

```c
    /* Fase 1: canonico G1 em paralelo com os logs ad-hoc acima -- estes tem
     * caps proprios (40 / 64+1000) e formato nao-parseavel; o SWALLOWED tem
     * cap explicito com total recuperavel e e' consumido pelo trace_parse.py.
     * `who` e' "read8"/"write32"/... -> o token op= fica "read" ou "write". */
    ps3_mask_swallowed_report(PS3_MASK_UNCOMMITTED, a, n,
                              (who && who[0] == 'w') ? "write" : "read",
                              caller_ra);
```

- [ ] **Passo 3: build e suites**

```bash
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
./scripts/smoke_macos.sh --no-gui
```

Esperado: build limpo, smoke PASS integral. `ppu_loader.cpp` não entra na lib
(`CMakeLists.txt:129`), por isso o erro de compilação aparece no build do jogo —
ver Passo 4.

- [ ] **Passo 4: build do jogo e verificação in-boot**

```bash
cd /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp
PS3_ENGINE_ROOT=/Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp-bringup ./build_macos.sh
SP=/private/tmp/claude-501/-Users-andrebrumcortezferreira-Documents-PESSOAL-ps3recomp/c235dbdb-9cd6-4aa1-a89f-36bc5574cd43/scratchpad
(
  set -a; . ./env_gow2.sh; set +a
  export PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_TRACE_MEMORY=1
  exec ./boot_gow2 EBOOT.ELF
) > "$SP/f1_t2_uncommitted.log" 2>&1 &
BPID=$!; sleep 40
kill -TERM $BPID 2>/dev/null; sleep 2; kill -9 $BPID 2>/dev/null
grep -c 'mask=uncommitted_guard' "$SP/f1_t2_uncommitted.log"
grep -m2 'mask=uncommitted_guard' "$SP/f1_t2_uncommitted.log"
```

Esperado: contagem > 0 e linhas no formato G1 com `<fn>` resolvido. Colar as
linhas reais no relatório. **Nunca** `pkill`; matar por PID.

- [ ] **Passo 5: verificar o no-op (G2)**

```bash
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_NO_RSX=1 PS3_RSX_BACKEND=trace
  exec ./boot_gow2 EBOOT.ELF
) > "$SP/f1_t2_noop.log" 2>&1 &
BPID=$!; sleep 30; kill -TERM $BPID 2>/dev/null; sleep 2; kill -9 $BPID 2>/dev/null
grep -c '\[PS3T\]' "$SP/f1_t2_noop.log"
```

Esperado: **0**.

- [ ] **Passo 6: commit**

```bash
git add runtime/ppu/ppu_loader.cpp
git commit -m "feat(fase1): vm_uncommitted_guard emite SWALLOWED e ganha gate

A mascara que engole mais memoria guest passa a reportar quanto engole e de
que funcao guest vem. PS3_MASK_OFF_UNCOMMITTED=1 desliga-a para o experimento
E2; por omissao fica ligada, comportamento identico ao actual."
```

---

## Task 3: Ligar o `tblsize_guard` (mudança de assinatura)

**Ficheiros:**
- Modificar: `runtime/ppu/ppu_loader.cpp:968` (assinatura + corpo), `:1482`, `:1506` (call sites)

**Interfaces:**
- Consome: `ps3_mask_swallowed_report`, `ps3_mask_disabled`, `PS3_MASK_TBLSIZE` (Task 1).
- Produz: nada de novo.

**Porquê tarefa separada da 2:** esta muda a **assinatura** de uma função
`static` num ficheiro protegido e toca dois call sites. Um revisor pode
legitimamente aprovar a Task 2 e rejeitar esta.

**Estado actual verificado:**

```c
static int tblsize_guard(uint32_t a, uint64_t v) {   /* :968 — NAO recebe caller_ra */
    ...
    if (v == 0 && (a == 0x572BB4 || a == 0x572BB8 || a == 0x572BB0 ||
                   a == 0x53FBF0 || a == 0x53FF4C || a == 0x53D6AC)) { ... }
```

Call sites: `:1482` e `:1506`, ambos `if (tblsize_guard((uint32_t)a, v)) return;`
— ambos em caminhos de escrita.

- [ ] **Passo 1: mudar a assinatura**

`:968` passa a:

```c
/* Fase 1: ganhou caller_ra para o SWALLOWED poder atribuir a escrita a funcao
 * guest (dladdr). Os dois call sites (vm_write*) passam
 * __builtin_return_address(0). */
static int tblsize_guard(uint32_t a, uint64_t v, void* caller_ra) {
```

- [ ] **Passo 2: actualizar os dois call sites**

`:1482` e `:1506` passam a:

```c
    if (tblsize_guard((uint32_t)a, v, __builtin_return_address(0))) return;
```

- [ ] **Passo 3: gate e emissão no corpo**

Imediatamente antes do `return 1;` do ramo que bloqueia (o `if (v == 0 && (a == ...))`),
inserir:

```c
        /* Fase 1: PS3_MASK_OFF_TBLSIZE=1 deixa o zero-write passar. Omissao =
         * mascara ligada (G2). O gate fica DENTRO do ramo que bloqueia: fora
         * dele a funcao ja devolvia 0 e nao ha nada a desmascarar. */
        if (ps3_mask_disabled(PS3_MASK_TBLSIZE)) return 0;
        ps3_mask_swallowed_report(PS3_MASK_TBLSIZE, a, 4, "write", caller_ra);
```

- [ ] **Passo 4: build, suites, in-boot, no-op**

Repetir os passos 3–5 da Task 2, trocando o `grep` para `mask=tblsize_guard` e
o log para `f1_t3_tblsize.log`.

**Nota honesta:** o `tblsize_guard` só dispara em 6 EAs muito específicos e só
para `v == 0`. É perfeitamente possível que **não dispare** na janela de 40 s.
Se não disparar, declarar **não-exercitado in-boot** e apoiar-se no teste
offline-unit — não inventar evidência.

- [ ] **Passo 5: commit**

```bash
git add runtime/ppu/ppu_loader.cpp
git commit -m "feat(fase1): tblsize_guard emite SWALLOWED e ganha gate

Mudanca de assinatura: a funcao passa a receber caller_ra para o SWALLOWED
atribuir o zero-write bloqueado a funcao guest. Dois call sites actualizados.
PS3_MASK_OFF_TBLSIZE=1 deixa o write passar; omissao mantem o bloqueio."
```

---

## Task 4: `vm.h` como fonte única do mapa de regiões

**Ficheiros:**
- Modificar: `runtime/memory/vm.h:41-49` (constantes), `:332-343` (`vm_region_name`)
- Modificar: `runtime/syscalls/sys_memory.c:40,45` (`_Static_assert`)
- Modificar: `runtime/syscalls/sys_vm.h:38-39` (`_Static_assert`)
- Modificar: `runtime/ppu/ppu_loader.cpp:386,390` (`_Static_assert`)
- Modificar: `runtime/trace/tests/test_ps3_mem_trace.c` (casos de região novos)

**Interfaces:**
- Consome: nada de tarefas anteriores.
- Produz: `vm_region_name` passa a devolver `lv2_heap`, `sys_vm`, `rsx_local` e
  `tls` onde hoje devolve `unmapped` (ou `rsx` errado). A Task 1 já usa
  `vm_region_name` — o campo `region=` do `SWALLOWED` melhora sem alterações.

**O problema medido:** `vm_region_name` (`vm.h:332`) conhece 4 regiões e devolve
`unmapped` para tudo o resto. Consequências:

| EA | Hoje diz | Devia dizer | Confirmado por |
|---|---|---|---|
| `0x40000000`–`0x60000000` | `unmapped` | `lv2_heap` | `sys_memory.c:40,45` |
| `0x60000000`–`0x70000000` | `unmapped` | `sys_vm` | `sys_vm.h:38,39` |
| `0xC0000000`–`0xD0000000` | `unmapped` | `rsx_local` | `libs/video/cellGcmSys.c:295-296` |
| `0x10F00000`+`0x10000` | `rsx` | `tls` | `ppu_loader.cpp:386,390` |

**Regra de honestidade (spec §5.1):** só entram intervalos confirmados por
código. Os quatro acima estão. Nada mais é adicionado.

- [ ] **Passo 1: escrever os casos de teste que falham**

Em `runtime/trace/tests/test_ps3_mem_trace.c`, na tabela de casos de
`vm_region_name`, acrescentar:

```c
    { 0x40000000u,                "lv2_heap",  "inicio da janela lv2 (sys_memory)" },
    { 0x5FFFFFFFu,                "lv2_heap",  "ultimo byte da janela lv2" },
    { 0x60000000u,                "sys_vm",    "inicio de sys_vm" },
    { 0x6FFFFFFFu,                "sys_vm",    "ultimo byte de sys_vm" },
    { 0xC0000000u,                "rsx_local", "inicio da RSX local real" },
    { 0xCFFFFFFFu,                "rsx_local", "ultimo byte da RSX local (256 MB do GCM)" },
    { 0x10F00000u,                "tls",       "imagem TLS (hoje reporta 'rsx')" },
    { 0x10F0FFFFu,                "tls",       "ultimo byte da janela TLS" },
```

- [ ] **Passo 2: correr e confirmar FALHA**

```bash
./runtime/trace/tests/run_tests_mem_trace.sh
```

Esperado: **FALHA** no cenário `region` — os oito casos novos devolvem
`unmapped` (ou `rsx` para os dois de TLS).

- [ ] **Passo 3: acrescentar as constantes a `vm.h`**

Junto ao bloco `:41-52`:

```c
/* Fase 1: regioes que existiam no runtime mas nao no mapa, por isso
 * vm_region_name() devolvia "unmapped" para a heap real do jogo -- e o campo
 * region= do MEMORY/OOB da Fase 0 foi medido atraves dessa lente.
 * vm.h e' a fonte unica: quem duplicava estes valores ganha um _Static_assert.
 * So' entram intervalos confirmados por codigo (ver o plano da Fase 1). */
#define VM_LV2_HEAP_BASE    0x40000000u   /* == SYS_MEM_ALLOC_BASE  */
#define VM_LV2_HEAP_END     0x60000000u   /* == SYS_MEM_ALLOC_END   */
#define VM_SYS_VM_BASE      0x60000000u   /* == SYS_VM_REGION_BASE  */
#define VM_SYS_VM_END       0x70000000u   /* == SYS_VM_REGION_END   */
#define VM_RSX_LOCAL_BASE   0xC0000000u   /* RSX local real         */
#define VM_RSX_LOCAL_END    0xD0000000u   /* == localAddress+localSize do GCM */
#define VM_TLS_IMG_BASE     0x10F00000u   /* == PPU_TLS_IMG         */
#define VM_TLS_IMG_WINDOW   0x00010000u   /* == PPU_TLS_WINDOW      */
```

- [ ] **Passo 4: corrigir `vm_region_name`**

Substituir o corpo (`:332-343`) por:

```c
static inline const char* vm_region_name(uint32_t addr)
{
    /* Fase 1: TLS antes de "rsx". A janela TLS (0x10F00000) cai dentro do
     * intervalo VM_RSX_BASE nominal (0x10000000..0x20000000), por isso a
     * ordem importa -- sem esta linha a imagem TLS reporta "rsx", como
     * acontecia ate' aqui. */
    if (addr >= VM_TLS_IMG_BASE && addr < VM_TLS_IMG_BASE + VM_TLS_IMG_WINDOW)
        return "tls";
    if (addr >= VM_MAIN_MEM_BASE && addr < VM_MAIN_MEM_BASE + VM_MAIN_MEM_SIZE)
        return "main";
    if (addr >= VM_STACK_BASE && addr < VM_STACK_BASE + VM_STACK_REGION)
        return "stack";
    if (addr >= VM_SPU_BASE && addr < VM_SPU_BASE + 8 * VM_SPU_WINDOW_SIZE)
        return "spu";
    if (addr >= VM_LV2_HEAP_BASE && addr < VM_LV2_HEAP_END)
        return "lv2_heap";
    if (addr >= VM_SYS_VM_BASE && addr < VM_SYS_VM_END)
        return "sys_vm";
    if (addr >= VM_RSX_LOCAL_BASE && addr < VM_RSX_LOCAL_END)
        return "rsx_local";
    /* VM_RSX_BASE (0x10000000) e' o intervalo RSX NOMINAL do mapa antigo; a
     * RSX local real e' rsx_local, acima. Mantido por compatibilidade com
     * vm_is_valid_addr(), que o usa. */
    if (addr >= VM_RSX_BASE && addr < VM_RSX_BASE + VM_RSX_SIZE)
        return "rsx";
    return "unmapped";
}
```

- [ ] **Passo 5: `_Static_assert` contra drift**

Em `runtime/syscalls/sys_memory.c`, acrescentar `#include "../memory/vm.h"` aos
includes e, a seguir aos `#define`:

```c
_Static_assert(SYS_MEM_ALLOC_BASE == VM_LV2_HEAP_BASE,
               "mapa divergiu: vm.h e' a fonte unica (Fase 1)");
_Static_assert(SYS_MEM_ALLOC_END  == VM_LV2_HEAP_END,
               "mapa divergiu: vm.h e' a fonte unica (Fase 1)");
```

Em `runtime/syscalls/sys_vm.h`, a seguir aos `#define` (`:38-39`), o par
equivalente para `SYS_VM_REGION_BASE`/`_END` contra `VM_SYS_VM_BASE`/`_END`,
acrescentando o include de `vm.h` se ainda não existir.

Em `runtime/ppu/ppu_loader.cpp`, a seguir a `:390`:

```cpp
static_assert(PPU_TLS_IMG    == VM_TLS_IMG_BASE,   "mapa TLS divergiu (Fase 1)");
static_assert(PPU_TLS_WINDOW == VM_TLS_IMG_WINDOW, "mapa TLS divergiu (Fase 1)");
```

- [ ] **Passo 6: correr e confirmar que PASSA**

```bash
./runtime/trace/tests/run_tests_mem_trace.sh
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
./scripts/smoke_macos.sh --no-gui
```

Esperado: cenário `region` verde com os oito casos novos; build limpo (os
`_Static_assert` provam que não há drift); smoke PASS.

- [ ] **Passo 7: commit**

```bash
git add runtime/memory/vm.h runtime/syscalls/sys_memory.c runtime/syscalls/sys_vm.h \
        runtime/ppu/ppu_loader.cpp runtime/trace/tests/test_ps3_mem_trace.c
git commit -m "fix(fase1): vm.h passa a ser a fonte unica do mapa de regioes

vm_region_name conhecia 4 regioes e devolvia 'unmapped' para tudo o resto --
incluindo a heap REAL do jogo (0x40000000-0x60000000), a RSX local real
(0xC0000000) e sys_vm; a imagem TLS caia no intervalo RSX nominal e reportava
'rsx'. O campo region= do MEMORY/OOB da Fase 0 foi medido atraves dessa lente.

So' entraram intervalos confirmados por codigo. Quem duplicava as constantes
(sys_memory.c, sys_vm.h, ppu_loader.cpp) ganhou _Static_assert: drift passa a
ser erro de compilacao."
```

---

## Task 5: Registar as ranges comitadas do `sys_memory`

**Ficheiros:**
- Modificar: `runtime/syscalls/sys_memory.c:101,174,383,491,570`
- Modificar: `runtime/trace/tests/test_ps3_heap.c` (teste de registo)

**Interfaces:**
- Consome: `ppu_register_committed_range(uint32_t lo, uint32_t hi)`
  (`ppu_loader.cpp:494`).
- Produz: nada de novo.

**O problema medido:** 5 chamadas a `vm_commit` e **0** a
`ppu_register_committed_range`. A memória é comitada mas nunca registada, por
isso `vm_uncommitted_guard` trata-a como não-comitada e engole escritas a
memória que o jogo legitimamente alocou.

**Esta é a tarefa que muda comportamento por omissão** — daí a Task 7.

- [ ] **Passo 1: escrever o teste que falha**

Em `runtime/trace/tests/test_ps3_heap.c`, com um duplo de
`ppu_register_committed_range` que conta chamadas e guarda o último par:

O teste já existente chama a API real `sys_memory_allocate(&ctx)` com um
`ppu_context` (ver `test_ps3_heap.c:137,172`); seguir esse padrão. O duplo de
`ppu_register_committed_range` vive no próprio ficheiro de teste — o símbolo
real está em `ppu_loader.cpp`, que este teste não liga.

```c
/* Fase 1 Task 5: cada vm_commit bem-sucedido tem de registar a range, senao
 * o vm_uncommitted_guard engole escritas a memoria legitimamente alocada. */
static unsigned g_reg_calls = 0;
static uint32_t g_reg_lo = 0, g_reg_hi = 0;

/* Duplo do simbolo definido em ppu_loader.cpp:494 (nao ligado por este teste). */
void ppu_register_committed_range(uint32_t lo, uint32_t hi)
{
    g_reg_calls++; g_reg_lo = lo; g_reg_hi = hi;
}

static void test_alloc_regista_range(void)
{
    g_reg_calls = 0; g_reg_lo = 0; g_reg_hi = 0;

    /* Mesmo padrao de test_ps3_heap.c:137 -- sys_memory_allocate le os
     * argumentos do ppu_context (r3 = size, r4 = flags, r5 = ponteiro de
     * saida do EA) e devolve int64_t. Preencher o ctx como os testes ja
     * existentes fazem antes desta chamada. */
    ppu_context ctx;
    heap_test_ctx_init(&ctx, /*size=*/0x10000u);
    int64_t rc = sys_memory_allocate(&ctx);

    check(rc == CELL_OK,    "alloc devolveu CELL_OK");
    check(g_reg_calls == 1, "ppu_register_committed_range chamado 1x");
    check(g_reg_hi - g_reg_lo == 0x10000u,
          "range registada cobre exactamente o tamanho pedido");
}
```

`heap_test_ctx_init` é o helper que os testes existentes já usam para preparar o
`ppu_context` antes de `sys_memory_allocate`; se ainda não estiver extraído,
extraí-lo do corpo de `test_alloc_free` (`test_ps3_heap.c:137` e seguintes) como
primeiro passo desta tarefa, sem alterar o comportamento dos testes actuais.

- [ ] **Passo 2: correr e confirmar FALHA**

```bash
./runtime/trace/tests/run_tests_heap.sh
```

Esperado: **FALHA** com `g_reg_calls == 0`.

- [ ] **Passo 3: registar em cada sítio**

Nos sítios `:101`, `:174`, `:383` e `:570`, que já testam o retorno, registar no
ramo de sucesso. Padrão (adaptar o nome da variável de endereço e de tamanho a
cada sítio):

```c
    if (vm_commit(alloc_addr, size) != CELL_OK) {
        /* ... tratamento de erro existente, inalterado ... */
    }
    /* Fase 1: sem este registo o vm_uncommitted_guard trata a memoria como
     * nao-comitada e ENGOLE escritas do jogo a memoria que ele proprio
     * alocou. Medido: 5 vm_commit, 0 registos. */
    ppu_register_committed_range(alloc_addr, alloc_addr + size);
```

O sítio `:491` **ignora o retorno** (`vm_commit(alloc_addr, size);`). Passar a
verificar antes de registar:

```c
    if (vm_commit(alloc_addr, size) == CELL_OK)
        ppu_register_committed_range(alloc_addr, alloc_addr + size);
```

Declarar no relatório que este sítio passou a verificar um retorno que antes
descartava — é mudança de comportamento, ainda que correcta.

- [ ] **Passo 4: declarar o símbolo**

Se `sys_memory.c` ainda não vir a declaração, acrescentar junto aos includes:

```c
/* Definido em runtime/ppu/ppu_loader.cpp:494 (compilado por jogo, nao entra na
 * lib). Declarado aqui porque sys_memory.c ENTRA na lib: o simbolo resolve no
 * link do boot_gow2. */
extern void ppu_register_committed_range(uint32_t lo, uint32_t hi);
```

**Cuidado (G4):** `sys_memory.c` entra na `libps3recomp_runtime.a` em todas as
plataformas. Se algum consumidor da lib não ligar `ppu_loader.cpp`, o símbolo
fica por resolver. Verificar que as suites e o `smoke_macos.sh` continuam a
ligar; se alguma partir, o padrão do repo é um stub fraco em
`runtime/trace/tests/trace_link_stubs.h`.

- [ ] **Passo 5: correr e confirmar que PASSA**

```bash
./runtime/trace/tests/run_tests_heap.sh
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
./scripts/smoke_macos.sh --no-gui
runtime/spu/tests/run_tests.sh   # tem de continuar em 21 passed, 0 failed
```

- [ ] **Passo 6: commit**

```bash
git add runtime/syscalls/sys_memory.c runtime/trace/tests/test_ps3_heap.c
git commit -m "fix(fase1): sys_memory regista as ranges que comita

Medido: 5 chamadas a vm_commit, 0 a ppu_register_committed_range. A memoria
era comitada mas nunca registada, por isso o vm_uncommitted_guard tratava-a
como nao-comitada e engolia escritas do jogo a memoria que ele proprio alocou.

O sitio :491 ignorava o retorno de vm_commit; passa a verificar antes de
registar. Muda comportamento por omissao -- o baseline e' re-medido na Task 7."
```

---

## Task 6: Experimentos E1 e E2

**Ficheiros:**
- Criar: `docs/bringup/gow2/FASE1/mascaras-e1-e2.md`

**Interfaces:**
- Consome: tudo das Tasks 1–5.
- Produz: as contagens e atribuições que o relatório final da Task 7 cita.

**Regras de run (G6):** `PS3_NO_RSX=1` **e** `PS3_RSX_BACKEND=trace`;
`PS3_ENGINE_ROOT` obrigatório; morte **por PID**; **nunca** `pkill`; logs no
scratchpad, nunca commitados, **não apagar no fim**. `vm commit (sys_memory
window) failed` é flake conhecida: repetir o run e **registar quantas
repetições** foram precisas.

- [ ] **Passo 1: E1 — quanto e de quem, sem alterar comportamento**

Um run com as máscaras **ligadas** e `PS3_TRACE_MEMORY=1`. Depois:

```bash
SP=/private/tmp/.../scratchpad
grep -c 'mask=uncommitted_guard' "$SP/f1_e1.log"
grep -c 'mask=tblsize_guard'     "$SP/f1_e1.log"
grep 'SWALLOWED_TRUNCATED'       "$SP/f1_e1.log"
# top de funcoes guest atribuidas
grep '| SWALLOWED |' "$SP/f1_e1.log" | awk -F' \\| ' '{print $3}' | sort | uniq -c | sort -rn | head
# regioes afectadas
grep -o 'region=[a-z_]*' "$SP/f1_e1.log" | sort | uniq -c | sort -rn
```

- [ ] **Passo 2: E2 — um run por máscara, mais controlo**

| Run | Configuração | Log |
|---|---|---|
| controlo | nenhuma `PS3_MASK_OFF_*` | `f1_e2_controlo.log` |
| E2-a | `PS3_MASK_OFF_UNCOMMITTED=1` | `f1_e2_uncommitted.log` |
| E2-b | `PS3_MASK_OFF_TBLSIZE=1` | `f1_e2_tblsize.log` |

**Nunca as duas ao mesmo tempo** — desligar tudo de uma vez dá um crash e zero
informação sobre qual máscara importava.

Para cada run, extrair com o parser da Fase 0:

```bash
.venv/bin/python3 tools/trace_parse.py "$SP/f1_e2_uncommitted.log" --json > "$SP/f1_e2_a.json"
```

e registar: último checkpoint atingido, primeiro erro, e se o boot chegou mais
longe, menos longe, ou igual ao controlo.

- [ ] **Passo 3: escrever o relatório**

`docs/bringup/gow2/FASE1/mascaras-e1-e2.md`, com uma secção por máscara:

- contagem total engolida (e se o cap foi atingido, o total recuperado do
  `SWALLOWED_TRUNCATED`, declarado como **limite inferior**);
- top de funções guest atribuídas, com os símbolos como aparecem;
- distribuição por região (agora que `region=` diz a verdade);
- até onde o boot chega sem a máscara, comparado com o controlo;
- **veredicto por máscara**: a máscara esconde corrupção real, ou compensa um
  bug nosso de mapeamento?

Cada afirmação declara **in-boot**, **offline-unit** ou **não-exercitado** (G5).
Se o `tblsize_guard` não disparar na janela, dizê-lo — não inventar.

- [ ] **Passo 4: commit**

```bash
git add docs/bringup/gow2/FASE1/mascaras-e1-e2.md
git commit -m "docs(fase1): relatorio E1/E2 — o que cada mascara engole e o que muda sem ela"
```

---

## Task 7: Re-medição do baseline

**Ficheiros:**
- Criar: `docs/bringup/gow2/FASE1/baseline-pos-fase1.json`
- Criar: `docs/bringup/gow2/FASE1/attempt-1.md`
- Modificar: `docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md` (estado da Fase 1)

**Interfaces:**
- Consome: `scripts/baseline_run.sh`, `tools/baseline_report.py`,
  `tools/trace_parse.py` (`diff_baseline`) — todos da Fase 0.

**Porquê:** a Task 5 muda comportamento por omissão. Sem re-medir, perde-se a
comparabilidade com o artefacto da Fase 0.

- [ ] **Passo 1: correr o baseline**

```bash
./scripts/baseline_run.sh --runs 3 --out "$SP/f1_baseline"
```

Repetir qualquer run que apanhe a flake `vm commit (sys_memory window) failed`
e **registar quantas repetições** foram precisas.

- [ ] **Passo 2: comparar com o da Fase 0**

```bash
.venv/bin/python3 tools/trace_parse.py "$SP/f1_baseline/run_1.log" \
    --baseline docs/bringup/gow2/FASE0/baseline.json --json | \
  .venv/bin/python3 -c 'import json,sys; print(json.dumps(json.load(sys.stdin)["diff_baseline"], indent=2))'
```

- [ ] **Passo 3: escrever `attempt-1.md`**

No formato **exacto** de 11 secções que o plano de origem manda (linhas
573-597): `# Checkpoint: …`, `## Hipótese`, `## Baseline`, `## Alteração`,
`## Testes unitários`, `## Teste de integração`, `## Resultado esperado`,
`## Resultado observado`, `## Primeiro evento divergente`, `## Evidências`,
`## Decisão`, `## Próxima tentativa`.

**Qualquer diferença face ao baseline da Fase 0 tem de ser explicada** — não
basta registar que mudou. Se não mudar, isso é resultado válido e significa que
as ranges por registar não afectavam o caminho medido; dizê-lo assim.

Repetir as ressalvas de leitura que a Fase 0 estabeleceu, senão os números
lêem-se como defeitos: `threads_bloqueadas` e `fios_pendentes` significam
**"aberto no momento do kill"**, não "vazado"; `SHUTDOWN` nunca dispara;
`FIRST_PRESENT` é estruturalmente impossível sob o backend `trace`.

- [ ] **Passo 4: actualizar o plano de origem**

Na secção Fase 1 de `docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md`,
registar o estado real e apontar para os artefactos. **Declarar explicitamente
que o C1 `BOOT_MEMORY_OK` NÃO foi atingido** e o que falta: validação de
headers/footers, detecção de double-free/overlap/split, modos
`strict`/`diagnostic`, e o buraco das ~2110 escritas directas a `vm_base`.

- [ ] **Passo 5: verificar que não há dados de jogo**

```bash
grep -oE '/Users/[A-Za-z]+|EBOOT|\.psarc' docs/bringup/gow2/FASE1/baseline-pos-fase1.json || echo "limpo"
```

- [ ] **Passo 6: commit**

```bash
git add docs/bringup/gow2/FASE1/baseline-pos-fase1.json \
        docs/bringup/gow2/FASE1/attempt-1.md \
        docs/IMPLEMENTATION_PLAN_GAME_RUNTIME_BRINGUP.md
git commit -m "docs(fase1): baseline re-medido apos desmascarar + mapa corrigido

3 runs medidos e comparados com o baseline da Fase 0 via diff_baseline.
Declara explicitamente que o C1 BOOT_MEMORY_OK NAO foi atingido e o que falta."
```

---

## O que este plano não faz

Declarado para o aceite não ser lido como mais do que é:

- não valida headers nem footers do allocator;
- não detecta double-free, overlap nem split inválido;
- não implementa os modos `strict`/`diagnostic`;
- não fecha o buraco das ~2110 escritas directas a `vm_base` (spec §7.1) — a
  contagem de `SWALLOWED` é por isso um **limite inferior**;
- não instrumenta as duas máscaras `type15_*` do repo do jogo (spec §7.2);
- não toca no lifter nem exige re-lift — a mina do re-lift (spec §7.3)
  continua por resolver e bloqueia trabalho futuro;
- **não atinge o C1 `BOOT_MEMORY_OK`.**
