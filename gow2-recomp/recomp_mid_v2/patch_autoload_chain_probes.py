#!/usr/bin/env python3
"""Sondas de entrada ao longo da cadeia que leva a' criacao da thread AUTO_LOAD.

Porque uma bisseccao e nao mais uma sonda solta
-----------------------------------------------
Medido a 2026-08-01: a thread `AUTO_LOAD` **nunca e' criada**, e o proprio
criador (`func_00146CB8`) **nunca corre** (`PS3_TRACE_ALCREATE` = 0 numa corrida
valida, R_PermA 20298800). Logo a parede esta a montante dele, e cada sonda
solta custa um rebuild de ~4 min. Esta bisseccao poe uma sonda em cada degrau
da cadeia de uma so' vez: o primeiro degrau silencioso e' a parede.

Cadeia, extraida do EBOOT.ELF por xref de `bl` com fronteiras de funcao REAIS
(alvos de bl, nao os fragmentos da tabela do lift):

    func_00010354
      -> func_0025C838
        -> func_002B2E04
          -> func_002B2DD0        <- o laco onde PS3_STUCK_ICALL_LIMIT dispara
            -> func_000B951C
              -> func_000B9204
                -> func_000B6440 / func_000B6714
                  -> func_000B5294
                    -> func_000BBDC8
                      -> func_000BB0B0   <- contem o bl para o criador (0x000BB1E4)
                        -> func_00146CB8 -> sys_ppu_thread_create("AUTO_LOAD")

O outro sitio de chamada do criador (0x000BBD70, em func_000BBB00) ja' foi
excluido: `PS3_TRACE_ALGATE` deu zero, e o fragmento que la' desemboca
(0x000BB9EC) nao tem **um unico** ramo nem ponteiro em todo o EBOOT. E nao e'
um fallthrough perdido pelo lifter -- em 0x000BB9E8 ha' um `b 0x000BB6C8`
explicito, verificado desmontando o binario. Hipotese de bug do lifter
REFUTADA por verificacao, nao por opiniao.

Interesse particular do degrau `func_002B2DD0`: e' onde o circuit-breaker
`PS3_STUCK_ICALL_LIMIT` conta ate' 2000 e chama exit(3). Se a cadeia morre a'
saida dele, o problema e' o laco a girar, nao um estado que falta.

Gate: `PS3_TRACE_ALCHAIN` (vazio ou "0" = OFF, default). Cada funcao imprime
so' a PRIMEIRA vez (e depois de cap vezes cala-se), para nao inundar um laco:
`PS3_TRACE_ALCHAIN_CAP` (default 3, `-1` = ilimitado). Read-only.

Uso:  patch_autoload_chain_probes.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se nenhuma funcao da cadeia casou.
"""
import os
import sys
import glob

MARKER = "ALCHAIN-PROBE"

# ordem = ordem da cadeia (de cima para baixo). O ultimo nao pertence a'
# cadeia: e' um CONTROLO -- uma funcao que se sabe correr (medida 3184 vezes a
# 2026-08-01 com PS3_TRACE_E6B4=all). Se o controlo imprimir e os degraus nao,
# o silencio dos degraus e' um facto sobre o jogo; se o controlo tambem calar,
# o instrumento e' que esta partido. Nao se le' um zero sem controlo -- foi a
# licao das quatro sondas que mentiram nesta sessao.
CHAIN = [
    "func_00010354",   # topo do call graph estatico
    "func_0025C838",   # o main(): nove chamadas em linha recta, sem condicionais
    # as sete chamadas do main() ANTES do loop principal, por ordem. Como sao
    # sequenciais e sem ramos, a ULTIMA que imprimir e' a que nao retorna.
    "func_002B37D4",   # 0x0025C868
    "func_00242700",   # 0x0025C870
    "func_002B4F04",   # 0x0025C878
    "func_0025C680",   # 0x0025C880
    "func_002B76EC",   # 0x0025C884
    "func_002B2EEC",   # 0x0025C88C
    "func_002B2E74",   # 0x0025C894 -- ENTRA e NAO RETORNA (medido)
    # as 11 chamadas de func_002B2E74, tambem em linha recta e sem ramos.
    "func_0024A7CC",   # 0x002B2E88
    "func_002AAC84",   # 0x002B2E90
    "func_002AC328",   # 0x002B2E98
    "func_002AB2F8",   # 0x002B2EA0
    "func_002B5C94",   # 0x002B2EA8
    "func_002B5508",   # 0x002B2EB0
    "func_002D2978",   # 0x002B2EB8
    "func_002287AC",   # 0x002B2EC0
    "func_000B71B8",   # 0x002B2EC8  -- o "B71" dos comentarios do lift
    "func_0023B654",   # 0x002B2ED0
    "func_002B7188",   # 0x002B2ED8
    "func_002B2E04",   # 0x0025C89C -- contem o `bl` para o loop principal
    "func_00242C94",   # O LOOP PRINCIPAL
    "func_002B2DD0",
    "func_000B951C",
    "func_000B9204",
    "func_000B6440",
    "func_000B6714",
    "func_000B5294",
    "func_000BBDC8",
    "func_000BB0B0",
    # o produtor CORRECTO da tabela de pools e o seu chamador -- medido
    # 2026-08-01: o objecto sao recebe *(obj+0x14) de func_0022851C+0xD8C
    # (chamado de func_00411A5C+0x1D8); o objecto MAU recebe um bloco cru
    # de func_00263178, de func_002B11B8. Saber quantos objectos o
    # produtor correcto cobre diz se o mau ficou de fora.
    "func_0022851C",
    "func_00411A5C",
    "func_0039E6B4",   # CONTROLO
]


def probe_for(fn, step):
    rotulo = "degrau %d/%d %s" % (step, len(CHAIN), fn)
    if fn == "func_0039E6B4":
        rotulo = "CONTROLO %s (tem de imprimir)" % fn
    return (
        "        /* " + MARKER + " */\n"
        "        { static int _on=-1; if(_on<0){ extern char* getenv(const char*);\n"
        "            const char* _e=getenv(\"PS3_TRACE_ALCHAIN\"); _on=(_e&&*_e&&*_e!='0')?1:0; }\n"
        "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
        "            const char* _c=getenv(\"PS3_TRACE_ALCHAIN_CAP\"); _cap=(_c&&*_c)?atoi(_c):3; }\n"
        "          if(_on){ static int _n=0; if(_cap<0 || _n++<_cap){\n"
        "            fprintf(stderr,\"[ALCHAIN] " + rotulo + " r3=0x%08X lr=0x%08X\\n\",\n"
        "              (uint32_t)ctx->gpr[3], (uint32_t)ctx->lr);\n"
        "            fflush(stderr); } } }\n"
    )


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = 0
    for i, fn in enumerate(CHAIN, 1):
        head = "void %s(ppu_context* ctx) {\n" % fn
        if t.count(head) != 1:
            continue
        t = t.replace(head, head + probe_for(fn, i), 1)
        n += 1
    if not n:
        return t, "MISSING", 0
    return t, "APPLIED", n


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = sites = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state, n = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            sites += n
            print("APPLIED  %-20s degraus=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  nenhuma funcao da cadeia encontrada", file=sys.stderr)
        return 2
    print("patch_autoload_chain_probes: applied=%d already=%d degraus=%d de %d"
          % (applied, already, sites, len(CHAIN)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
