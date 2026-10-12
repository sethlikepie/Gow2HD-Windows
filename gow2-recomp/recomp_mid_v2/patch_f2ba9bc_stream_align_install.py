#!/usr/bin/env python3
r"""F2B-STREAM-ALIGN / F2B-STREAM-RESYNC / F2B-STREAM-DESYNC-STOP: reposicao do
bloco de clamp/realinhamento de header perto do fim do R_PermA em func_002BA9BC.

Porque existe (Fase 10 -- "A parede do DecodeAu", Plano 10-02, Caso B)
------------------------------------------------------------------------
Medido no Plano 10-02: depois do patch AREAD-HLE (10-01), `cellVdecDecodeAu`
passa a ser chamado 10x em 6/6 corridas (a parede do DecodeAu quebrou), mas
`thr_auto_load end` continua 0 em 6/6 -- ha uma parede NOVA a jusante, mais
funda. Os seis logs terminam identicamente: apos o R_PermA ser lido por
inteiro (`bytes_read=20169344/20169344`, confirmado por
`[FIOSOPEN] F2B-STREAM-EOF-DONE ... file_pos=20169344/20169344`), o guest
entra num header com um tamanho absurdo
(`[FREELIST-TAG-GUARD] 262610 entry ... need=0x31304350 -> abort r3=0`,
0x31304350 ~= 826MB, muito acima do tecto de 64MiB) e o boot fica preso a
repetir `[MOVIEFSM] st620 0 -> 0` ate' ao timeout -- nunca chega a criar a
thread `AUTO_LOAD` (confirmado: `sys_ppu_thread_create name="AUTO_LOAD"`
NUNCA aparece nos 6 logs).

O inventario do Plano 10-01 (Task 1) ja tinha mapeado este bloco como
candidato de reserva: `F2B-STREAM-ALIGN`, `F2B-STREAM-RESYNC` e
`F2B-STREAM-DESYNC-STOP` aparecem no diff de marcadores (presentes em
`recomp_macos_v2.pre_v4`, ausentes do lift actual), self-declarados
"orfao e por instalar" na seccao "O que NAO instala" de
`patch_f2b_multimb_install.py`:

    F2B-BODY-CLAMP / F2B-STREAM-RESYNC / F2B-STREAM-ALIGN / DESYNC-STOP em
    func_002BA9BC: ~150 linhas com offsets absolutos do R_PermA, de uma
    sessao anterior.

O sintoma medido (header com tamanho absurdo perto do fim do R_PermA) e'
EXACTAMENTE o que este bloco existe para prevenir: quando o tipo do header
nao parece membro valido OU o corpo excede muito o que resta no stream, o
bloco realinha o cursor para a cadeia solida conhecida offline
(MDL_PUMeterDrain1_0@20038352) ou faz idle limpo em vez de deixar a
alocacao seguinte (func_00262610, protegida por FREELIST-TAG-GUARD da Fase
9) abortar com r3=0 e travar o resto do fluxo.

O que o bloco faz (verbatim, extraido de
recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:31664-31811, func_002BA9BC)
------------------------------------------------------------------------
Depois do calculo do tamanho do body (r29) e do tipo (r6 fixo em 16 bits),
se `g_f2b_fill_mfd`/`g_f2b_fill_sz` estiverem armados (stream F2B activo):
  - calcula `stream_left` (avail no ring + bytes por ler do ficheiro);
  - marca `_absurd` se o tipo > 0x100 OU o corpo > stream_left E > 1MiB;
  - se absurdo E estamos na banda residual (ultimos ~256KB do R_PermA de
    20169344 bytes): realinha UMA vez para a cadeia solida conhecida
    (MDL_PUMeterDrain1_0@20038352, `F2B-STREAM-ALIGN`) e senao faz idle
    limpo (`F2B-STREAM-EOF-QUIET`);
  - se absurdo E NAO residual: tenta ate' 8 `F2B-STREAM-RESYNC` (varre ate'
    256KB a frente por um header plausivel) antes de desistir;
  - se nenhum realinhamento resolveu e nao estamos perto do EOF: regista
    `F2B-STREAM-DESYNC-STOP` (so' diagnostico, nao muda estado) -- perto do
    EOF faz idle limpo;
  - se nao absurdo mas o corpo excede o stream restante: `F2B-BODY-CLAMP`
    (corta o corpo ao que resta, sem abortar).
Fora deste ramo (mfd/sz nao armados), o comportamento e' inalterado --
o bloco e' aditivo, nunca substitui o calculo natural de r29/1D4.

Contrato de rc (mesmo de patch_fios_stream_guards_install.py /
patch_2b3d1c_movie_io.py)
------------------------------------------------------------------------
  marcador F2B-BODY-CLAMP ja presente no corpo de func_002BA9BC -> ALREADY
  func_002BA9BC ausente de todos os chunks                      -> rc=2
  func_002BA9BC presente em >1 chunk (ambiguo)                   -> rc=2
  ancora (CR-check + vm_write32 do 1D4) ausente/duplicada        -> rc=2
                                                                     (recusa,
                                                                     nao
                                                                     adivinha)
  pre-requisito F2B (g_f2b_fill_mfd) ausente do MESMO chunk      -> rc=2
                                                                     (recusa)
Idempotente: 2a corrida = ALREADY.

Uso: python3 recomp_mid_v2/patch_f2ba9bc_stream_align_install.py [LIFT_DIR]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths  # noqa: E402

MARKER = "F2B-BODY-CLAMP"
FUNC_SIG = "void func_002BA9BC(ppu_context* ctx) {"

# Ancora: o CR-check de "type < 0x10" seguido do vm_write32 que grava r29 em
# +0x1D4 -- unico em todo o chunk (confirmado por varredura desta sessao),
# byte-identico entre o lift de referencia e o lift actual.
ANCHOR = (
'        { int64_t a = (int32_t)ctx->gpr[6]; int64_t b = (int64_t)0xF; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n        vm_write32(ctx->gpr[31] + 0x1D4, ctx->gpr[29]);\n'
)

# Bloco a inserir, extraido VERBATIM de
# recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:31664-31811 (func_002BA9BC).
BLOCK = (
    '        /* F2B-BODY-CLAMP + DESYNC-STOP: header size (aligned → r29/1D4) cannot\n         * exceed remaining stream (ring avail + unread FO). After ~010 the ring\n         * can still desync (next expected TXR_chest…); bad headers yield ASCII\n         * sizes / type>0xFF and freelist death (need multi-MB) → CBC20 hang.\n         * If type looks non-member OR body is wildly above stream_left, idle SM\n         * (same class as EOF-DONE) instead of multi-pass garbage. */\n        if (g_f2b_fill_mfd && g_f2b_fill_sz) {\n            uint32_t _bst = vm_read32((uint32_t)ctx->gpr[31] + 0x1A8u);\n            uint32_t _bav = (_bst >= 0x10000u && _bst < 0x4F000000u) ? vm_read32(_bst + 0x10u) : 0u;\n            uint32_t _file_left = (g_f2b_fill_file_pos < g_f2b_fill_sz)\n                ? (g_f2b_fill_sz - g_f2b_fill_file_pos) : 0u;\n            uint32_t _stream_left = _bav + _file_left;\n            uint32_t _body = (uint32_t)ctx->gpr[29];\n            uint32_t _ty = (uint32_t)ctx->gpr[6] & 0xFFFFu;\n            uint32_t _raw = (uint32_t)ctx->gpr[10];\n            int _absurd = 0;\n            if (_ty > 0x100u) _absurd = 1; /* normal WAD types are small (1/2/3/7/12/0x15…) */\n            if (_body > _stream_left && _body > 0x100000u) _absurd = 1;\n            if (_absurd) {\n                /* Residual band (last 256KB of R_Perm 20169344): offline FO has\n                 * solid chain MAT_EnergyConstant@20038320 → MDL_PUMeterDrain1_0@\n                 * 20038352 → … → DC_WAD…@20124912. In-boot lands mid-payload\n                 * (~20031552). Do NOT F2B-STREAM-RESYNC here — hard-align cursor\n                 * to next offline-known type1 header once, then FULL idle.\n                 * Mid-FO still RESYNC (cap 8). */\n                uint32_t _logical = (g_f2b_fill_file_pos > _bav)\n                    ? (g_f2b_fill_file_pos - _bav) : 0u;\n                int _found = 0;\n                int _residual = (g_f2b_fill_sz == 20169344u\n                    && (_logical + 0x40000u >= g_f2b_fill_sz\n                        || g_f2b_fill_file_pos + 0x10000u >= g_f2b_fill_sz));\n                static int _resync_n = 0;\n                static int _residual_aligned = 0;\n                if (_residual) {\n                    /* R_PermA offline: solid type1 chain begins at\n                     * MDL_PUMeterDrain1_0@20038352 (after MAT_EnergyConstant).\n                     * One rewind/align there lets guest walk hdr=0x20+align16\n                     * to FO end — no F2B-STREAM-RESYNC. Later residual absurd\n                     * → FULL idle. */\n                    if (!_residual_aligned) {\n                        const uint32_t _to = 20038352u; /* MDL_PUMeterDrain1_0 */\n                        if (_to + 0x20u < g_f2b_fill_sz) {\n                            if (_bst >= 0x10000u && _bst < 0x4F000000u) {\n                                vm_write32(_bst + 0x8u, 0u);\n                                vm_write32(_bst + 0x10u, 0u);\n                                vm_write32(_bst + 0x4u, 0u);\n                            }\n                            g_f2b_fill_file_pos = _to;\n                            f2b_stream_fill(_bst, 0x20u);\n                            _residual_aligned = 1;\n                            { static int _n=0; if(_n++<8)\n                                fprintf(stderr,"[FIOSOPEN] F2B-STREAM-ALIGN residual "\n                                  "from=%u -> %u (MDL_PUMeterDrain1_0 chain)\\n",\n                                  _logical, _to); }\n                            ctx->gpr[29] = 0;\n                            vm_write32((uint32_t)ctx->gpr[31] + 0x1D4u, 0u);\n                            vm_write32((uint32_t)ctx->gpr[31] + 0x1CCu, 2u);\n                            _found = 1;\n                        }\n                    }\n                    if (!_found) {\n                        if (g_f2b_fill_sz) g_f2b_fill_file_pos = g_f2b_fill_sz;\n                        if (_bst >= 0x10000u && _bst < 0x4F000000u) {\n                            vm_write32(_bst + 0x8u, 0u);\n                            vm_write32(_bst + 0x10u, 0u);\n                            vm_write32(_bst + 0x4u, 0u);\n                        }\n                        { static int _n=0; if(_n++<8)\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-EOF-QUIET left=0x%X "\n                              "pos=%u/%u (residual full)\\n",\n                              _stream_left, g_f2b_fill_file_pos, g_f2b_fill_sz); }\n                        vm_write32((uint32_t)ctx->gpr[31] + 0x1D4u, 0u);\n                        vm_write32((uint32_t)ctx->gpr[31] + 0x1ACu, 0u);\n                        vm_write32((uint32_t)ctx->gpr[31] + 0x1CCu, 0u);\n                        ctx->gpr[29] = 0;\n                        _found = 1;\n                    }\n                } else if (_resync_n < 8 && _logical + 0x40u < g_f2b_fill_sz) {\n                    uint32_t _scan_lim = g_f2b_fill_sz - 0x20u;\n                    uint32_t _max = _logical + 0x40000u;\n                    if (_max > _scan_lim) _max = _scan_lim;\n                    for (uint32_t _o = _logical; _o <= _max; _o += 4u) {\n                        unsigned char _hdr[0x20];\n                        if (movie_io_pread(g_f2b_fill_mfd, _hdr, 0x20u, _o) != 0x20u)\n                            break;\n                        uint16_t _hty = (uint16_t)_hdr[0] | ((uint16_t)_hdr[1] << 8);\n                        uint32_t _hsz = (uint32_t)_hdr[4] | ((uint32_t)_hdr[5] << 8)\n                            | ((uint32_t)_hdr[6] << 16) | ((uint32_t)_hdr[7] << 24);\n                        if (_hty == 0u || _hty > 0x20u) continue;\n                        if (_hty == 1u && _hsz == 0u) continue;\n                        if (_hsz > (g_f2b_fill_sz - _o - 0x20u) && _hsz > 0x100u)\n                            continue;\n                        if (_hdr[8] < 0x20 || _hdr[8] >= 0x7f) continue;\n                        if (_o <= _logical) continue;\n                        if (_bst >= 0x10000u && _bst < 0x4F000000u) {\n                            vm_write32(_bst + 0x8u, 0u);\n                            vm_write32(_bst + 0x10u, 0u);\n                            vm_write32(_bst + 0x4u, 0u);\n                        }\n                        g_f2b_fill_file_pos = _o;\n                        f2b_stream_fill(_bst, 0x20u);\n                        _resync_n++;\n                        { static int _n=0; if(_n++<32)\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-RESYNC from=%u -> %u "\n                              "ty=%u sz=%u\\n", _logical, _o, (unsigned)_hty, _hsz); }\n                        ctx->gpr[29] = 0;\n                        vm_write32((uint32_t)ctx->gpr[31] + 0x1D4u, 0u);\n                        vm_write32((uint32_t)ctx->gpr[31] + 0x1CCu, 2u);\n                        _found = 1;\n                        break;\n                    }\n                }\n                if (!_found) {\n                    int _eof = (g_f2b_fill_file_pos >= g_f2b_fill_sz)\n                        || (_stream_left < 0x40u);\n                    if (!_eof) {\n                        { static int _n=0; if(_n++<24){\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-DESYNC-STOP body=0x%X left=0x%X "\n                              "type=0x%X raw_sz=0x%X pos=%u/%u → state=0 rem=0\\n",\n                              _body, _stream_left, _ty, _raw,\n                              g_f2b_fill_file_pos, g_f2b_fill_sz);\n                            fflush(stderr); } }\n                    } else {\n                        if (g_f2b_fill_sz) g_f2b_fill_file_pos = g_f2b_fill_sz;\n                        if (_bst >= 0x10000u && _bst < 0x4F000000u) {\n                            vm_write32(_bst + 0x8u, 0u);\n                            vm_write32(_bst + 0x10u, 0u);\n                            vm_write32(_bst + 0x4u, 0u);\n                        }\n                        { static int _n=0; if(_n++<8)\n                            fprintf(stderr,"[FIOSOPEN] F2B-STREAM-EOF-QUIET left=0x%X pos=%u/%u\\n",\n                              _stream_left, g_f2b_fill_file_pos, g_f2b_fill_sz); }\n                    }\n                    vm_write32((uint32_t)ctx->gpr[31] + 0x1D4u, 0u);\n                    vm_write32((uint32_t)ctx->gpr[31] + 0x1ACu, 0u);\n                    vm_write32((uint32_t)ctx->gpr[31] + 0x1CCu, 0u);\n                    ctx->gpr[29] = 0;\n                }\n            } else if (_body > _stream_left) {\n                { static int _n=0; if(_n++<48){\n                    fprintf(stderr,"[FIOSOPEN] F2B-BODY-CLAMP body=0x%X -> left=0x%X "\n                      "(av=%u file_left=%u pos=%u/%u) hdr_sz_raw=0x%X type=0x%X\\n",\n                      _body, _stream_left, _bav, _file_left,\n                      g_f2b_fill_file_pos, g_f2b_fill_sz, _raw, _ty);\n                    fflush(stderr); } }\n                ctx->gpr[29] = _stream_left;\n            }\n        }\n'
)


def func_span(text: str, sig: str):
    """(inicio, fim) do corpo da 1a funcao com esta assinatura, ou None."""
    i = text.find(sig)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(sig))
    return (i, len(text) if j < 0 else j)


def has_f2b_preamble(text: str) -> bool:
    return "g_f2b_fill_mfd" in text and "g_f2b_fill_sz" in text


def patch_one(path: Path) -> str:
    """Devolve um de: skip, already, applied, refuse-anchor, refuse-preamble."""
    text = path.read_text(encoding="utf-8", errors="replace")
    span = func_span(text, FUNC_SIG)
    if span is None:
        return "skip"
    b0, b1 = span
    body = text[b0:b1]
    if MARKER in body:
        return "already"
    if not has_f2b_preamble(text):
        return "refuse-preamble"
    if body.count(ANCHOR) != 1:
        return "refuse-anchor"
    new_body = body.replace(ANCHOR, ANCHOR + BLOCK, 1)
    new_text = text[:b0] + new_body + text[b1:]
    try:
        path.write_text(new_text, encoding="utf-8", newline="\n")
    except TypeError:
        path.write_text(new_text, encoding="utf-8")
    return "applied"


def main() -> int:
    paths = [p for p in resolve_lift_paths(
        sys.argv[1:], str(Path(__file__).resolve().parent.parent / "recomp_macos_v2"))
        if p.is_file()]
    if not paths:
        print("ERRO: nenhum chunk de lift legivel", file=sys.stderr)
        return 2

    hits = [(p, patch_one(p)) for p in paths]
    found = [(p, r) for p, r in hits if r != "skip"]

    if not found:
        print("ERRO: func_002BA9BC ausente de todos os chunks (%s)" %
              ", ".join(p.name for p in paths), file=sys.stderr)
        return 2
    if len(found) > 1:
        print("ERRO: func_002BA9BC presente em >1 chunk (ambiguo): %s" %
              ", ".join(p.name for p, _ in found), file=sys.stderr)
        print("  NADA foi escrito (recusa atomica).", file=sys.stderr)
        return 2

    path, result = found[0]
    if result == "already":
        print("%s: %s ja' presente (ALREADY)" % (path.name, MARKER))
        return 0
    if result == "refuse-preamble":
        print("ERRO: preambulo F2B (g_f2b_fill_mfd/g_f2b_fill_sz) ausente de %s -- "
              "o bloco chamaria/leria simbolos indefinidos. Corre "
              "patch_f2b_multimb_install.py primeiro, ou revalida se o "
              "preambulo migrou de chunk." % path.name, file=sys.stderr)
        return 2
    if result == "refuse-anchor":
        print("ERRO: ancora (CR-check tipo<0x10 + vm_write32 +0x1D4) ausente ou "
              "duplicada em %s :: func_002BA9BC. O lifter mudou -- revalida o "
              "bloco contra um lift de producao conhecido-bom e regenera este "
              "script." % path.name, file=sys.stderr)
        return 2

    print("%s: %s APPLIED" % (path.name, MARKER))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
