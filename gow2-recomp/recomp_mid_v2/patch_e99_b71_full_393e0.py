#!/usr/bin/env python3
r"""E99 -- fix de raiz da parede [G] (parte 4): o B71 #055 volta a correr func_000393E0 por default.

Medido (E97/E98, 2026-09-02): depois do R_Hero01 a thread principal entra em func_000B8368 →
func_0003209C → func_002A466C(*(X+0x1FD8)) com X+0x1FD8 == 0 e percorre uma lista a partir do
endereco 0x70 para sempre (frame preso em 1). No console *(X+0x1FD8) = objecto real (0x30669020,
lista +0x70 vazia e circular). Os unicos escritores de +0x1FD8 sao func_00034A60 e **func_000393E0**
-- e o bloco de Julho (patch_b71_cb56c_reuse_block.py) substituia o func_000393E0 por um
"HLE-lite" que so' carimba a vtable e zera tres campos (PS3_B71_FULL_393E0=1 para o completo).
Com o E90 o estado que o fazia "encravar" ja' nao existe. Default: completo; PS3_B71_LITE_393E0=1
repoe o lite so' para A/B. Idempotente (marcador E99-FULL-393E0)."""
import sys
from pathlib import Path
ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"
OLD = '''            const char* _full = getenv("PS3_B71_FULL_393E0");
            if (_full && *_full && *_full != '0') {
                func_000393E0(ctx); DRAIN_TRAMPOLINE(ctx);
            } else {'''
NEW = '''            /* E99-FULL-393E0: full constructor by default; PS3_B71_LITE_393E0=1 restores the lite for A/B */
            const char* _lite = getenv("PS3_B71_LITE_393E0");
            const char* _full = getenv("PS3_B71_FULL_393E0");
            if (!(_lite && *_lite && *_lite != '0') || (_full && *_full && *_full != '0')) {
                func_000393E0(ctx); DRAIN_TRAMPOLINE(ctx);
            } else {'''
def main():
    for f in sorted(ROOT.glob("ppu_recomp_*.cpp")):
        s=f.read_text(errors='replace')
        if "E99-FULL-393E0" in s: print("E99: ALREADY"); return 0
        if OLD not in s: continue
        if s.count(OLD)!=1: print("E99: agulha ambigua"); return 2
        f.write_text(s.replace(OLD,NEW,1)); print(f"E99: aplicado em {f.name}"); return 0
    print("E99: bloco HLE-lite ausente (correr patch_b71_cb56c_reuse_block.py antes)"); return 2
if __name__=="__main__": raise SystemExit(main())
