#!/usr/bin/env python3
"""F2B-STREAM-PUMP: parar de escrever numa janela guest HARD-CODED.

O defeito, medido a 2026-08-01 (2/2 corridas, byte a byte iguais)
---------------------------------------------------------------
O bloco `F2B-STREAM-PUMP` (instalado por patch_fios_f2b_open_block_install.py
no corpo de func_002B4274) despeja o ficheiro INTEIRO -- 20 169 344 bytes do
r_perma.wad_ps3, em 154 chunks de 128 KiB -- dentro de uma janela guest
escrita a martelo no codigo:

    uint32_t ring    = 0x40080000u;
    uint32_t ring_sz = 0x00100000u;   /* 1 MiB */

O ring VERDADEIRO do guest, lido do proprio objecto de stream pelo
f2b_stream_fill (host_gow2_f2b.c), e' outro:

    [FIOSOPEN] F2B-STREAM-FILL stream=0x4007FCD0 base=0x40083D40 avail=262144

    ring real  : [0x40083D40, 0x400C3D40)   256 KiB
    janela hard: [0x40080000, 0x40180000)     1 MiB

O pump escreve 770 KiB para la' do fim do ring real, em cheio no heap do
guest, onde a fabrica de tipos acabou de construir os objectos do walk do
typemap:

    tab[0x14] -> 0x400C3D48   (WadServer e os outros 20 *Server)
    tab[0x58] -> 0x400C3D88   (WAD_R_LglScA / WAD_R_Perm, w0=0x80000016)

Apanhado com o watch de escritas host (ps3_watch_store_bulk, ps3recomp
commit 92c6705) -- os watches normais nao o viam porque um movie_io_pread
escreve directamente em vm_base sem passar por vm_write*:

    [WATCHSTORE] BULK who=movie_io_pread fread [0x400C0000,+131072)
                 cobre alvo 0x400C3D88 -> agora 0x9102FFFF

E' exactamente o valor que o walk depois le' como vtable:

    [WADLD-VT28] #28 obj=0x400C3D88 vt=0x005170B8   <- 1o WAD, valida
    [WADLD-VT28] #49 obj=0x400C3D88 vt=0x9102FFFF   <- 2o WAD, destruida
    [ICALL-BAD]  ctr=0x40637408 lr=0x002B0EBC r3=0x400C3D88

Porque o gate PS3_FIOS_STREAM_PUMP=0 nao serve de fix
-----------------------------------------------------
Ele desliga o BLOCO INTEIRO, nao so' o laco: leva atras o rebind de
limit/cursor no container E o arranque do f2b_stream_fill
(g_f2b_fill_fo/mfd/sz). Medido em 3 corridas: a corrupcao desaparece
(vt=0x005170B8 nas duas resolucoes, zero ICALL-BAD) mas F2B-STREAM-FILL e
F2B-STREAM-ENSURE passam a 0, thr_auto_load nunca arranca e o processo
segfalha. Serviu como experiencia discriminadora -- nao como correccao.

O que este script muda
----------------------
1. `ring`/`ring_sz` deixam de ser constantes e passam a ser LIDOS do objecto
   de stream do guest (st = *(type_sys+0x1A8), base = *(st+0), cap = *(st+0xC)),
   que e' a mesma fonte que o f2b_stream_fill ja usa.
2. Sem ring valido, o pump NAO corre -- nunca mais se inventa uma janela.
3. O laco pumpa no maximo UM ring (`pos < pump_end`, pump_end = min(_sz, cap)),
   em vez de dar 20 voltas por cima de si proprio. As voltas anteriores eram
   deitadas fora de qualquer maneira: so' o ultimo MiB sobrevivia, e o
   f2b_stream_fill que corre logo a seguir reescreve o inicio do ring a partir
   de g_f2b_fill_file_pos=0.

Nao mexe no rebind de limit/cursor nem no arranque do fill -- so' no destino
e no alcance das escritas.

Idempotente: a sentinela e' o proprio marcador PUMP-RING-BOUNDS.

ORDEM (importante): tem de correr DEPOIS do patch_fios_f2b_open_block_install.py,
que e' quem instala o bloco F2B-STREAM-PUMP no lift. O apply_all_patches.sh
descobre os patches por glob alfabetico -- dai o nome comecar por
"patch_fios_f2b_p", que ordena a seguir a "patch_fios_f2b_open". Se um dia isto
correr antes, a agulha nao casa e o script devolve rc=2 (MISSING), nao um SKIP
silencioso.

Uso:  patch_fios_f2b_pump_ring_bounds.py [DIR_DE_LIFT]   (default: ../recomp_macos_v2)
rc: 0 aplicado ou ja aplicado; 2 se nao encontrou a agulha em chunk nenhum.
"""
import os
import sys
import glob

MARKER = "PUMP-RING-BOUNDS"

NEEDLE = (
    "            uint32_t ring=0x40080000u;\n"
    "            uint32_t ring_sz=0x00100000u; /* 1 MiB ring */\n"
    "            extern unsigned char* vm_base;\n"
    "            extern uint32_t ppu_vm_size;\n"
    "            if(vm_base && (uint64_t)ring+ring_sz <= (ppu_vm_size?ppu_vm_size:0x50000000u)){\n"
    "              uint32_t pos=0, chunk=0x20000u; /* 128 KiB */\n"
    "              unsigned total=0; int nchunk=0;\n"
    "              while(pos < _sz && nchunk < 400){\n"
)

REPL = (
    "            /* " + MARKER + ": o ring vem do objecto de stream do GUEST\n"
    "             * (st=*(type_sys+0x1A8): base=*(st+0), cap=*(st+0xC)) -- a mesma\n"
    "             * fonte do f2b_stream_fill. As constantes 0x40080000/1MiB que aqui\n"
    "             * estavam escreviam 770 KiB para la' do fim do ring real e\n"
    "             * destruiam os objectos de tipo em 0x400C3D48/0x400C3D88 (medido\n"
    "             * 2026-08-01: vt 0x005170B8 -> 0x9102FFFF entre os dois WADs).\n"
    "             * Sem ring valido o pump nao corre: nunca inventar uma janela. */\n"
    "            uint32_t ring=0u, ring_sz=0u;\n"
    "            extern unsigned char* vm_base;\n"
    "            extern uint32_t ppu_vm_size;\n"
    "            if(_c >= 0x64u){\n"
    "              uint32_t _ts_r = _c - 0x64u;\n"
    "              uint32_t _st_r = vm_read32(_ts_r + 0x1A8u);\n"
    "              if(_st_r >= 0x10000u && _st_r < 0x4F000000u){\n"
    "                ring    = vm_read32(_st_r + 0x0u);\n"
    "                ring_sz = vm_read32(_st_r + 0xCu);\n"
    "              }\n"
    "            }\n"
    "            if(ring < 0x10000u || ring >= 0x4F000000u\n"
    "               || ring_sz == 0u || ring_sz > 0x01000000u){\n"
    "              { static int _nb=0; if(_nb++<8){\n"
    "                fprintf(stderr,\"[FIOSOPEN] F2B-STREAM-PUMP skip: ring guest invalido\"\n"
    "                  \" (base=0x%08X cap=%u) -- sem janela hard-coded\\n\", ring, ring_sz);\n"
    "                fflush(stderr); } }\n"
    "              ring = 0u; ring_sz = 0u;\n"
    "            }\n"
    "            if(vm_base && ring && ring_sz\n"
    "               && (uint64_t)ring+ring_sz <= (ppu_vm_size?ppu_vm_size:0x50000000u)){\n"
    "              uint32_t pos=0, chunk=0x20000u; /* 128 KiB */\n"
    "              unsigned total=0; int nchunk=0;\n"
    "              /* " + MARKER + ": um ring, nao 20 voltas por cima de si proprio. */\n"
    "              uint32_t pump_end = (_sz < ring_sz) ? _sz : ring_sz;\n"
    "              while(pos < pump_end && nchunk < 400){\n"
)

# O laco tinha `n=_sz-pos` (o que sobra do FICHEIRO); com pump_end passa a ser
# o que sobra do RING. Sem isto o ultimo chunk voltava a passar do fim.
NEEDLE_N = "                uint32_t n=_sz-pos; if(n>chunk) n=chunk;\n"
REPL_N = (
    "                uint32_t n=pump_end-pos; if(n>chunk) n=chunk;  /* " + MARKER + " */\n"
)


def patch_text(t):
    """Devolve (texto, estado) com estado em {'APPLIED','ALREADY','MISSING'}."""
    if MARKER in t:
        return t, "ALREADY"
    if NEEDLE not in t:
        return t, "MISSING"
    if t.count(NEEDLE) != 1:
        return t, "MISSING"
    t = t.replace(NEEDLE, REPL, 1)
    # A segunda agulha vive dentro do laco que acabamos de reescrever.
    if t.count(NEEDLE_N) != 1:
        return t, "MISSING"
    t = t.replace(NEEDLE_N, REPL_N, 1)
    return t, "APPLIED"


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
        print("MISSING  agulha do F2B-STREAM-PUMP nao encontrada em %d chunk(s)"
              % len(chunks), file=sys.stderr)
        return 2
    print("patch_fios_f2b_pump_ring_bounds: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
