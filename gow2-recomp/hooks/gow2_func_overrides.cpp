/* gow2_func_overrides.cpp -- corpos dos WEAK OVERRIDES host do GoW2.
 *
 * Ver o cabecalho de gow2_func_overrides.h para o contrato do simbolo, para a
 * diferenca face ao mid-asm da Fase 17 e para a razao de a macro
 * GOW2_FUNC_OVERRIDE nao ser cosmetica.
 *
 * ESTADO NESTE PLANO (19-03): O PILOTO DE IDENTIDADE + DUAS MIGRACOES REAIS.
 * --------------------------------------------------------------------------
 * O 19-01 mediu OFFLINE que o mecanismo funciona (nm -m + execucao real de um
 * binario ligado com override). O 19-02 ligou a ponta HOST: TU versionada,
 * compilada e ligada pelo build_macos.sh, e provou IN-BOOT que o override que
 * ganha o link e' este ficheiro e nao o wrapper fraco do lift -- com um
 * override de IDENTIDADE sobre `func_00010230` (o entry point do guest), que
 * continua ca' em baixo e continua a ser o sensor do caminho de link.
 *
 * Este plano (19-03) acrescenta COMPORTAMENTO, e so' o que o mecanismo weak e'
 * o unico a poder fazer:
 *
 *   func_000CE03C  pad de setjmp/longjmp do CE03C (WINDOWS.md #8). ENVOLVE o
 *                  corpo natural -- um [[midasm_hook]] corre AO LADO de uma
 *                  instrucao e nao pode envolver nada.
 *   func_0002F3F0  bounds-check do hash de nomes do registry 393E0. Precisa de
 *                  um RETURN ANTECIPADO (e de um tecto no numero de bytes
 *                  lidos) -- outra coisa que um mid-asm nao faz.
 *
 * PORQUE `func_00010230` E NAO OUTRA (escolha medida, nao gosto)
 * -------------------------------------------------------------
 *  - E' o ENTRY POINT do guest: a EBOOT.ELF tem `entry OPD 0x00519570`, que
 *    resolve para code=0x00010230 (lido do proprio ELF nesta sessao; o
 *    runtime/ppu/ppu_loader.cpp ja' lhe chama "the literal entry point
 *    0x00010230"). Logo e' chamada EXACTAMENTE UMA VEZ, e sempre -- se o boot
 *    arranca, o override correu. Nenhuma outra candidata da' essa garantia sem
 *    depender do caminho da intro.
 *  - `ppu_run` chama-a pela `function_table` (ppu_loader.cpp: resolve o OPD,
 *    procura o code addr na tabela, chama `fn`). E' o cenario L4 do 19-01 --
 *    o mais arriscado, porque a referencia esta' na MESMA TU do wrapper fraco
 *    e o compilador poderia curto-circuitar. Medido verde la', e aqui e' o
 *    caminho REAL do jogo, nao um teste.
 *  - E' inicio de funcao liftada e tem tamanho EXACTO conhecido:
 *    functions.json diz start=0x00010230 end=0x00010254 -> size 0x24. O
 *    `size` do TOML nao e' aproximado (o campo e' registado e nao consumido --
 *    19-01 -- mas escreve-se o numero verdadeiro).
 *  - NENHUM patch_*.py lhe toca (verificado por grep sobre os 140 patches: a
 *    unica ocorrencia de 00010230 e' um COMENTARIO de cadeia de chamadas em
 *    patch_introseq_probe.py). Um piloto patchado partiria o
 *    apply_all_patches.sh, porque com a emissao ligada a forma
 *    `void func_XXXXXXXX(ppu_context* ctx) {` deixa de existir no lift para a
 *    funcao declarada (passa a `PPC_FUNC_IMPL(...)`), e um patch que a procure
 *    por texto nao a encontra.
 *  - Uma vez por boot = custo zero. O 19-01 mediu que a -O0 (o default do
 *    build) o wrapper e' um `bl` real; num piloto chamado por frame isso seria
 *    ruido no smoke de perf.
 *
 * PORTABILIDADE (G4 -- Windows nao regride): zero tokens Win32. Este ficheiro
 * so' e' compilado pelo build_macos.sh; o build_d3d.sh nao o toca.
 */
#include "gow2_func_overrides.h"

#include "ppu_memory.h"   /* vm_read8 / vm_read32 / vm_write32 / vm_write8 */

#include <setjmp.h>       /* setjmp / jmp_buf -- o pad do CE03C (ver abaixo) */

#include <cstdio>
#include <cstdlib>

/* ---------------------------------------------------------------------------
 * Dependencias do runtime e do lift.
 *
 * Mesmo arranjo do gow2_midasm_hooks.cpp: nenhuma destas entidades tem header
 * publico no motor (o proprio ppu_loader.cpp e varios syscalls declaram-nas
 * localmente com `extern`), por isso sao declaradas aqui.
 * ------------------------------------------------------------------------ */
extern "C" {

/* Alvo do longjmp do pad do CE03C. Definido em runtime/ppu/ppu_loader.cpp
 * (`extern "C" void* g_ce03c_play_abort = nullptr;`) e disparado de dentro de
 * `ps3_indirect_call`, no ramo [ICALL-ASCII], quando o FO residual fica preso
 * a saltar para uma string depois do StartSeq#2 real:
 *     longjmp(*(jmp_buf*)g_ce03c_play_abort, 1);
 * Enquanto ninguem o armar vale NULL e o ramo nunca dispara. */
extern void* g_ce03c_play_abort;

/* Estado do movie player HLE: EA do hook de EOS sticky. */
extern uint32_t g_movie_eos_ea;

/* libs/codec/cellVdec.c -- para o ramo de abort: para os drivers async para o
 * PICOUT nao continuar a bater no path-as-code sob o giant lock. */
void cellVdec_stop_all_for_play_abort(void);

} /* extern "C" */

/* Cross-fragment trampoline pointer -- a MESMA declaracao que o ppu_lifter.py
 * escreve no preambulo de toda a TU liftada e que casa com a definicao do
 * runtime/ppu/ppu_loader.cpp. Em Apple TEM de ser `thread_local` do C++ (ver a
 * nota longa no gow2_midasm_hooks.cpp: um `extern "C" __thread` nao pede o
 * wrapper TLS do Itanium e o link morre). */
#if defined(_MSC_VER)
extern "C" __declspec(thread) void (*g_trampoline_fn)(void*);
#elif defined(__APPLE__)
extern "C" thread_local void (*g_trampoline_fn)(void*);
#else
extern "C" __thread void (*g_trampoline_fn)(void*);
#endif

/* Funcao liftada do guest, com linkagem C++ (o lifter emite-as sem extern "C").
 * E' o alvo do trampolim que o CE03C arma na cauda. */
void func_000CE01C(ppu_context* ctx);

/* Corpo liftado original do entry point. Declarado, nao definido: a definicao
 * (`PPC_FUNC_IMPL(func_00010230)`) vem do chunk liftado. Se o lift for gerado
 * SEM a emissao ligada, este simbolo nao existe e o link parte alto -- e o
 * guard do build_macos.sh trata desse caso ANTES, saltando a compilacao. */
PPC_FUNC_IMPL_DECL(func_00010230);

/* Probe gated por env var, OFF por omissao (regra 6 do CLAUDE.md): com
 * PS3_FUNCOVR_TRACE ausente/vazio/"0" este ficheiro e' um no-op exacto sobre o
 * baseline. Ligado, imprime a prova IN-BOOT de que quem correu foi o override
 * host e nao o wrapper fraco do lift -- a string nao existe em lift nenhum. */
static bool gow2_funcovr_trace(void)
{
    static int on = -1;
    if (on < 0) {
        const char* e = getenv("PS3_FUNCOVR_TRACE");
        on = (e && e[0] && e[0] != '0') ? 1 : 0;
    }
    return on != 0;
}

/* ---------------------------------------------------------------------------
 * func_00010230 -- entry point do guest (EA 0x00010230, size 0x24).
 *
 * PILOTO DA FASE 19 (plano 19-02). Override de IDENTIDADE: o unico efeito com
 * o probe desligado e' um `bl` a mais para o corpo original. Nao le nem escreve
 * um so' campo de `ctx` -- de proposito: assim o override nao depende de o
 * layout do `ppu_context` do motor casar com o do lift (casam, verificado no
 * 17-02, mas nao depender e' mais barato do que voltar a prova-lo).
 * ------------------------------------------------------------------------ */
GOW2_FUNC_OVERRIDE(func_00010230)
{
    if (gow2_funcovr_trace()) {
        static int n = 0;
        if (n++ < 4) {
            fprintf(stderr,
                    "[FUNCOVR] func_00010230 (guest entry) -- override HOST "
                    "activo, chamada #%d\n", n);
            fflush(stderr);
        }
    }
    __imp_func_00010230(ctx);
}

/* ===========================================================================
 * func_000CE03C -- pad de setjmp/longjmp do CE03C (WINDOWS.md #8)
 *
 * A LACUNA QUE ISTO FECHA
 * -----------------------
 * O `patch_ce03c_introseq_block.py` instalava 116 linhas dentro do lift
 * gitignored. A Fase 17 migrou a PRIMEIRA metade -- o wait-idle -- para o
 * [[midasm_hook]] `Ce03cWaitIdle`, e declarou (17-03-SUMMARY, WINDOWS #8) que
 * a SEGUNDA metade nao cabia nesse mecanismo: o patch tambem ENVOLVIA o corpo
 * natural num `setjmp`, com um ramo de abort que chama
 * `cellVdec_stop_all_for_play_abort()` e reconstroi a freelist do media
 * object. Um `[[midasm_hook]]` corre AO LADO de uma instrucao; nao envolve
 * nada. Resultado medido: um lift feito com `--config` tinha o wait-idle e NAO
 * tinha o pad -- e nenhum script o repunha.
 *
 * O weak override e' o mecanismo que envolve. Daqui em diante o pad e' codigo
 * host versionado e sobrevive ao re-lift.
 *
 * ENVOLVER, NAO SUBSTITUIR (e porque e' esta a escolha)
 * ----------------------------------------------------
 * O corpo natural NAO e' copiado para aqui: chama-se `__imp_func_000CE03C`,
 * que e' o corpo que o lifter acabou de gerar -- e que ja' traz la' dentro,
 * como primeira instrucao, a chamada `gow2_midasm_Ce03cWaitIdle(ctx)` emitida
 * pelo [[midasm_hook]] da Fase 17. Isto tem duas consequencias que se querem:
 *
 *   1) UMA SO' FONTE DE VERDADE PARA O WAIT-IDLE. O override NAO chama
 *      gow2_midasm_Ce03cWaitIdle -- se o chamasse haveria DOIS pumps por
 *      entrada no CE03C (o do hook dentro do __imp_ e o do override), e o
 *      wait-idle nao e' idempotente: cada um esperaria ate' 600 ciclos de
 *      50 ms. Os dois mecanismos COMPOEM-SE; nao se duplicam.
 *   2) RE-LIFT-SAFE DE VERDADE. Se o lifter melhorar a traducao das 20
 *      instrucoes do corpo natural, o proximo lift traz a versao nova sem
 *      ninguem tocar neste ficheiro. Copiar o corpo para ca' (substituicao
 *      total) congelava-o -- exactamente o defeito que o patch tinha.
 *
 * A CAUDA, E PORQUE ELA E' REPETIDA NO RAMO DE ABORT
 * -------------------------------------------------
 * O corpo liftado termina com a cauda
 *     r9 = *(u32*)r30; r9 += 1; *(u32*)r30 = r9;
 *     g_trampoline_fn = func_000CE01C; return;
 * No caminho NORMAL essa cauda corre dentro do `__imp_` -- nada a fazer. No
 * caminho de ABORT o `longjmp` salta-a, e o bloco injectado corria-a na mesma
 * (esta a seguir ao pad, nao dentro dele). Por isso o ramo `else` repete-a
 * aqui, verbatim. Sem isto, um abort deixaria o contador por incrementar e o
 * trampolim por armar -- uma divergencia silenciosa face ao patch migrado.
 *
 * DIVERGENCIA DECLARADA (uma, e medida)
 * -------------------------------------
 * O bloco injectado forcava `r30 = _sv_r30` tambem no caminho NORMAL, imediata-
 * mente antes da cauda. Aqui isso e' inalcancavel: a cauda vive dentro do
 * `__imp_`. Nao e' regressao desta fase -- e' o comportamento que o lift
 * `--config` ja' tem desde o 17-03 (o pad nunca la' esteve) e cujo smoke esta'
 * medido verde nas Fases 17, 18 e 19-02 (st620_max=11). r30 e' callee-saved na
 * ABI PPC, portanto as tres chamadas guest do corpo natural tem de o repor.
 * No ramo de ABORT -- onde a ABI foi violada por um salto nao-local -- o
 * restauro E' feito, como o patch fazia.
 *
 * SEGUNDA DIFERENCA, TAMBEM DELIBERADA: o `_sv_r2` do bloco injectado era
 * capturado DEPOIS do wait-idle (o pad vinha a seguir a ele), e o hook mid-asm
 * deixa r2 "como o pump o devolveu" -- por escolha declarada no 17-03. Aqui e'
 * capturado A' ENTRADA, antes de tudo. Onde os dois divergiriam e' se o pump
 * sujasse o TOC; nesse caso o valor certo e' o da entrada, nao o sujo. Na
 * pratica sao iguais (o lifter resolve o TOC estaticamente -- o carimbo TOCFIX
 * que ele emite a seguir a cada chamada indirecta), mas a
 * escolha e' a que fica correcta se um dia deixarem de ser.
 * ======================================================================== */
PPC_FUNC_IMPL_DECL(func_000CE03C);

GOW2_FUNC_OVERRIDE(func_000CE03C)
{
    const uint64_t sv_r30 = ctx->gpr[30];
    const uint64_t sv_r2 = ctx->gpr[2];   /* o hang do FO pode sujar o TOC */

    jmp_buf jb;
    g_ce03c_play_abort = (void*)&jb;

    if (setjmp(jb) == 0) {
        /* Corpo natural do guest, tal como o lifter o gerou -- incluindo a
         * chamada ao mid-asm Ce03cWaitIdle na primeira linha e a cauda. */
        __imp_func_000CE03C(ctx);
    } else {
        /* Residual do FO depois do StartSeq#2 real: ps3_indirect_call ficou
         * preso a saltar para uma string e deu longjmp para ca'. */
        { static int n = 0; if (n++ < 4)
            fprintf(stderr, "[INTROSEQ] CE03C Play aborted "
                            "(FO residual after StartSeq#2)\n"); }
        g_trampoline_fn = 0;
        ctx->gpr[2] = sv_r2;   /* repor o TOC antes de tocar em memoria guest */

        cellVdec_stop_all_for_play_abort();

        /* O abort na pilha do host deixou o guest a meio do Play: parquear o
         * player e re-semear a freelist do media object, senao o open do WAD
         * a seguir a' intro bate no FREELIST-TAG-GUARD. */
        {
            const uint32_t toc = (uint32_t)sv_r2;
            const uint32_t mv = (toc >= 0x1124u)
                                    ? vm_read32(toc - 0x1124u) : 0u;
            if (mv >= 0x10000u && mv < 0x4F000000u) {
                vm_write32(mv + 0x620u, 0u);
                vm_write8(mv + 0x744u, 0);
                vm_write8(mv + 0x745u, 0);
                vm_write8(mv + 0x746u, 0);
            }
            g_movie_eos_ea = 0u;

            const uint32_t slot = (toc >= 0x1460u)
                                      ? vm_read32(toc - 0x1460u) : 0u;
            const uint32_t media = (slot && slot < 0x4F000000u)
                                       ? vm_read32(slot + 0x118u) : 0u;
            if (media >= 0x10000u && media < 0x4F000000u) {
                if (vm_read32(media + 0x16Cu) == 0u)
                    vm_write32(media + 0x16Cu, 1u);
                const uint32_t offs[8] = { 0x250u, 0x330u, 0x410u, 0x4F0u,
                                           0x5D0u, 0x6B0u, 0x790u, 0x870u };
                uint32_t chain = 0;
                for (int i = 7; i >= 0; i--) {
                    const uint32_t op = media + offs[i];
                    if (op >= 0x4F000000u) continue;
                    vm_write32(op + 0x0u, chain);
                    vm_write32(op + 0x90u, 0u);
                    vm_write32(op + 0x40u, 0u);
                    vm_write32(op + 0x44u, 0u);
                    chain = op;
                }
                vm_write32(media + 0x200u, chain);
                vm_write32(media + 0x218u, 0u);
                fprintf(stderr, "[INTROSEQ] CE03C post-abort freelist rebuild "
                                "media=0x%08X\n", media);
                fflush(stderr);
            } else {
                fprintf(stderr, "[INTROSEQ] CE03C post-abort freelist SKIP "
                                "toc=0x%08X media=0x%08X\n", toc, media);
                fflush(stderr);
            }
        }

        /* A cauda do corpo natural, que o longjmp saltou (ver acima). */
        ctx->gpr[30] = sv_r30;
        ctx->gpr[9] = vm_read32((uint32_t)ctx->gpr[30] + 0x0u);
        ctx->gpr[9] = ctx->gpr[9] + (int64_t)(1);
        vm_write32((uint32_t)ctx->gpr[30] + 0x0u, (uint32_t)ctx->gpr[9]);
        g_trampoline_fn = (void (*)(void*))func_000CE01C;
    }

    g_ce03c_play_abort = 0;
    ctx->gpr[2] = sv_r2;
}

/* ===========================================================================
 * func_0002F3F0 -- bounds-check do hash de nomes do registry 393E0
 *
 * O QUE ISTO MIGRA
 * ----------------
 * `recomp_mid_v2/patch_b71_2f3f0_guard.py`, que era o ESCRITOR de um bloco
 * feito a mao dentro do lift gitignored (perdido no re-lift de 26 jul, corpo
 * 2467 -> 1425 bytes, sem que nada se queixasse). O corpo liftado e' um
 * `for(;;)` que so' termina ao ler um byte ZERO a partir de r4:
 *
 *     loc_0002F404: ... r11 = vm_read8(r4 + 1); r4 += 1; ...
 *                   if (!(cr & 2)) goto loc_0002F404;      <-- SEM tecto
 *
 * `vm_read8` devolve 0 fora das regioes comitadas, mas o host macOS comita
 * 0x00000000..0x51000000, 0xC0000000..0xE0000000 e 0xD0000000..0xE0000000 --
 * 512 MB contiguos de framebuffer/stack quase todos NAO-nulos depois do
 * primeiro render, e 1,27 GB de heap com payload de WAD. Um ponteiro lixo la'
 * dentro faz UMA chamada percorrer centenas de milhoes de iteracoes; o 393E0
 * chama isto centenas de vezes.
 *
 * PORQUE E' WEAK E NAO MID-ASM (correccao ao `alvo` do ledger)
 * -----------------------------------------------------------
 * O ledger da Fase 16 tinha `alvo=midasm` para esta linha. Nao serve, por duas
 * razoes independentes: (a) o guard precisa de um RETURN ANTECIPADO quando o
 * ponteiro e' invalido, e um [[midasm_hook]] corre ao lado de uma instrucao e
 * deixa o corpo continuar; (b) o tecto de 256 caracteres so' se poe MUDANDO a
 * condicao do salto de volta ao topo do loop -- e um hook nao muda o fluxo. O
 * `alvo` foi corrigido para `weak` no PATCH_MIGRATION.tsv, com esta razao.
 *
 * ENVOLVER, NAO SUBSTITUIR -- excepto no caminho que nao pode ser envolvido
 * ------------------------------------------------------------------------
 * O caso normal chama `__imp_func_0002F3F0` (o corpo liftado, re-lift-safe).
 * So' ha' um caminho em que isso e' impossivel: quando NAO existe um NUL nos
 * primeiros 256 bytes, o loop do guest nao para, e o tecto tem de ser
 * aplicado por fora. Ai', e SO' ai', o hash e' calculado aqui -- e nao "a
 * gosto", mas replicando instrucao a instrucao o que o corpo liftado faz:
 *
 *     r0 = rlwinm(r10, 5, 0, 26)   ->  (h << 5) & 0xFFFFFFE0   ( = 32*h )
 *     r0 = r0 - r10                ->  h * 31
 *     r10 = r0 + r11               ->  h = h*31 + c
 *     ...
 *     r0 = mulhwu(r10, 0x0749CB29) ->  magia da divisao por 0x119 (281)
 *     r0 = rlwinm(r0, 29, 3, 31)   ->  >> 3
 *     r9 = r0 * 0x119 ; r0 = r10 - r9   ->  h % 281
 *
 * O acumulador so' importa nos 32 bits baixos (a magia le' (uint32_t)r10 e o
 * resultado final sai por (int32_t)), por isso a recorrencia em uint32_t e'
 * exacta -- nao uma aproximacao. A divisao usa a MESMA constante magica do
 * lift, e nao o operador `%`, para nao depender de o compilador escolher a
 * mesma sequencia.
 *
 * FIDELIDADE AO GUEST: para uma string legitima (rodata, nomes de registry
 * muito abaixo de 256 chars) nenhum ramo dispara e o resultado e' bit-a-bit o
 * do console -- o caminho normal E' o corpo do jogo. So' o patologico, que na
 * PS3 real nunca existiria, e' cortado. Nao ha' CRC bypass, magic estampado
 * nem guard mascarado: e' validacao de ponteiro + tecto de iteracao.
 *
 * SEM GATE DE ENV VAR, de proposito -- como no bloco original: e' um
 * bounds-check FUNCIONAL, nao um probe, e deixa-lo OFF por omissao seria repor
 * exactamente o pendurar que ele existe para evitar. Os dois fprintf tem tecto
 * de vida (16 e 8) e num boot saudavel nunca aparecem (medido: 0 ocorrencias
 * no smoke de 25 s).
 * ======================================================================== */
PPC_FUNC_IMPL_DECL(func_0002F3F0);

GOW2_FUNC_OVERRIDE(func_0002F3F0)
{
    const uint32_t sp = (uint32_t)ctx->gpr[4];

    /* (1) Ponteiro fora das bandas plausiveis do guest -> hash 0 e RETORNA.
     * 0x4F000000 e' o mesmo tecto de ponteiro-valido dos blocos B71/TYPE15. */
    if (sp < 0x10000u || sp >= 0x4F000000u) {
        ctx->gpr[3] = 0;
        { static int n = 0; if (n++ < 16)
            fprintf(stderr, "[B71] 2F3F0 bad str=0x%08X -> 0\n", sp); }
        return;
    }

    /* (2) Pre-scan com o mesmo alcance do bloco original: indices 0..256. */
    int len = 0;
    while (len <= 256 && (uint8_t)vm_read8(sp + (uint32_t)len) != 0u)
        len++;

    if (len <= 256) {
        /* Ha' NUL dentro do alcance: o loop do guest termina sozinho, e o
         * tecto nunca dispararia. Corre o corpo liftado tal e qual. */
        __imp_func_0002F3F0(ctx);
        return;
    }

    /* (3) Patologico: 257 bytes seguidos sem NUL. O corpo liftado nao pode ser
     * chamado (nao pararia); replica-se aqui, com o tecto de 256 caracteres. */
    { static int n = 0; if (n++ < 8)
        fprintf(stderr, "[B71] 2F3F0 cap str=0x%08X\n", sp); }

    uint32_t h = 0;
    for (int i = 0; i < 256; i++)
        h = (h * 31u) + (uint32_t)(uint8_t)vm_read8(sp + (uint32_t)i);

    const uint32_t q = (uint32_t)(((uint64_t)h * 0x0749CB29ull) >> 32) >> 3;
    const uint32_t rem = h - (q * 0x119u);

    ctx->gpr[4] = (uint64_t)(sp + 256u);   /* onde o cursor do guest ficaria */
    ctx->gpr[3] = (uint64_t)(int64_t)(int32_t)rem;
}

/* ===========================================================================
 * func_002C0498 -- Play re-entrado com st620 != 0 mantem o open FIOS em voo
 *
 * O QUE ISTO MIGRA
 * ----------------
 * `recomp_mid_v2/patch_fios_play_already_active.py`. O guest, em 0x002C0498,
 * faz teardown do container e volta ao corpo do Play:
 *
 *     bl 0x002BFF88      # teardown (cancel + limpa +8)
 *     b  0x002C0124      # re-entra no Play inteiro -> re-abre o m2v
 *
 * No recompilador esse teardown/re-open corre com o giant lock e perde a
 * corrida com o pool de ops do FIOS: o open#1 escreveu container+8, o Play#2
 * faz teardown, o open#2 apanha "SEM OP LIVRE" e escreve 0, e o poll do
 * estado 1 fica a ver io=0 para sempre. No hardware ha' janela para o op
 * cancelado ser libertado entre as duas coisas; aqui, muitas vezes, nao ha'.
 *
 * O fix e' o epilogo do proprio Play (o mesmo de 0x002C03FC): retorna ao
 * chamador sem teardown e sem re-open, deixando o open em voo intacto.
 *
 * PORQUE E' SUBSTITUICAO TOTAL E NAO "ENVOLVER"
 * --------------------------------------------
 * Ao contrario dos dois overrides acima, aqui NAO se chama `__imp_`: o fix
 * consiste precisamente em NAO correr o corpo natural (as duas instrucoes de
 * teardown+salto sao o defeito). Chamar `__imp_` reporia o bug. E' o unico
 * caso destes tres em que a substituicao total e' a leitura correcta -- e e'
 * tambem o que o patch legado ja' fazia: ele apagava o corpo e escrevia este.
 *
 * O corpo abaixo e' o texto do patch legado, VERBATIM (mesmos offsets, mesma
 * ordem, mesmas conversoes) -- migrar nao e' oportunidade para reescrever.
 *
 * NOTA DE LINK: sem chamada a `__imp_`, um build apontado a um lift SEM os
 * wrappers fracos falha com `duplicate symbol` (e nao com `undefined symbol`,
 * que e' o modo de falha do padrao "envolver") -- medido no 19-02, caso (2b).
 * O guard do build_macos.sh apanha os dois antes de compilar.
 * ======================================================================== */
GOW2_FUNC_OVERRIDE(func_002C0498)
{
    ctx->gpr[0] = vm_read64((uint32_t)(ctx->gpr[1] + 0x2D0));
    ctx->gpr[25] = vm_read64((uint32_t)(ctx->gpr[1] + 0x288));
    ctx->gpr[26] = vm_read64((uint32_t)(ctx->gpr[1] + 0x290));
    ctx->lr = ctx->gpr[0];
    ctx->gpr[27] = vm_read64((uint32_t)(ctx->gpr[1] + 0x298));
    ctx->gpr[28] = vm_read64((uint32_t)(ctx->gpr[1] + 0x2A0));
    ctx->gpr[29] = vm_read64((uint32_t)(ctx->gpr[1] + 0x2A8));
    ctx->gpr[30] = vm_read64((uint32_t)(ctx->gpr[1] + 0x2B0));
    ctx->gpr[31] = vm_read64((uint32_t)(ctx->gpr[1] + 0x2B8));
    ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0x2C0);
    ctx->gpr[3] = (int64_t)(int32_t)(0);
}
