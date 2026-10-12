#!/usr/bin/env python3
"""Idempotent M1 discriminator probes for TYPE15 / CC9D0 spin (diag only).

Injects into ppu_recomp_000.cpp (local lift, gitignored):

  [CC9D0-DISC] this=… f4=… f54=… prod=… vt=… child_head=… parent=…
  [CB56C-DISC] obj=… prod=… reused=… vt=… after_attach child_head=…

Gate (default OFF) — shared contract Python env_on + injected C:
  ON  iff getenv present, non-empty, and first char is '1'.
  OFF for unset / empty / '0' / 'false' / 'off' / 'no' / anything else.

  PS3_TYPE15_DISC=1   — preferred; does NOT enable the spammy PS3_TRACE_CC9D0
                        [CC9D0] iter logger.
  PS3_TRACE_CC9D0=1   — also enables these DISC lines (same first-char-'1' rule);
                        existing [CC9D0] spam still separate (its own gate).

Hard cap: ≤20 total [CC9D0-DISC]+[CB56C-DISC] lines per process (shared
file-scope counter g_ps3_type15_disc_n). Safe VM reads only (EA bounds).

Does not change guest behaviour when gate is OFF.

Estado 2026-07-25 (relift limpo) — NAO APLICAVEL: pre-condicoes orfas
---------------------------------------------------------------------
Este patch escreve, mas so' sabe injectar POR CIMA de blocos que sao edicoes
manuais do lift antigo. Medido contra um lift limpo do ppu_lifter.py actual:

  ancora exigida                                         existe?  escritor
  -----------------------------------------------------  -------  ----------
  extern "C" void ppu_giant_lock_acquire(void);           nao      patch_fios_stop_yield.py /
                                                                   patch_fios_cancel_yield.py
                                                                   (mas escrevem noutro chunk)
  bloco CB56C attach: "[POSTINTRO] CB56C after 2A4FE4"     nao      NENHUM
    + "[TYPE15] CB56C SKIP_ATTACH" + variavel ty15_reused
  bloco logger "[CC9D0] iter=..." em func_000CC9D0         nao      NENHUM

Nenhum dos tres blocos existe tao-pouco no lift de producao actual
(recomp_macos_v3/*.cpp: 0 ocorrencias de "[POSTINTRO] CB56C after 2A4FE4",
"ty15_reused", "[TYPE15] CB56C SKIP_ATTACH", "[CC9D0] iter="). Em todo o repo
esses textos so' aparecem neste script e em notes/*.md.

No lift limpo o CB56C chama func_002A4FE4 de forma INCONDICIONAL
(ppu_recomp_000.cpp: "ctx->lr = 0x000CB6AC; func_002A4FE4(ctx);
DRAIN_TRAMPOLINE(ctx);") — nao ha' if/else de REUSE nem variavel ty15_reused
para o [CB56C-DISC] imprimir. Logo isto NAO e' drift de agulha reparavel por
regex: e' comportamento ausente. Reconstruir os blocos (inventar ty15_reused,
inventar o ramo SKIP_ATTACH) seria forjar resultado (regra 4 do CLAUDE.md);
o caminho legitimo e' escrever primeiro os escritores desses blocos.

Alteracao de 2026-07-25: apenas diagnostico. Chunks que nao contem as funcoes
alvo passam a ser skip (o lifter foi de 31 para 7 chunks e o script varria os
7, imprimindo 7x o mesmo FAILED), e a mensagem de falha nomeia a pre-condicao
em falta. O rc continua != 0 e nenhuma agulha foi relaxada.
"""
from pathlib import Path
import re
import sys
from typing import Optional
from lift_paths import resolve_lift_paths

MARKER = "[CC9D0-DISC]"
COUNTER_MARKER = "g_ps3_type15_disc_n"

# Funcoes alvo: se nenhuma vive no chunk, o chunk nao interessa (skip, nao falha).
TARGET_FNS_RE = re.compile(r"void\s+func_000C(?:B56C|C9D0)\s*\(\s*ppu_context\s*\*\s*ctx\s*\)")

# C snippet shared by all DISC inject sites (must match env_on).
# ON iff first char is '1' (unset/empty/0/false all OFF).
_C_DISC_GATE = (
    "((e1 && *e1=='1') || (e2 && *e2=='1')) ? 1 : 0"
)


def env_on(name: str, raw: Optional[str]) -> bool:
    """ON iff first char is '1' (unset/empty/0/false all OFF)."""
    del name
    if raw is None or raw == "":
        return False
    return raw[0] == "1"


def strip_disc(t: str) -> str:
    """Remove prior M1 DISC injects (any gate variant) so patch() can re-apply."""
    # File-scope counter
    t = re.sub(
        r"/\* M1 disc budget: shared across CC9D0/CB56C DISC lines \(cap 20\)\. \*/\n"
        r"static int g_ps3_type15_disc_n = 0;\n",
        "",
        t,
        count=1,
    )
    # CB56C full + SKIP after_attach blocks (both comment markers)
    t = re.sub(
        r"\n            /\* M1 after_attach disc \(full path\) \*/\n"
        r"            \{ static int _disc_on = -1;.*?g_ps3_type15_disc_n\+\+;\n"
        r"              \}\n"
        r"            \}",
        "",
        t,
        count=1,
        flags=re.S,
    )
    t = re.sub(
        r"\n            /\* M1 after_attach disc \(SKIP_ATTACH path\) \*/\n"
        r"            \{ static int _disc_on = -1;.*?g_ps3_type15_disc_n\+\+;\n"
        r"              \}\n"
        r"            \}",
        "",
        t,
        count=1,
        flags=re.S,
    )
    # CC9D0-DISC block
    t = re.sub(
        r"\n        /\* M1: capped CC9D0-DISC \(PS3_TYPE15_DISC=1 preferred; shared budget ≤20\) \*/\n"
        r"        \{ static int _disc_on = -1;.*?_dn\+\+;\n"
        r"          \}\n"
        r"        \}",
        "",
        t,
        count=1,
        flags=re.S,
    )
    return t


def patch(t: str) -> str:
    if MARKER in t:
        return t

    # File-scope shared counter (≤20 lines total).
    counter_anchor = 'extern "C" void ppu_giant_lock_acquire(void);\n'
    if COUNTER_MARKER not in t:
        if counter_anchor not in t:
            raise SystemExit(
                "PRE-CONDICAO AUSENTE: declaracao 'extern \"C\" void "
                "ppu_giant_lock_acquire(void);' nao esta neste chunk "
                "(escrita por patch_fios_stop_yield.py / patch_fios_cancel_yield.py)")
        t = t.replace(
            counter_anchor,
            counter_anchor
            + "/* M1 disc budget: shared across CC9D0/CB56C DISC lines (cap 20). */\n"
            + "static int g_ps3_type15_disc_n = 0;\n",
            1,
        )

    # --- CB56C: after full attach (post 2A4FE4) ---
    cb56c_full_needle = '''            { static int _n=0; if(_n++<8)
                fprintf(stderr,"[POSTINTRO] CB56C after 2A4FE4\\n"); }
          } else {
            ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];
            vm_write32(ctx->gpr[31] + 0x8, ctx->gpr[28]);
            { static int _n=0; if(_n++<8)
                fprintf(stderr,"[TYPE15] CB56C SKIP_ATTACH product=0x%08X (legacy)\\n",
                  (unsigned)(uint32_t)ctx->gpr[29]); }
          }'''

    cb56c_full_insert = f'''            {{ static int _n=0; if(_n++<8)
                fprintf(stderr,"[POSTINTRO] CB56C after 2A4FE4\\n"); }}
            /* M1 after_attach disc (full path) */
            {{ static int _disc_on = -1;
              if (_disc_on < 0) {{
                extern char* getenv(const char*);
                const char* e1 = getenv("PS3_TYPE15_DISC");
                const char* e2 = getenv("PS3_TRACE_CC9D0");
                _disc_on = {_C_DISC_GATE};
              }}
              if (_disc_on && g_ps3_type15_disc_n < 20) {{
                uint32_t _obj = (uint32_t)ctx->gpr[31];
                uint32_t _prod = (uint32_t)ctx->gpr[29];
                uint32_t _vt = 0, _ch = 0;
                if (_prod >= 0x10000u && _prod < 0x4F000000u) {{
                  _vt = vm_read32(_prod);
                  if ((_prod + 0x70u) >= 0x10000u && (_prod + 0x70u) < 0x4F000000u)
                    _ch = vm_read32(_prod + 0x70u);
                }}
                fprintf(stderr,
                  "[CB56C-DISC] obj=0x%08X prod=0x%08X reused=%d vt=0x%08X after_attach child_head=0x%08X\\n",
                  (unsigned)_obj, (unsigned)_prod, ty15_reused, (unsigned)_vt, (unsigned)_ch);
                fflush(stderr);
                g_ps3_type15_disc_n++;
              }}
            }}
          }} else {{
            ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];
            vm_write32(ctx->gpr[31] + 0x8, ctx->gpr[28]);
            {{ static int _n=0; if(_n++<8)
                fprintf(stderr,"[TYPE15] CB56C SKIP_ATTACH product=0x%08X (legacy)\\n",
                  (unsigned)(uint32_t)ctx->gpr[29]); }}
            /* M1 after_attach disc (SKIP_ATTACH path) */
            {{ static int _disc_on = -1;
              if (_disc_on < 0) {{
                extern char* getenv(const char*);
                const char* e1 = getenv("PS3_TYPE15_DISC");
                const char* e2 = getenv("PS3_TRACE_CC9D0");
                _disc_on = {_C_DISC_GATE};
              }}
              if (_disc_on && g_ps3_type15_disc_n < 20) {{
                uint32_t _obj = (uint32_t)ctx->gpr[31];
                uint32_t _prod = (uint32_t)ctx->gpr[29];
                uint32_t _vt = 0, _ch = 0;
                if (_prod >= 0x10000u && _prod < 0x4F000000u) {{
                  _vt = vm_read32(_prod);
                  if ((_prod + 0x70u) >= 0x10000u && (_prod + 0x70u) < 0x4F000000u)
                    _ch = vm_read32(_prod + 0x70u);
                }}
                fprintf(stderr,
                  "[CB56C-DISC] obj=0x%08X prod=0x%08X reused=%d vt=0x%08X after_attach child_head=0x%08X\\n",
                  (unsigned)_obj, (unsigned)_prod, ty15_reused, (unsigned)_vt, (unsigned)_ch);
                fflush(stderr);
                g_ps3_type15_disc_n++;
              }}
            }}
          }}'''

    if cb56c_full_needle not in t:
        raise SystemExit(
            "PRE-CONDICAO AUSENTE: bloco de attach CB56C ([POSTINTRO] CB56C after "
            "2A4FE4 / [TYPE15] CB56C SKIP_ATTACH / ty15_reused) nao existe no lift "
            "-- edicao manual sem escritor no repo, nao e' drift de agulha")
    t = t.replace(cb56c_full_needle, cb56c_full_insert, 1)

    # --- CC9D0: after existing TRACE_CC9D0 block, before f54 branch ---
    # Leave pre-existing [CC9D0] spam gate as-is (not part of DISC contract).
    cc9d0_needle = '''        { static int on=-1; if(on<0){extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_CC9D0");
            on=(e&&*e&&*e!='0')?1:0;}
          if(on){ static long n=0; static uint32_t last_this=0xFFFFFFFFu;
            uint32_t th=(uint32_t)ctx->gpr[31];
            uint8_t f54=(uint8_t)ctx->gpr[0]; uint32_t f4=vm_read32(th+0x4);
            if(n<40 || th!=last_this || (n%100000)==0){
              fprintf(stderr,"[CC9D0] iter=%ld this=0x%08X f54=%d f4=%u (prev_this=0x%08X)\\n",
                n, th, (int)(int8_t)f54, f4, last_this);
              fflush(stderr);
            }
            last_this=th; n++;
          } }
        if ((!((ctx->cr >> 0) & 2))) { g_trampoline_fn = (void(*)(void*))func_000CCBF0; return; }'''

    cc9d0_insert = f'''        {{ static int on=-1; if(on<0){{extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_CC9D0");
            on=(e&&*e&&*e!='0')?1:0;}}
          if(on){{ static long n=0; static uint32_t last_this=0xFFFFFFFFu;
            uint32_t th=(uint32_t)ctx->gpr[31];
            uint8_t f54=(uint8_t)ctx->gpr[0]; uint32_t f4=vm_read32(th+0x4);
            if(n<40 || th!=last_this || (n%100000)==0){{
              fprintf(stderr,"[CC9D0] iter=%ld this=0x%08X f54=%d f4=%u (prev_this=0x%08X)\\n",
                n, th, (int)(int8_t)f54, f4, last_this);
              fflush(stderr);
            }}
            last_this=th; n++;
          }} }}
        /* M1: capped CC9D0-DISC (PS3_TYPE15_DISC=1 preferred; shared budget ≤20) */
        {{ static int _disc_on = -1;
          if (_disc_on < 0) {{
            extern char* getenv(const char*);
            const char* e1 = getenv("PS3_TYPE15_DISC");
            const char* e2 = getenv("PS3_TRACE_CC9D0");
            _disc_on = {_C_DISC_GATE};
          }}
          if (_disc_on && g_ps3_type15_disc_n < 20) {{
            static long _dn = 0;
            static uint32_t _last_th = 0xFFFFFFFFu;
            uint32_t th = (uint32_t)ctx->gpr[31];
            int sample = (_dn < 8) || (th != _last_th) || ((_dn % 100000) == 0);
            if (sample && g_ps3_type15_disc_n < 20) {{
              uint32_t f4 = 0, f54 = 0, prod = 0, vt = 0, ch = 0, parent = 0;
              if (th >= 0x10000u && th < 0x4F000000u) {{
                f4 = vm_read32(th + 0x4u);
                f54 = (uint32_t)vm_read8(th + 0x54u);
                prod = vm_read32(th + 0x8u);
                parent = vm_read32(th + 0xD8u); /* prior disc interest; may be 0 */
              }}
              if (prod >= 0x10000u && prod < 0x4F000000u) {{
                vt = vm_read32(prod);
                if ((prod + 0x70u) >= 0x10000u && (prod + 0x70u) < 0x4F000000u)
                  ch = vm_read32(prod + 0x70u);
              }}
              fprintf(stderr,
                "[CC9D0-DISC] this=0x%08X f4=0x%08X f54=0x%02X prod=0x%08X vt=0x%08X child_head=0x%08X parent=0x%08X\\n",
                (unsigned)th, (unsigned)f4, (unsigned)f54, (unsigned)prod,
                (unsigned)vt, (unsigned)ch, (unsigned)parent);
              fflush(stderr);
              g_ps3_type15_disc_n++;
            }}
            _last_th = th;
            _dn++;
          }}
        }}
        if ((!((ctx->cr >> 0) & 2))) {{ g_trampoline_fn = (void(*)(void*))func_000CCBF0; return; }}'''

    if cc9d0_needle not in t:
        raise SystemExit(
            "PRE-CONDICAO AUSENTE: logger [CC9D0] iter= em func_000CC9D0 nao existe "
            "no lift -- edicao manual sem escritor no repo, nao e' drift de agulha")
    t = t.replace(cc9d0_needle, cc9d0_insert, 1)

    if MARKER not in t:
        raise SystemExit("patch applied but MARKER missing — logic error")
    return t


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_000.cpp")
    rc = 0
    for p in paths:
        if not p.exists():
            print(f"skip {p}")
            rc = 1
            continue
        t = p.read_text()
        # func_000CB56C / func_000CC9D0 vivem num so' chunk: nos outros isto e'
        # skip, nao falha (o lifter passou de 31 para 7 chunks).
        if MARKER not in t and not TARGET_FNS_RE.search(t):
            print(f"skip {p} (func_000CB56C/func_000CC9D0 nao estao neste chunk)")
            continue
        had = MARKER in t
        if had:
            t = strip_disc(t)
            if MARKER in t:
                print(f"FAILED {p}: strip_disc left MARKER (regex shape drift)")
                rc = 1
                continue
        try:
            t2 = patch(t)
        except SystemExit as e:
            print(f"FAILED {p}: {e}")
            rc = 1
            continue
        if t2 != p.read_text():
            p.write_text(t2)
            print(f"{'REAPPLIED' if had else 'APPLIED'} {p}")
        else:
            print(f"ALREADY-APPLIED {p}")
    return rc


if __name__ == "__main__":
    # Keep env_on importable / self-check when run with --self-test
    if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
        assert env_on("PS3_TRACE_CC9D0", None) is False
        assert env_on("PS3_TRACE_CC9D0", "0") is False
        assert env_on("PS3_TRACE_CC9D0", "") is False
        assert env_on("PS3_TRACE_CC9D0", "false") is False
        assert env_on("PS3_TRACE_CC9D0", "1") is True
        assert env_on("PS3_TYPE15_DISC", "1") is True
        assert env_on("PS3_TYPE15_DISC", "true") is False
        # Injected C gate must match: first char == '1'
        assert "*e1=='1'" in _C_DISC_GATE and "*e2=='1'" in _C_DISC_GATE
        assert "*e1!='0'" not in _C_DISC_GATE
        print("patch self-test env_on: PASS")
        raise SystemExit(0)
    raise SystemExit(main())
