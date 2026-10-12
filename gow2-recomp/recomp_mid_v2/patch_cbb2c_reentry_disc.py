#!/usr/bin/env python3
"""Idempotent diagnostic-only trace for the CBB2C/CB56C reentry question (2026-07-23).

H_A vs H_B discriminator (see notes/2026-07-22-type15-cb56c-attach-diagnostic.md,
notes/2026-07-23-2b2e04-typewrites-diagnostic.md): func_000CB56C unconditionally
zeroes `this+0x4`/`this+0x54` at entry, before any factory lookup or reuse
detection, and nothing later in CB56C ever refills `this+0x4`. func_000CC9D0
spins forever on objects 0x4066D798/0x4066D804 with those fields stuck at 0.

func_000CBB2C loops exactly twice (stride 0x6C, limit base+0xD8) calling
func_000CB56C(base) and func_000CB56C(base+0x6C) -- i.e. one CBB2C entry
already explains both observed CC9D0 objects by construction. The open
question is whether CBB2C/CB56C ever run MORE than once for the same base
(H_B: reset is fine for construction, but something re-invokes construction
on an already-live object) or exactly once (H_A: the reset itself would then
be the only write, and CB56C would be behaving as a correct from-scratch
constructor -- in which case the missing piece is elsewhere, not a "guard"
inside CB56C/CBB2C).

This patch does NOT change default behaviour (PS3_TRACE_CBB2C_DISC unset ->
byte-identical to upstream). Gated ON it adds, at the very top of each
function (before any register/stack mutation, so gpr[3]/lr are still the raw
incoming arguments):
  - func_000CBB2C: base EA, guest LR, and a running per-base call count (small
    linear-scan table, plenty for the handful of distinct bases expected).
  - func_000CB56C: obj EA, guest LR, and the PRE-reset values of +0x4, +0x54,
    +0xD8 (captured before line ~161540 unconditionally zeroes them).
"""
from pathlib import Path
import sys
from lift_paths import resolve_lift_paths

# CORRECCAO 2026-07-25 (re-lift): a agulha antiga era a assinatura da funcao
# SEGUIDA do bloco de probe "[POSTINTRO] enter func_...". Esse bloco POSTINTRO
# era uma edicao manual de sessao que NENHUM patch_*.py escreve (grep POSTINTRO
# em patch_*.py: so' aparece como agulha, nunca como insercao) e por isso nao
# existe no lift limpo -> as duas agulhas falhavam em todos os chunks.
# O bloco POSTINTRO nunca foi requisito de comportamento, so' ancora: o proprio
# docstring pede a insercao "no topo da funcao, antes de qualquer mutacao de
# registo/stack". Passamos a ancorar na ASSINATURA da funcao (unica em todo o
# lift, verificado por grep -c = 1 em ppu_recomp_000.cpp e 0 nos restantes),
# o que casa com o lift NOVO e tambem com o antigo (onde o nosso bloco fica
# apenas ANTES do POSTINTRO -- mesma posicao semantica: topo da funcao).
MARKER = "[CBB2C-DISC]"
MARKER_CB56C = "[CB56C-DISC]"


def patch(t: str) -> tuple[str, str]:
    """Devolve (texto, estado) com estado em {APPLIED, ALREADY, SKIP}."""
    cbb2c_needle = "void func_000CBB2C(ppu_context* ctx) {\n"
    cbb2c_insert = cbb2c_needle + '''
        { static int _on=-1; if(_on<0){extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_CBB2C_DISC"); _on=(e&&*e&&*e!='0')?1:0;}
          if(_on){
            static uint32_t _bases[16]; static uint32_t _counts[16]; static int _nbases=0;
            uint32_t _base=(uint32_t)ctx->gpr[3];
            uint32_t _lr=(uint32_t)ctx->lr;
            int _idx=-1;
            for (int _i=0;_i<_nbases;_i++) if (_bases[_i]==_base) { _idx=_i; break; }
            if (_idx<0 && _nbases<16) { _idx=_nbases++; _bases[_idx]=_base; _counts[_idx]=0; }
            if (_idx>=0) _counts[_idx]++;
            static int _n2=0; if(_n2++<64)
              fprintf(stderr,"[CBB2C-DISC] base=0x%08X lr=0x%08X count_for_base=%u\\n",
                _base, _lr, (unsigned)(_idx>=0?_counts[_idx]:0u));
            fflush(stderr);
          } }
'''

    cb56c_needle = "void func_000CB56C(ppu_context* ctx) {\n"
    cb56c_insert = cb56c_needle + '''
        { static int _on=-1; if(_on<0){extern char* getenv(const char*);
            const char* e=getenv("PS3_TRACE_CBB2C_DISC"); _on=(e&&*e&&*e!='0')?1:0;}
          if(_on){
            uint32_t _obj=(uint32_t)ctx->gpr[3];
            uint32_t _lr=(uint32_t)ctx->lr;
            uint32_t _pre4 = vm_read32(_obj + 0x4u);
            uint32_t _pre54 = (uint32_t)vm_read8(_obj + 0x54u);
            uint32_t _preD8 = vm_read32(_obj + 0xD8u);
            static int _n2=0; if(_n2++<64)
              fprintf(stderr,"[CB56C-DISC] obj=0x%08X caller_lr=0x%08X pre+4=0x%08X pre+54=0x%02X pre+D8=0x%08X\\n",
                _obj, _lr, _pre4, _pre54, _preD8);
            fflush(stderr);
          } }
'''

    # CORRECCAO 2026-07-25 (chunk-fixo): com um DIRECTORIO em argv o script varre
    # os 7 chunks; as duas funcoes vivem so' num deles. Antes qualquer chunk sem a
    # agulha dava "FAILED" e rc=1. Agora cada site e' tratado de forma
    # independente e "funcao nao esta' neste chunk" e' SKIP, nao falha; so' se
    # NENHUM chunk tiver as funcoes e' que main() devolve rc=1.
    state = "SKIP"
    if cbb2c_needle in t:
        if MARKER in t:
            state = "ALREADY"
        else:
            t = t.replace(cbb2c_needle, cbb2c_insert, 1)
            state = "APPLIED"
    if cb56c_needle in t:
        if MARKER_CB56C in t:
            state = "ALREADY" if state != "APPLIED" else "APPLIED"
        else:
            t = t.replace(cb56c_needle, cb56c_insert, 1)
            state = "APPLIED"
    return t, state


def main() -> int:
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_000.cpp")
    hit = False
    for p in paths:
        if not p.exists():
            print(f"skip {p}")
            continue
        t = p.read_text()
        t2, state = patch(t)
        if state == "SKIP":
            continue
        hit = True
        if t2 != t:
            p.write_text(t2, newline="\n")
            print(f"APPLIED {p}")
        else:
            print(f"ALREADY-APPLIED {p}")
    if not hit:
        print("FAILED: func_000CBB2C/func_000CB56C nao encontradas em nenhum chunk")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
