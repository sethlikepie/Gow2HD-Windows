#include <stdlib.h>
/* GoW2: register all four lifted SPU images with the workload dispatcher.
 *
 * Fingerprints are FNV-1a-64 of the extracted ELF (verified byte-identical to
 * the in-memory image at dispatch time). Entry LS addresses come from each
 * ELF's e_entry. spu1 is the intro MPEG decoder (dispatched during the SCE
 * logo movie); spu2/3 come up later in gameplay.
 */
#include <stdint.h>

typedef struct spu_context spu_context;
typedef void (*spu_lifted_entry_fn)(spu_context*);

extern void spu_workload_register_image(uint64_t fingerprint, spu_lifted_entry_fn fn,
                                        const char* name, int image_id);
extern void spu_workload_register_raw_image(uint64_t fingerprint, spu_lifted_entry_fn fn,
                                            const char* name, uint32_t raw_ls_base,
                                            int image_id);

extern void spu0_spu_func_00003070(spu_context* ctx);   /* spu0 e_entry 0x3070 */
extern void spu1_spu_func_00003050(spu_context* ctx);   /* spu1 e_entry 0x3050 */
extern void spu2_spu_func_00004080(spu_context* ctx);   /* spu2 e_entry 0x4080 */
extern void spu3_spu_func_00004080(spu_context* ctx);   /* spu3 e_entry 0x4080 */
extern void spu4_spu_func_00003050(spu_context* ctx);   /* spu4 e_entry 0x3050 (fp 0x9527C889B1945669) */
extern void spu5_spu_func_00003070(spu_context* ctx);   /* spu5 e_entry 0x3070 (fp 0x3512A7E99D34E0FF) */
extern void spu6_spu_func_00000A00(spu_context* ctx);   /* spu6 entry 0xA00 (fp 0xCEDB9A67A0C3A305 = SCREAM mixer PM) */
extern void spu0_spu_recomp_register(void);
extern void spu1_spu_recomp_register(void);
extern void spu2_spu_recomp_register(void);
extern void spu3_spu_recomp_register(void);
extern void spu4_spu_recomp_register(void);
extern void spu5_spu_recomp_register(void);
extern void spu6_spu_recomp_register(void);

extern void spu_begin_image(int image_id);

void gow2_register_spu_workloads(void)
{
    /* Each image in its own namespace (id = N + 1): they all share one LS
     * address space, and registered together as image 0 an indirect branch of
     * one image resolved to another image's function at the same address
     * (spu1 running spu0's 0x3AC8). Jobs take the id of their entry. */
    spu_begin_image(1); spu0_spu_recomp_register();
    spu_begin_image(2); spu1_spu_recomp_register();
    spu_begin_image(3); spu2_spu_recomp_register();
    spu_begin_image(4); spu3_spu_recomp_register();
    spu_begin_image(5); spu4_spu_recomp_register();
    spu_begin_image(6); spu5_spu_recomp_register();
    spu_begin_image(7); spu6_spu_recomp_register();
    /* The SCREAM policy module is a SPURS job manager: it DMAs the mixer job
     * -- the code segment of SPU ELF #2 (EBOOT 0x4EF400, 0xDBC0 B), the spu2
     * image -- to LS 0x4000 and branches into it. The job runs in the
     * module's context, so its functions must resolve under image 7 too. */
    spu2_spu_recomp_register();
    spu_begin_image(0);
    /* PS3_SPU0=0 (A/B, opt-in): leave spu0 unregistered (its jobs MISS). */
    if (!(getenv("PS3_SPU0") && getenv("PS3_SPU0")[0] == '0'))
        spu_workload_register_image(0xDE6DC3A5EA2BE487ull, spu0_spu_func_00003070, "gow2_spu0", 1);
    /* spu1 (dearch / EDGE-zlib) VERIFICADO no caminho intro->WAD (Task 4 do
     * plano 2): sob PS3_SPU1=1 o dispatch vai de MISS constante (~332/120s) a
     * HIT, e o job liftado RODA ATE O FIM e retorna limpo ("[SPUJOB] spu job
     * returned cleanly") em runs repetidas, SEM SPUCRASH e sem matar o host
     * (SEH do plano 1 protege). No boot ele faz DMA real de dearch: varre uma
     * tabela de descritores de 224 B (GET/PUT ~350 cada) e empurra na fila
     * lock-free (LFQPUSHBT). PORE'M o inflate do CONTEUDO dos WADs ainda NAO
     * foi exercido: R_LglScA/R_PermA abrem mas o jogo nao faz stream/dearchive
     * dos 20 MB do R_PermA dentro da janela da intro -- os HITs pos-WAD nao
     * geram DMA em massa. O gargalo agora e' UPSTREAM do spu1 (loader de asset
     * nao avanca ao consumo do WAD), nao o spu1 em si. Fica opt-in (nao default)
     * ate o inflate de membro ser observavel. Toggle: PS3_SPU1 ou PS3_SPU_ALL. */
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU1"))
        spu_workload_register_image(0x2A5C4E67A14505B8ull, spu1_spu_func_00003050, "gow2_spu1", 2);
    /* spu2/3 stay opt-in: their lifted entries may still fault mid-run. O host
     * esta' protegido nos DOIS lados agora -- VEH+ExitThread no Windows,
     * sigaction+siglongjmp em POSIX (spu_workload.c) -- portanto uma falta
     * aborta o job e nao o processo, e podem ser registados para bring-up sem
     * regredir o loop estavel. ATE 2026-07-20 esta nota dizia "thanks to SEH
     * isolation", o que era FALSO no macOS: o bloco SEH e' #ifdef _WIN32 e no
     * Mac a falta matava o processo inteiro (provado: exit 139 no
     * test_seh_isolation antes do fix).
     * Fine gates PS3_SPU2 / PS3_SPU3 allow enabling one at a time; PS3_SPU_ALL
     * enables both. NOT a default -- promocao continua a espera de uma
     * validacao de boot completa, agora por FALTA DE PROVA DE PROGRESSO (o job
     * fazer trabalho util), nao por medo de derrubar o host. Bloqueador
     * concreto no Mac: o job corre na thread do worker SPURS, medida em 512 KB
     * de stack (o Windows da'-lhe uma thread dedicada de 256 MB), portanto um
     * call graph fundo e' isolado de forma limpa mas nao chega ao fim. */
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU2"))
        spu_workload_register_image(0xABCD0BA4D18DED49ull, spu2_spu_func_00004080, "gow2_spu2", 3);
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU3"))
        spu_workload_register_image(0xED6A0C318DEB46C6ull, spu3_spu_func_00004080, "gow2_spu3", 4);
    /* spu4/spu5: SPURS-scheduler workloads the frontend dispatches post-AUTO_LOAD
     * (dumped via PS3_SPU_DUMP, lifted 515/448 fns; needed spu_pref_u32 helper).
     * Without them the `schedul` cond never signals and the frontend deadlocks at
     * the Bluepoint logo before the menu. Gated PS3_SPU4/PS3_SPU5 (host is SEH/
     * setjmp-isolated if a job faults). */
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU4"))
        spu_workload_register_image(0x9527C889B1945669ull, spu4_spu_func_00003050, "gow2_spu4", 5);
    if (getenv("PS3_SPU_ALL") || getenv("PS3_SPU5"))
        spu_workload_register_image(0x3512A7E99D34E0FFull, spu5_spu_func_00003070, "gow2_spu5", 6);
    /* spu6 = SCREAM mixer PM (fp 0xCEDB9A67A0C3A305, 11520B em 0x4FD980). The SPURS
     * kernel loads a policy module at LS 0xA00 and enters it there.
     * On for a normal play launch. PS3_SPU6=0 turns it off. A faulting job is
     * aborted by the setjmp landing pad; it does not have to stay opt-in. */
    {
        const char* e = getenv("PS3_SPU6");
        int on = getenv("PS3_SPU_ALL") || !e || (e[0] && e[0] != '0');
        if (on)
            spu_workload_register_raw_image(0xCEDB9A67A0C3A305ull, spu6_spu_func_00000A00,
                                            "gow2_spu6", 0xA00, 7);
    }
}

#if defined(__GNUC__)
__attribute__((constructor))
static void gow2_spu_autoregister(void) { gow2_register_spu_workloads(); }
#endif
