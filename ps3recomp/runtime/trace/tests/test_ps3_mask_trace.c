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

/* ps3_timebase_now() e' declarado `extern` por ps3_trace.c (usado em
 * ps3_trace_emit()) mas definido de verdade em runtime/syscalls/sys_timer.c
 * -- nao ligado aqui, o teste corre standalone. Mesmo stub de
 * test_ps3_mem_trace.c (irmao gemeo deste teste). */
uint64_t ps3_timebase_now(void)
{
    static uint64_t counter = 1;
    return counter++;
}

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

/* T2: gate ligado -> linha no formato G1 exacto, com todos os campos.
 * region=[a-z0-9_]+ (nao so' [a-z_]+): Fase 1 Task 4 fez vm_region_name()
 * classificar 0x40001000 como "lv2_heap" (antes: "unmapped", tambem
 * so' letras por coincidencia) -- nomes de regiao podem ter digito
 * (ver runtime/memory/vm.h:vm_region_name), o charset antigo era estreito
 * demais. */
static void t_format(void)
{
    regex_t re;
    const char* pat =
      "^\\[PS3T\\] [0-9]+ \\| (0x[0-9A-F]{8}|-) \\| [^|]* \\| MEMORY \\| SWALLOWED \\| "
      "ea=0x40001000 len=4 mask=uncommitted_guard op=write region=[a-z0-9_]+$";
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

/* T4: cheia -> cap respeitado e total recuperavel por SWALLOWED_TRUNCATED,
 * agenda GEOMETRICA (review da Task 2: a agenda linear original -- marco a
 * cada CAP eventos, igual ao MEMORY/OOB de ps3_mem_trace.c -- deu 65.056
 * linhas TRUNCATED para 4,1M de engolimentos medidos in-boot, afogando as
 * outras nove mascaras da Task 7). A agenda agora e': marco inicial em
 * CAP+1 e depois so' em potencias de dois acima disso (128, 256, 512, ...).
 * 257 eventos cobrem TRES marcos -- os dois EXTREMOS (65 e 256) e um
 * INTERMEDIO (128), fechando a lacuna que a review da Task 1 apontou (so'
 * se testavam os extremos). O teste tambem falha se a agenda regredir para
 * linear: com 257 eventos a linear daria 4 marcos (65,129,193,257), nao 3,
 * e teria count=129 (que a geometrica nunca produz). */
static void t_flood(void)
{
    cap_begin();
    for (int i = 0; i < 257; i++)
        ps3_mask_swallowed_report(PS3_MASK_TBLSIZE, 0x572BB4u, 4, "write", NULL);
    char* out = cap_end();

    int swallowed = 0, truncated = 0;
    for (char* p = out; (p = strstr(p, "| SWALLOWED |")) != NULL; p++) swallowed++;
    for (char* p = out; (p = strstr(p, "| SWALLOWED_TRUNCATED |")) != NULL; p++) truncated++;

    check(swallowed == PS3_MASK_SWALLOWED_CAP, "cheia: exactamente CAP linhas SWALLOWED");
    check(truncated == 3, "cheia: 3 marcos TRUNCATED geometricos (65, 128, 256) -- linear daria 4");
    check(strstr(out, "count=65") != NULL,  "TRUNCATED com count=65 (marco inicial)");
    check(strstr(out, "count=128") != NULL, "TRUNCATED com count=128 (marco intermedio)");
    check(strstr(out, "count=256") != NULL, "TRUNCATED com count=256 (marco final)");
    check(strstr(out, "count=129") == NULL, "NAO pode haver marco linear count=129 (provaria regressao p/ agenda antiga)");
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
