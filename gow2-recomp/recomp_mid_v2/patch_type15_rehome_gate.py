#!/usr/bin/env python3
"""DESLIGA o paliativo TYPE15 REHOME (`ps3_type15_note_resolve`), que passou de
proteccao a causa de defeito.

Porque precisou de interruptor
------------------------------
`ps3_type15_note_resolve` era chamada **sem gate nenhum** no lift -- corria
sempre, em todas as corridas, desde Julho. Era a unica peca deste tipo no
projecto a nao respeitar a regra 6 do CLAUDE.md (experiencias gated, OFF por
default). Sem interruptor nao havia maneira de medir o que acontecia sem ela.

Default: **DESLIGADO**. `PS3_TYPE15_REHOME=1` volta a ligar o paliativo, so'
para reproduzir a comparacao abaixo.

O que o paliativo faz, e porque ha' suspeita fundada contra ele
---------------------------------------------------------------
Ao primeiro `resolve` do tipo 0x54 copia 0x200 bytes da fabrica para uma zona
"pinada" (`0x47D00000`), rehoma a free-list para pin+0x400, rehoma o "shell"
para pin+0x800, traduz os ponteiros auto-referentes e aponta `tab[0x54]` para a
copia. Nove camadas, construidas ao longo de varias sessoes, **todas para
sobreviver ao mesmo evento**: o "stream stomp". O comentario no lift nomeia-o:

    *obj becomes ASCII 0x5F436F75 ("_Cou" from R_Perm "_Count…")

Esse stomp era o `F2B-STREAM-PUMP` a escrever 20 MB numa janela FIXA de 1 MiB
(`0x40080000`), 770 KiB para la' do ring real de 256 KiB, por cima da arena
`0x400Cxxxx`/`0x401xxxxx` onde a fabrica vive. **Corrigido hoje** por
`patch_fios_f2b_pump_ring_bounds.py` (base/cap lidos do objecto de stream do
guest), medido 3/3.

E o paliativo tem um custo proprio, medido (2026-08-01, `PS3_TRACE_E6B4=1`):

    [TYPE15] REHOME old=0x401002F0 pin=0x47D00000 ... +44=0 +48=0 +C8b=-1 +D4=0
    [E6B4] VAZIA self=0x47D00000 vt=0x00516D70 cnt=0 ... idx=2 ent=0x00000000  (x13)

O `SNAP` acontece com `vt=0x00516D70` -- **vtable viva, objecto sao**. A copia e'
tirada quando a fabrica ainda esta VAZIA (`+C8 = -1` e' o cursor "sem produto").
A partir dai `tab[0x54]` aponta para o congelado, e tudo o que o jogo popula vai
para o `0x401002F0` original e nunca mais e' lido. Dai `cnt=0`, dai o slot [2]
vazio, dai os 13 `ps3_call_opd(opd=0)` de `func_0039E6B4`.

Medicao que fechou o caso (2026-08-01, MESMO binario, so' muda a env var)
-------------------------------------------------------------------------
                        REHOME=1        REHOME=0
    tabela vazia            13               0
    OPD-BAD                 14               1
    ICALL-BAD               12              12
    FACTORY REPAIR           0               0
    R_PermA lido      20298800        20298800
    st620 max                3               3
    thr_end                  0               0

E a prova de que o stomp desapareceu mesmo (`PS3_TRACE_E6B4=all` +
`PS3_WATCH_STORE=0x401002F0`, com REHOME=0):

  - **3184 despachos, ZERO vazios.** Oito fabricas distintas, todas com vtable
    viva (0x0051xxxx), todas a resolver produtos reais (0x4063xxxx/0x4066xxxx).
  - As escritas em `0x401002F0` (word0 da fabrica natural) sao a cadeia normal
    de construtores C++ do guest -- `0x511628 -> 0x5116E8 -> 0x511680 ->
    0x516D70` -- a terminar exactamente na vtable que o SNAP capturava.
    **Nenhum ASCII. Nenhum stomp.**
  - `FACTORY REPAIR = 0`: o reparador defensivo nunca precisa de disparar.

Conclusao: com o pump limitado, o paliativo deixou de proteger seja o que for e
passou a ser **a causa unica** dos 13 despachos com OPD nulo. Removido por
default, com a prova de nao-regressao que a regra do CLAUDE.md exige.

O que este fix NAO faz: `thr_end` continua a 0 nos dois bracos. A parede do
AUTO_LOAD nao e' esta, e nao foi aberta aqui.

Gate: default DESLIGADO. `PS3_TYPE15_REHOME=1` religa (imprime uma linha, nunca
silencioso), so' para reproduzir a comparacao.

Uso:  patch_type15_rehome_gate.py [DIR_DE_LIFT]
rc: 0 aplicado/ja aplicado; 2 se a agulha nao casou.
"""
import os
import sys
import glob

MARKER = "TYPE15-REHOME-GATE"

NEEDLE = (
    'extern "C" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt) {\n'
    "    if (idx != 0x54u || !obj) return;\n"
)

REPL = (
    'extern "C" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt) {\n'
    "    /* " + MARKER + ": interruptor do paliativo. Default = ligado (nao muda\n"
    "     * nada); PS3_TYPE15_REHOME=0 desliga, para medir o caminho natural\n"
    "     * agora que o stream stomp que ele contornava esta corrigido. */\n"
    "    { static int _g = -1;\n"
    "      if (_g < 0) { extern char* getenv(const char*);\n"
    "        const char* _e = getenv(\"PS3_TYPE15_REHOME\");\n"
    "        _g = (_e && *_e && *_e != '0') ? 1 : 0;\n"
    "        if (_g) { fprintf(stderr,\n"
    "          \"[TYPE15] REHOME RELIGADO por PS3_TYPE15_REHOME=1 -- paliativo \"\n"
    "          \"obsoleto, so' para comparacao\\n\"); fflush(stderr); } }\n"
    "      if (!_g) return; }\n"
    "    if (idx != 0x54u || !obj) return;\n"
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
        print("MISSING  agulha de ps3_type15_note_resolve nao encontrada", file=sys.stderr)
        return 2
    print("patch_type15_rehome_gate: applied=%d already=%d" % (applied, already))
    return 0


if __name__ == "__main__":
    sys.exit(main())
