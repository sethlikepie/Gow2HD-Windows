#!/usr/bin/env python3
"""Sonda do indice que escolhe o pool passado ao pop da free-list.

Como se chegou aqui (cadeia inteira medida a 2026-08-01)
--------------------------------------------------------
    main() func_0025C838
      -> func_002B2E74                     (7a de 9 chamadas, em linha recta)
           -> func_000B71B8 = B71          (9a de 11)
                -> #41 func_0010F5E8       lookup no registry de tipos
                     -> func_0039D51C      this=0x400C5048 (fabrica)
                          -> func_0039E40C this=0x400C61C8 (o objecto de Julho)
                               -> vt[0x60] = func_00254788  <- PENDURA AQUI
    func_002B2E04 (8a chamada do main) NUNCA E' ALCANCADA
      -> func_00242C94, o loop principal do jogo, nunca corre

E dentro do ciclo terminal repetem-se 16 escritas por volta:

    [vm] UNCOMMITTED write32 ... ra=func_00220284+0x1244/0x1274/0x12A4/0x12D4

que e' a assinatura de Julho: escreve-se 16 words atraves de um ponteiro vindo
do pop da free-list. E o pop, medido (`PS3_TRACE_FLHEAD`):

    [FLHEAD] TEXTO pool=0x00000005 pool+4=0x00000009 head=0x726D5F2E lr=0x00218364

**O ponteiro do pool nao e' um endereco: e' o numero 5.** (`head` sai
`0x726D5F2E` = "rm_." porque se le' `*(9)`, nao porque a lista tenha texto --
correccao ja' registada de uma leitura anterior errada.)

O sitio que o passa, em `func_002182A4`:

    r9  = rlwinm(r30, 4, 0, 27)        // r30 * 16
    r0  = *(r29 + 0)                   // divisor
    r9  = r28 * r9
    r9  = r9 / r0                      // divisao
    r9  = rlwinm(r9, 2, 0, 29)         // * 4
    r9  = r9 + r11                     // r11 = *(r29 + 0x14), base da tabela
    r3  = *(r9 + 0)                    // pool = tabela[indice]
    func_00263554(r3)                  // pop

O que esta sonda mede
---------------------
Todos os termos, para separar tres causas que dao o mesmo sintoma:

  - **indice fora de alcance** -> le'-se lixo para la' do fim da tabela
  - **tabela errada** (`r11` nao aponta para uma tabela de pools)
  - **entrada legitimamente 5** -> o defeito e' de quem a populou

Imprime so' quando o pool sai implausivel, ou tudo com `PS3_TRACE_POOLIDX=all`.

Nota sobre a divisao: o lifter emite `_b==0 ? 0 : a/b`. Em PPC o `divwu` por
zero deixa o resultado indefinido e nao faz trap, portanto o 0 e' uma escolha
ARBITRARIA do lifter, nao o comportamento do CELL. A sonda imprime o divisor
para se ver se esse caminho chega a ser exercitado.

Gate: `PS3_TRACE_POOLIDX` (vazio ou "0" = OFF, default). Cap por
`PS3_TRACE_POOLIDX_CAP` (default 60, `-1` = ilimitado). Read-only.

Uso:  patch_218340_pool_index_probe.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "POOLIDX-PROBE"

NEEDLE = (
    "        ctx->gpr[9] = ctx->gpr[9] + ctx->gpr[11];\n"
    "        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        ctx->gpr[3] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        ctx->lr = 0x00218364; func_00263554(ctx); DRAIN_TRAMPOLINE(ctx);\n"
)

REPL = (
    "        ctx->gpr[9] = ctx->gpr[9] + ctx->gpr[11];\n"
    "        ctx->gpr[9] = ppc_rldicl(ctx->gpr[9], 0, 32);\n"
    "        ctx->gpr[3] = vm_read32(ctx->gpr[9] + 0x0);\n"
    "        /* " + MARKER + ": de onde vem o pool passado ao pop */\n"
    "        { static int _on=-1; static int _all=0;\n"
    "          if(_on<0){ extern char* getenv(const char*);\n"
    "            const char* _e=getenv(\"PS3_TRACE_POOLIDX\");\n"
    "            _on=(_e&&*_e&&*_e!='0')?1:0; _all=(_e&&*_e=='a')?1:0; }\n"
    "          static int _cap=-2; if(_cap==-2){ extern char* getenv(const char*);\n"
    "            const char* _c=getenv(\"PS3_TRACE_POOLIDX_CAP\"); _cap=(_c&&*_c)?atoi(_c):60; }\n"
    "          if(_on){ uint32_t _pool=(uint32_t)ctx->gpr[3];\n"
    "            uint32_t _slot=(uint32_t)ctx->gpr[9];\n"
    "            uint32_t _base=(uint32_t)vm_read32(ctx->gpr[29] + 0x14);\n"
    "            uint32_t _div =(uint32_t)vm_read32(ctx->gpr[29] + 0x0);\n"
    "            int _mau=(_pool<0x10000u)||(_pool>=0x4F000000u);\n"
    "            if(_all||_mau){ static int _n=0; if(_cap<0||_n++<_cap){\n"
    "              fprintf(stderr,\"[POOLIDX] %s pool=0x%08X slot=0x%08X base=0x%08X \"\n"
    "                \"idx=%u divisor=%u r28=0x%08X r30=0x%08X obj=0x%08X\\n\",\n"
    "                _mau?\"MAU\":\"ok \", _pool,_slot,_base,\n"
    "                (_slot>=_base)?((_slot-_base)>>2):0xFFFFFFFFu, _div,\n"
    "                (uint32_t)ctx->gpr[28],(uint32_t)ctx->gpr[30],\n"
    "                (uint32_t)ctx->gpr[29]);\n"
    "              fflush(stderr); } } } }\n"
    "        ctx->lr = 0x00218364; func_00263554(ctx); DRAIN_TRAMPOLINE(ctx);\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY", 0
    n = t.count(NEEDLE)
    if not n:
        return t, "MISSING", 0
    return t.replace(NEEDLE, REPL), "APPLIED", n


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
            print("APPLIED  %-20s sites=%d" % (os.path.basename(path), n))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  agulha do indice do pool nao encontrada", file=sys.stderr)
        return 2
    print("patch_218340_pool_index_probe: applied=%d already=%d sites=%d"
          % (applied, already, sites))
    return 0


if __name__ == "__main__":
    sys.exit(main())
