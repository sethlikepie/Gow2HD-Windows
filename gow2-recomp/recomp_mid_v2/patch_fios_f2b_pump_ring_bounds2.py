#!/usr/bin/env python3
"""F2B-STREAM-PUMP: quantos bytes o pump escreve passa a ser regulavel.

Porque
------
O `patch_fios_f2b_pump_ring_bounds.py` (V1) parou o pump de escrever numa
janela hard-coded e limitou-o ao ring real do guest. Isso resolveu a corrupcao
medida (`tab[0x58]` deixou de ser destruido), mas passou a escrever DENTRO do
ring verdadeiro -- coisa que antes so' acontecia por acidente de sobreposicao.

E o pump nao actualiza `avail`/`cursor` no objecto de stream do guest. O
`f2b_stream_fill`, que corre logo a seguir, calcula o destino a partir desses
campos. Ou seja: e' possivel que o pump e o fill escrevam por cima um do outro
com posicoes de ficheiro diferentes.

Isso importa porque a parede seguinte e' exactamente "conteudo do WAD lido como
lixo": medido a 2026-08-01,

    [TYPESLOT] NULO idx=0x25F7C tipo=0x97DF tab=0x00868D48 slot=0x00000000

`tipo=0x97DF` nao e' um tipo (os validos observados sao todos < 0x40: 0x03,
0x05, 0x0F, 0x11, 0x15, 0x16, 0x19, 0x20), e `idx=0x25F7C` cai muito para fora
da tabela, logo o slot le' 0 e o despacho vai por ponteiro nulo.

**Hipotese a discriminar:** o pump a escrever no ring desalinha o stream que o
guest le'. Com `PS3_F2B_PUMP_BYTES=0` o pump nao escreve nada e o bloco fica
intacto no resto (rebind de limit/cursor + arranque do `f2b_stream_fill`) --
que e' precisamente a variavel que o gate cego `PS3_FIOS_STREAM_PUMP=0` nao
conseguia isolar, porque desligava o bloco inteiro (medido: st620 11->3,
StartSeq 2->0, R_PermA 1->0, filme 0).

O que faz
---------
Troca a constante que o V1 introduziu

    uint32_t pump_end = (_sz < ring_sz) ? _sz : ring_sz;

por uma versao regulada por `PS3_F2B_PUMP_BYTES`:

    0            -> o pump nao escreve nada (o fill faz o trabalho todo)
    N            -> no maximo N bytes
    (por definir) -> um ring, exactamente como o V1

Nunca escreve fora de `[base, base+cap)`: o `min` com `ring_sz` fica.

Depende do V1 (a agulha e' texto que o V1 emite). O nome ordena a seguir a
`patch_fios_f2b_pump_ring_bounds.py` no glob do apply_all_patches.sh.

Uso:  patch_fios_f2b_pump_ring_bounds2.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha do V1 nao existe.
"""
import os
import sys
import glob

MARKER = "PUMP-BYTES-KNOB"

NEEDLE = (
    "              /* PUMP-RING-BOUNDS: um ring, nao 20 voltas por cima de si proprio. */\n"
    "              uint32_t pump_end = (_sz < ring_sz) ? _sz : ring_sz;\n"
)

REPL = (
    "              /* PUMP-RING-BOUNDS: um ring, nao 20 voltas por cima de si proprio. */\n"
    "              /* " + MARKER + ": quantos bytes, regulavel. 0 = nao escrever nada\n"
    "               * (o f2b_stream_fill faz o trabalho todo). Por definir = um ring. */\n"
    "              uint32_t pump_end = (_sz < ring_sz) ? _sz : ring_sz;\n"
    "              { static int _pb=-1; if(_pb<0){ const char* _e=getenv(\"PS3_F2B_PUMP_BYTES\");\n"
    "                  _pb = (_e && *_e) ? (int)strtoul(_e, 0, 0) : -2; }\n"
    "                if(_pb >= 0 && (uint32_t)_pb < pump_end) pump_end = (uint32_t)_pb;\n"
    "                if(_pb >= 0){ static int _n=0; if(_n++<4){\n"
    "                  fprintf(stderr,\"[FIOSOPEN] F2B-STREAM-PUMP bytes=%u (PS3_F2B_PUMP_BYTES)\\n\",\n"
    "                    pump_end); fflush(stderr); } } }\n"
)


def patch_text(t):
    if MARKER in t:
        return t, "ALREADY"
    if t.count(NEEDLE) != 1:
        return t, "MISSING"
    return t.replace(NEEDLE, REPL, 1), "APPLIED"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lift = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "recomp_macos_v2")
    lift = os.path.abspath(lift)
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if not chunks:
        print("ERRO: nenhum ppu_recomp_*.cpp em %s" % lift, file=sys.stderr)
        return 2
    applied = already = 0
    for path in chunks:
        with open(path, "r", errors="replace") as fh:
            src = fh.read()
        out, state = patch_text(src)
        if state == "APPLIED":
            with open(path, "w") as fh:
                fh.write(out)
            applied += 1
            print("APPLIED  %s" % os.path.basename(path))
        elif state == "ALREADY":
            already += 1
            print("ALREADY  %s" % os.path.basename(path))
    if applied == 0 and already == 0:
        print("MISSING  a agulha do patch_fios_f2b_pump_ring_bounds.py (V1) nao existe -- "
              "correr o V1 primeiro", file=sys.stderr)
        return 2
    print("patch_fios_f2b_pump_ring_bounds2: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
