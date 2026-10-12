#!/usr/bin/env python3
"""Probe gated do OPEN de audio da intro (Task 1 do plano
../ps3recomp/docs/superpowers/plans/2026-07-20-macos-intro-audio-open-wall.md).

PERGUNTA
--------
A FSM do player de intro para em `st620 = 1` e o byte de EOS (obj+0x744) nunca
sobe. O estado 1 espera pelo open dos media. Com PS3_TRACE_FS=1 o guest abre UM
unico ficheiro em todo o boot -- `gow2.psarc`. Zero tentativas de `_movies`,
`.wav` ou `.vpk` chegam ao `cellFsOpen`, e o `movie_io_resolve()` (que ja sabe
resolver `.wav` sem distincao de maiusculas) nunca e' consultado. Logo o open de
audio NAO passa pelo cellFs. Falta NOMEAR por onde passa. E' isso que esta probe
mede -- nao corrige nada.

CADEIA ESTATICA (lida em recomp_macos_v2/ppu_recomp_001.cpp)
------------------------------------------------------------
    func_002C00DC  (Play -- unico escritor de st620 0->1)
      r29 = r1+0x170                      <- params de snd_stream na stack
      func_0045C548(r3=r29)               <- init dos params
      [r1+0x23C] = r26                    <- params+0xCC = PONTEIRO DO CAMINHO
      func_0045E230(r3=r29)               <- snd_stream OPEN (API publica)
      [obj+0x720] = r3                    <- guarda o resultado
      func_002B4340(r3=obj+0x62C, ...)    <- open FIOS do video (outra parede)

    func_0045E230(params)
      if (params+0xCC != 0 && params+0xD4 == 0) -> func_0045E2A8
      func_0045E2A8:
        procura voice livre na tabela [ [TOC+0x5BC], stride 0x35A8 ]
        func_0045D8F0(voice, params)      <- copia params para a voice
        func_004654D0(voice+0xF0, 10000000)
              -> thunk func_004B99D8 -> slot 0x00519488
              -> sysPrxForUser NID 0x1573DC3F  == sys_lwmutex_lock (10 s)
        [voice+0x134] = 1                 <- pedido armado
        func_004B9D38(voice+0xE8)         <- thunk (slot 0x005194F4): acorda o servico
        func_004654A8(voice+0xF0)         <- thunk func_004B99F8: unlock
        return [voice+0]

    ==> func_0045E230 NAO abre ficheiro nenhum. Ele arma um pedido e sinaliza.
        O boot cria 4x sys_ppu_thread "snd_stream_service_thread" (entry
        0x00533A38): sao ESSAS que abrem. O open real e', portanto, ASSINCRONO
        e noutra thread do guest.

    O open a serio (unico leitor de TOC+0x74C, o formato
    "snd_stream: couldn't open file %s\n" em VA 0x004D1F90):

    func_00461658(stream)
      sub = stream + [stream+0x1E8]*0x308 + 0x220
      if (([sub+0x110] & 2) == 0) -> func_004618A0   (ramo A)
      ramo B (inline):  drv = [TOC+0x748]            (objecto fixo 0x009B97E0)
      ramo A (004618A0): drv = [ [TOC+0x750] ]       (ponteiro em 0x00994A20, .bss)
      ambos:  opd = [drv+8];  ctr = [opd+0];  r2 = [opd+4];  ps3_indirect_call
              r3 = self/ctx, r4 = sub+0x104 (slot de saida do handle),
              r5 = 0, r6 = (int8)modo, r7 = [sub+0x304]
      if (rc < 0) -> func_004618EC -> func_0045CF50(2, "snd_stream: couldn't
                                       open file %s\n", ...)

    ==> o open e' uma chamada INDIRECTA do guest atraves de uma tabela de
        driver. O que esta probe imprime -- opd/code/toc -- e' exactamente a
        identidade do ponto de entrada onde o open aterra. Se `code` for uma EA
        de guest normal, e' codigo do proprio jogo; se for >= 0xF0000000, e' um
        TAG de import do HLE (ver ppu_imports.cpp: OPD.code = HLE_TAG_BASE+4*i)
        e portanto uma funcao do HOST, identificavel pelo NID.

ARMADILHA MEDIDA (nao inferir do log)
-------------------------------------
`func_0045CF50(level, fmt, ...)` tem porta de verbosidade:
`if ([[TOC+0x604]] < level) return;`. Na imagem [0x005765D0] = 3 e a mensagem de
open e' nivel 2, portanto HOJE imprimiria. Mesmo assim: a AUSENCIA da linha
"couldn't open file" NAO prova que o open teve sucesso -- pode significar que
func_00461658 nunca e' alcancada. Por isso a probe instrumenta a ENTRADA do open
e nao se fia na mensagem.

RESULTADO DA MEDICAO (2026-07-20, macOS/arm64, M=8 x 25 s, PS3_NO_RSX=1)
-----------------------------------------------------------------------
Com PS3_TRACE_SNDOPEN=1, em 8 de 8 corridas:

    [SNDOPEN] 0045E230 enter #1 params=0x0FEFF9F0 r4=0x00000000 r5=0x0FEFFAF8
              p_CC=0x0FEFF8F0 path='/_movies/SmLogo_v2.wav' p_D4=0x00000000
    [SNDOPEN] 00461658 OPEN enter #1 stream=0x4309EF00 idx=0 sub=0x4309F120
              f110=0x00000000 ramo=A-004618A0(TOC+0x750)
    [SNDOPEN]   00461658 drvA[TOC+0x748] drv=0x009B97E0 opd=0x00533940
              code=0x0045FB50 toc=0x00541178  class=GUEST-code-EA
    [SNDOPEN]   00461658 drvB[[TOC+0x750]] drv=0x009B95BC opd=0x0052F650
              code=0x002B47D4 toc=0x00541178  class=GUEST-code-EA
    [SNDOPEN]   00461658 sub inline '/_movies/SmLogo_v2.wav'
    [SNDOPEN] 004618A0 ramoA #1 sub=0x4309F120 outslot=0x4309F224
              r6=0xFFFFFFFE r7=0x00000000
    [SNDOPEN]   004618A0 open() drv=0x009B95BC opd=0x0052F650
              code=0x002B47D4 toc=0x00541178  class=GUEST-code-EA
    [SNDOPEN] 004618EC FAIL #1 rc=-2 sub=0x430A26C8 drv_usado=0x009B95BC

RESPOSTA A PERGUNTA "em que ponto de entrada do HOST aterra o open?":
**EM NENHUM.** `class=GUEST-code-EA` -- a OPD que o bctr usa aponta para
codigo do proprio guest, `func_002B47D4`, e nao para um TAG de import do HLE.
E `func_002B47D4` faz:

    memset(buf,0,0x104) ; strcpy(buf, path) ; func_002B3890(buf)
      -> func_002B3890 e' um tolower() (tabela ctype em [TOC-0x1458]); logo o
         guest JA normaliza '/_movies/SmLogo_v2.wav' para minusculas, que e'
         exactamente como o membro existe no psarc. O open NAO falha por caixa.
    func_0030D578(r3=[[TOC-0x1460]+0x118], r4=0, r5=buf, r6=1, r7=out)
      -> o MESMO choke point FIOS que o caminho de VIDEO usa via func_002B4340.
    func_003067DC(rc,0) ; se != 0 -> falha

Confirmacao independente no mesmo log: `[fs] open` aparece 1 vez por corrida e
sempre so' para '/dev_hdd0/game/NPUA80491/USRDIR/gow2.psarc'; zero `[movieio]`
ou `[fs]` para SmLogo/.wav/.m2v/.vpk em 8/8. Nao ha cellFsOpen nem movie_io no
caminho do audio -- e nao ha nada de HOST para instrumentar mais abaixo.

CONSEQUENCIA PARA O PLANO: audio e video partilham o open de membro em
func_0030D578. O choke point unico da Task 4 (opcao 1) fica PROVADO, nao
suposto -- corrigir la serve os dois caminhos de uma vez.

O QUE FAZ
---------
So' LE memoria do guest (vm_read8/vm_read32 tem guarda de out-of-bounds e
devolvem 0); nunca escreve. Com PS3_TRACE_SNDOPEN desligada e' um no-op exacto
sobre o baseline (regra 6 do CLAUDE.md do motor).

Em vez de assumir o layout do objecto para achar o nome do ficheiro, a probe
VARRE a regiao do objecto e imprime toda a string ASCII imprimivel apontada por
um u32 la dentro. Assim o caminho aparece sem depender de eu ter acertado no
offset -- que e' precisamente o que nao se sabe.

Marcadores de outros patches (nao colidir): [AREAD] (patch_2b3d1c_probe.py),
[OOBARG] (patch_oob_ra_probe.py), [INTROSEQ] (patch_introseq_probe.py),
[KICK]/[SPUBRP]/[SPUHALT], [MOVIEFSM]/[MOVIEOBJ] (movie_eos_arm.c). Esta usa
[SNDOPEN] e um patch concorrente trata do FIOS (func_002B4340/func_0030D578) --
funcoes disjuntas das desta.

Uso:  patch_snd_open_probe.py [DIR_DE_LIFT]   (default: ../recomp_macos_v2)
      PS3_TRACE_SNDOPEN=1 ./boot_gow2 EBOOT.ELF 2>&1 | grep SNDOPEN

Reaplicado por ../apply_all_patches.sh apos cada re-lift. Idempotente.

Nota de implementacao (armadilha ja paga por outros agentes): a substituicao e'
`str.replace()` textual sobre a regiao da funcao, NAO `re.sub` com string de
substituicao -- essa interpreta escapes e mete uma quebra de linha REAL dentro
de um literal C, gerando fonte que nao compila.
"""
from __future__ import annotations

import sys
from pathlib import Path

MARKER = "SND-OPEN-PROBE"
ENV = "PS3_TRACE_SNDOPEN"

# --------------------------------------------------------------------------
# Helpers de ficheiro. Inseridos UMA vez, logo a seguir aos includes do chunk,
# para ficarem visiveis a todas as funcoes instrumentadas independentemente da
# ordem em que o lifter as emitir no proximo re-lift.
# --------------------------------------------------------------------------
HELPERS = r'''
/* ===================== SND-OPEN-PROBE (Task 1) ==========================
 * Probe gated por PS3_TRACE_SNDOPEN. Estritamente read-only sobre a memoria
 * do guest. Ver recomp_mid_v2/patch_snd_open_probe.py para a cadeia estatica.
 * ====================================================================== */
static int snd_probe_on(void) {
    static int on = -1;
    if (on < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_SNDOPEN");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

/* EA plausivel de dados do guest: imagem (0x10000..) ate' ao topo das arenas
 * altas observadas em boot (handles FIOS aparecem em 0x43xxxxxx). */
static int snd_probe_ea_ok(uint32_t ea) {
    return ea >= 0x00010000u && ea < 0x4F000000u;
}

/* Le uma C-string ASCII imprimivel; devolve o comprimento (0 se nao parecer
 * texto). Nunca escreve; vm_read8 ja tem guarda de OOB. */
static int snd_probe_cstr(uint32_t ea, char* out, int cap) {
    int i;
    out[0] = 0;
    if (!snd_probe_ea_ok(ea)) return 0;
    for (i = 0; i < cap - 1; i++) {
        unsigned c = (unsigned)vm_read8((uint64_t)(ea + (uint32_t)i)) & 0xFFu;
        if (c == 0) break;
        if (c < 0x20u || c > 0x7Eu) return 0;
        out[i] = (char)c;
    }
    out[i] = 0;
    return i;
}

/* Varre [base, base+len) como u32 e imprime toda a string apontada. E' assim
 * que o caminho do ficheiro aparece sem eu ter de adivinhar o offset. */
static void snd_probe_scan(const char* tag, uint32_t base, uint32_t len) {
    uint32_t off;
    char buf[192];
    if (!snd_probe_ea_ok(base)) return;
    if (snd_probe_cstr(base, buf, (int)sizeof buf) >= 4)
        fprintf(stderr, "[SNDOPEN]   %s inline '%s'\n", tag, buf);
    for (off = 0; off + 4u <= len; off += 4u) {
        uint32_t v = vm_read32((uint64_t)(base + off));
        if (snd_probe_cstr(v, buf, (int)sizeof buf) >= 5)
            fprintf(stderr, "[SNDOPEN]   %s +0x%03X -> 0x%08X '%s'\n", tag, off, v, buf);
    }
}

/* Identifica o ponto de entrada de uma chamada indirecta do guest: o objecto
 * de driver, o OPD em [drv+8] e o par (code, toc) que o bctr vai usar.
 * code >= 0xF0000000 == TAG de import do HLE (ppu_imports.cpp) => funcao do
 * HOST; caso contrario e' uma EA de codigo do proprio guest. */
static void snd_probe_opd(const char* tag, uint32_t drv) {
    uint32_t opd = 0, code = 0, toc = 0;
    if (snd_probe_ea_ok(drv)) {
        opd = vm_read32((uint64_t)(drv + 8u));
        if (snd_probe_ea_ok(opd) || opd >= 0xF0000000u) {
            code = vm_read32((uint64_t)(opd + 0u));
            toc  = vm_read32((uint64_t)(opd + 4u));
        }
    }
    fprintf(stderr, "[SNDOPEN]   %s drv=0x%08X opd=[drv+8]=0x%08X code=0x%08X toc=0x%08X  class=%s\n",
            tag, drv, opd, code, toc,
            drv == 0 ? "DRIVER-NULL (nada instalado)"
                     : (code >= 0xF0000000u ? "HOST/HLE-import-tag"
                                            : (code ? "GUEST-code-EA" : "OPD-VAZIA")));
}
'''

# --------------------------------------------------------------------------
# 1) func_0045E230 -- API publica de open do snd_stream chamada pelo Play.
#    r3 = struct de params; params+0xCC = ponteiro do caminho.
# --------------------------------------------------------------------------
P_0045E230 = (
    "        /* " + MARKER + ": entrada da API de open do snd_stream (chamada pelo Play).\n"
    "         * params+0xCC = ponteiro do caminho; params+0xD4 = fonte em memoria. */\n"
    "        if (snd_probe_on()) { static int _n = 0; if (_n++ < 8) {\n"
    "            uint32_t _p  = (uint32_t)ctx->gpr[3];\n"
    "            uint32_t _cc = snd_probe_ea_ok(_p) ? vm_read32((uint64_t)(_p + 0xCCu)) : 0u;\n"
    "            uint32_t _d4 = snd_probe_ea_ok(_p) ? vm_read32((uint64_t)(_p + 0xD4u)) : 0u;\n"
    "            char _s[192]; snd_probe_cstr(_cc, _s, (int)sizeof _s);\n"
    '            fprintf(stderr, "[SNDOPEN] 0045E230 enter #%d params=0x%08X r4=0x%08X r5=0x%08X'
    ' p_CC=0x%08X path=\'%s\' p_D4=0x%08X\\n",\n'
    "                _n, _p, (uint32_t)ctx->gpr[4], (uint32_t)ctx->gpr[5], _cc, _s, _d4);\n"
    '            snd_probe_scan("0045E230 params", _p, 0x100u);\n'
    "            fflush(stderr);\n"
    "        } }\n"
)

# --------------------------------------------------------------------------
# 2) func_00461658 -- o open A SERIO (unico dono da mensagem de falha).
#    r3 = stream; sub = r3 + [r3+0x1E8]*0x308 + 0x220.
# --------------------------------------------------------------------------
P_00461658 = (
    "        /* " + MARKER + ": entrada do open REAL do snd_stream.\n"
    "         * sub = stream + [stream+0x1E8]*0x308 + 0x220; o ramo depende de\n"
    "         * [sub+0x110]&2: 0 -> func_004618A0 (driver [[TOC+0x750]]),\n"
    "         * !=0 -> ramo inline (driver [TOC+0x748]). */\n"
    "        if (snd_probe_on()) { static int _n = 0; if (_n++ < 8) {\n"
    "            uint32_t _st  = (uint32_t)ctx->gpr[3];\n"
    "            uint32_t _toc = (uint32_t)ctx->gpr[2];\n"
    "            uint32_t _idx = snd_probe_ea_ok(_st) ? vm_read32((uint64_t)(_st + 0x1E8u)) : 0u;\n"
    "            uint32_t _sub = _st + _idx * 0x308u + 0x220u;\n"
    "            uint32_t _f110 = snd_probe_ea_ok(_sub) ? vm_read32((uint64_t)(_sub + 0x110u)) : 0u;\n"
    "            uint32_t _dA  = vm_read32((uint64_t)(_toc + 0x748u));\n"
    "            uint32_t _pB  = vm_read32((uint64_t)(_toc + 0x750u));\n"
    "            uint32_t _dB  = snd_probe_ea_ok(_pB) ? vm_read32((uint64_t)_pB) : 0u;\n"
    '            fprintf(stderr, "[SNDOPEN] 00461658 OPEN enter #%d stream=0x%08X idx=%u sub=0x%08X'
    ' f110=0x%08X ramo=%s\\n",\n'
    '                _n, _st, _idx, _sub, _f110, (_f110 & 2u) ? "B-inline(TOC+0x748)" : "A-004618A0(TOC+0x750)");\n'
    '            snd_probe_opd("00461658 drvA[TOC+0x748]", _dA);\n'
    '            fprintf(stderr, "[SNDOPEN]   00461658 drvB ptrslot=0x%08X obj=0x%08X\\n", _pB, _dB);\n'
    '            snd_probe_opd("00461658 drvB[[TOC+0x750]]", _dB);\n'
    '            snd_probe_scan("00461658 sub", _sub, 0x310u);\n'
    '            snd_probe_scan("00461658 stream", _st, 0x220u);\n'
    "            fflush(stderr);\n"
    "        } }\n"
)

# --------------------------------------------------------------------------
# 3) func_004618A0 -- ramo A, imediatamente antes do bctr do open.
# --------------------------------------------------------------------------
P_004618A0 = (
    "        /* " + MARKER + ": ramo A do open, mesmo antes da chamada indirecta.\n"
    "         * r31 = sub-objecto do stream; driver = [[TOC+0x750]]. */\n"
    "        if (snd_probe_on()) { static int _n = 0; if (_n++ < 8) {\n"
    "            uint32_t _toc = (uint32_t)ctx->gpr[2];\n"
    "            uint32_t _sub = (uint32_t)ctx->gpr[31];\n"
    "            uint32_t _pB  = vm_read32((uint64_t)(_toc + 0x750u));\n"
    "            uint32_t _dB  = snd_probe_ea_ok(_pB) ? vm_read32((uint64_t)_pB) : 0u;\n"
    '            fprintf(stderr, "[SNDOPEN] 004618A0 ramoA #%d sub=0x%08X outslot=0x%08X'
    ' r6=0x%08X r7=0x%08X\\n",\n'
    "                _n, _sub, _sub + 0x104u, (uint32_t)ctx->gpr[6], (uint32_t)ctx->gpr[7]);\n"
    '            snd_probe_opd("004618A0 open()", _dB);\n'
    "            fflush(stderr);\n"
    "        } }\n"
)

# --------------------------------------------------------------------------
# 4) func_004618EC -- bloco de FALHA (imprime "snd_stream: couldn't open file").
# --------------------------------------------------------------------------
P_004618EC = (
    "        /* " + MARKER + ": bloco de FALHA do open -- daqui sai a mensagem\n"
    "         * \"snd_stream: couldn't open file %s\" (fmt em TOC+0x74C). */\n"
    "        if (snd_probe_on()) { static int _n = 0; if (_n++ < 16) {\n"
    "            uint32_t _sub = (uint32_t)ctx->gpr[31];\n"
    '            fprintf(stderr, "[SNDOPEN] 004618EC FAIL #%d rc=%d sub=0x%08X drv_usado=0x%08X\\n",\n'
    "                _n, (int)(int32_t)ctx->gpr[3], _sub,\n"
    "                snd_probe_ea_ok(_sub) ? vm_read32((uint64_t)(_sub + 0x100u)) : 0u);\n"
    '            snd_probe_scan("004618EC sub", _sub, 0x310u);\n'
    "            fflush(stderr);\n"
    "        } }\n"
)

PROBES = [
    ("func_0045E230", P_0045E230, "API de open do snd_stream (chamada pelo Play)"),
    ("func_00461658", P_00461658, "open REAL do snd_stream"),
    ("func_004618A0", P_004618A0, "ramo A do open (chamada indirecta)"),
    ("func_004618EC", P_004618EC, "bloco de falha do open"),
]

# Ancora para os helpers. `<stdlib.h>` e' o sitio ideal (o `extern char*
# getenv(...)` de ambito de bloco so' herda linkagem C se getenv ja tiver sido
# declarado em ambito de namespace), MAS num lift FRESCO esse include ainda nao
# existe: quem o poe e' patch_stdlib_getenv.py, que o apply_all_patches.sh corre
# DEPOIS deste ('snd' < 'stdlib' na ordem do glob). Por isso ha fallback para o
# `<math.h>` do preambulo do lifter: patch_stdlib_getenv.py insere o stdlib.h
# logo a seguir a essa mesma ancora, portanto acaba ANTES destes helpers de
# qualquer maneira -- que e' a ordem que o linker precisa.
HELPER_NEEDLES = ("#include <stdlib.h>\n", "#include <math.h>\n")


def sig(name: str) -> str:
    return "void " + name + "(ppu_context* ctx) {\n"


def install_helpers(root: Path) -> list[str]:
    """Poe os helpers no topo de cada chunk que aloja uma das funcoes-alvo."""
    out = []
    wanted = {name for name, _, _ in PROBES}
    for path in sorted(root.glob("ppu_recomp_*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        if not any(sig(n) in src for n in wanted):
            continue
        if MARKER + " (Task 1)" in src:
            out.append(f"{path.name}: helpers ja presentes")
            continue
        needle = next((n for n in HELPER_NEEDLES if n in src[:4000]), None)
        if needle is None:
            raise SystemExit(
                f"{path.name}: nenhuma das ancoras {HELPER_NEEDLES!r} existe no "
                "preambulo -- o shape do lift mudou; reveja a probe antes de forcar"
            )
        src = src.replace(needle, needle + HELPERS, 1)
        path.write_text(src, encoding="utf-8", newline="\n")
        out.append(f"{path.name}: helpers instalados")
    if not out:
        raise SystemExit(
            f"nenhum chunk em {root} aloja as funcoes-alvo -- lift errado ou "
            "shape mudou"
        )
    return out


def install(root: Path, name: str, probe: str, what: str) -> str:
    """Insere `probe` logo a seguir a chave de abertura de `name`.

    A regiao considerada vai da assinatura ate' a definicao seguinte: o
    marcador so' e' procurado AI, senao a 2a funcao do mesmo ficheiro seria
    dada como ja instrumentada.
    """
    signature = sig(name)
    for path in sorted(root.glob("ppu_recomp_*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        i = src.find(signature)
        if i < 0:
            continue
        j = src.find("\nvoid func_", i + len(signature))
        region = src[i:j] if j > i else src[i:]
        if MARKER in region:
            return f"{path.name}: {name} ja instrumentada ({what})"
        region2 = region.replace(signature, signature + probe, 1)
        if region2 == region:
            raise SystemExit(f"{path.name}: falhou a insercao em {name}")
        path.write_text(src[:i] + region2 + (src[j:] if j > i else ""),
                        encoding="utf-8", newline="\n")
        return f"{path.name}: {name} instrumentada ({what})"
    raise SystemExit(
        f"{name} ausente no lift em {root} -- a cadeia do snd_stream mudou de "
        "forma; reveja a probe antes de forcar"
    )


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else \
        Path(__file__).resolve().parent.parent / "recomp_macos_v2"
    if not sorted(root.glob("ppu_recomp_*.cpp")):
        print(f"FAIL: nenhum ppu_recomp_*.cpp em {root}", file=sys.stderr)
        return 1

    for line in install_helpers(root):
        print(line)
    for name, probe, what in PROBES:
        print(install(root, name, probe, what))
    print(f"probe {MARKER} pronta -- corra com {ENV}=1")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
