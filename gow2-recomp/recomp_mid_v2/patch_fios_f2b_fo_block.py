#!/usr/bin/env python3
"""Instala o bloco orfao FIOS-F2B-MOVIEIO / F2B-FO-CTOR em func_0030D5CC.

Porque existe
-------------
`patch_fios_f2b_fo_ctor.py` e' um VERIFICADOR PURO (zero escritas) dos
marcadores `F2B-FO-CTOR` / `F2B-MOVIEIO`. O proprio docstring dele admitia que
o comportamento so' existia como "session edit": o bloco e' uma edicao MANUAL
dentro do lift gitignored e NENHUM script versionado o repunha. O dono nominal
do bloco maior, `patch_fios_f2a_f2b_wad.py`, nao escreve nada -- imprime a
receita ("re-apply F2a/F2b host helpers from this session's lift diffs").

Este ficheiro e' o ESCRITOR que faltava. O verificador continua a verificar.

O que instala (extraido verbatim do lift de producao `recomp_macos_v2`,
ppu_recomp_001.cpp, dentro de `func_0030D5CC`)
----------------------------------------------------------------------
O bloco `FIOS-F2B-MOVIEIO` inteiro -- e' a caixa onde o `F2B-FO-CTOR` vive; nao
sao separaveis. Quando o dearchiver recusa um caminho que existe no
`movie_cache` (R_LglScA / R_PermA / .m2v / .wav) e `r3==0`:

  - `movie_io_open(path)` (com fallback ao basename) -> mfd host;
  - REBIND do FO natural quando e' re-open de filme (`g_f2b_natural_movie_fo`),
    sem voltar a correr o ctor (re-ctor corrompe ponteiros guest);
  - caso contrario **F2B-FO-CTOR**: constroi o FO pela cadeia de ctor NATURAL do
    guest -- `func_0031F1A4(FO, pbuf)` e `func_00307774(FO+0x30, path)` -- para
    que o objecto string em +0x30 e o hash em +0x34 sejam os do `file_new` real,
    em vez de um FO carimbado a mao;
  - link na lista de FOs do media (+0x234/+0x238/+0x240), magic FIOS/"fh  ",
    `f2b_fo_mfd_put`, F2B-FO-SIZE (+0x48/+0x4C), preload da TOC para WAD pequeno
    e DONEFORCE da op (+0x90/+0x44/+0xCC + `ps3_fios_sticky_publish`).

Ancora: imediatamente ANTES do trio gerado
`r0=0x8001<<16 / r31=r3 / r0|=0x70A` (unico na funcao), que e' exactamente a
posicao que o bloco ocupa no lift de producao.

O que NAO instala (tem escritor proprio, ou e' outro orfao)
------------------------------------------------------------
  FIOS-OPEN-PROBE(op_alloc|file_new) ... patch_fios_open_probe.py (corre depois)
  FIOS-HOST-POP / FIOS-FREELIST-PROBE .. orfaos de `patch_fios_f2a_f2b_wad.py`
  FIOS-FO-DUMP + `g_f2b_natural_movie_fo = r3` .. enhancement de 2a geracao
      DENTRO da probe de `patch_fios_open_probe.py`; mexer la' seria reescrever
      o bloco de outro patch. Consequencia honesta: sem esse enhancement o ramo
      REBIND nunca dispara (`g_f2b_natural_movie_fo` fica 0) e usa-se sempre o
      ctor -- que e' o caminho dos WADs, o alvo deste item.

Dependencias que este script NAO resolve (declaradas, nunca escondidas)
-----------------------------------------------------------------------
O bloco usa `g_f2b_natural_movie_fo`, `f2b_fo_mfd_put`, `movie_io_pread` e
`vm_base`. Sao INJECCOES DE PREAMBULO (host, `static` no proprio chunk),
inventariadas em `lift_baseline/MANIFEST.tsv`, com copia versionada em
`lift_baseline/injected_001.cpp`. Nenhum patch_*.py as instala: familia de
orfaos separada, com dono proprio. Como sao `static` NO MESMO chunk, este script
NAO emite declaracoes `extern` (dariam conflito com a definicao quando ela
voltar) -- limita-se a AVISAR quais faltam. Mesmo criterio do precedente
`patch_ce03c_introseq_block.py`, cujo bloco tambem depende de simbolos de
preambulo (`ppu_giant_lock_release`, `usleep`).
`ps3_fios_sticky_publish` vem de `patch_fios_sticky.py` (esse tem escritor).

Como foi gerado
---------------
Nao foi transcrito a mao. Um gerador leu o lift de producao e o lift limpo no
slot deste script, extraiu o bloco entre a sua primeira linha de comentario e o
trio-ancora, e emitiu este ficheiro com o texto embebido via `repr()`.

Contrato de rc
--------------
  rc=0  aplicado, ou ja' aplicado (ALREADY)
  rc=1  nenhum chunk tem `func_0030D5CC`
  rc=2  a funcao existe mas a ancora gerada NAO e' a esperada (0 ou >1
        ocorrencias) -> RECUSA. O lifter mudou de forma: revalidar o bloco
        contra um lift de producao conhecido-bom e regerar este script.

Uso:  patch_fios_f2b_fo_block.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

SIG = "void func_0030D5CC(ppu_context* ctx) {\n"
MARKER = "F2B-FO-CTOR"
ANCHOR = '        ctx->gpr[0] = (int64_t)(int32_t)((uint32_t)0x8001 << 16);\n        ctx->gpr[31] = ctx->gpr[3] | ctx->gpr[3];\n        ctx->gpr[0] = ctx->gpr[0] | 0x70A;\n'
BLOCK = '        /* FIOS-F2B-MOVIEIO: when dearchiver rejects a path that lives in\n         * movie_cache (R_LglScA/R_PermA), open via movie_io and return a\n         * host-backed file object so the op can complete (Windows path). */\n        { static int _on=-1; if(_on<0){const char* e=getenv("PS3_FIOS_F2B_MOVIEIO");\n            _on=(!e||*e!=\'0\')?1:0;}\n          if(_on && (uint32_t)ctx->gpr[3]==0u){\n            char _pt[160]; _pt[0]=0;\n            { uint32_t _a=(uint32_t)(ctx->gpr[25]);\n              if(_a && _a < 0x4F000000u){ int _i;\n                for(_i=0;_i<159;_i++){ unsigned char _c=(unsigned char)vm_read8(_a+_i);\n                  _pt[_i]=(char)_c; if(!_c) break; }\n                _pt[159]=0; } }\n            /* WAD (R_Lgl/R_Perm) + movie re-open after MovieStop (F2b measured\n             * 2026-07-22: natural file_new dies on invalid mutex for smlogo_v2.m2v;\n             * HOST-POP salvages the op but FO stays null without this path). */\n            if(_pt[0] && (strstr(_pt,"wad")||strstr(_pt,"WAD")||strstr(_pt,"lgl")||strstr(_pt,"Lgl")||strstr(_pt,"perm")||strstr(_pt,"Perm")\n                ||strstr(_pt,"movies")||strstr(_pt,"Movies")||strstr(_pt,".m2v")||strstr(_pt,".M2V")\n                ||strstr(_pt,".wav")||strstr(_pt,".WAV"))){\n              extern unsigned movie_io_open(const char*, unsigned*);\n              unsigned sz=0;\n              unsigned mfd = movie_io_open(_pt, &sz);\n              if(!mfd){\n                /* also try basename */\n                const char* base=_pt; for(const char* s=_pt; *s; s++) if(*s==\'/\') base=s+1;\n                mfd = movie_io_open(base, &sz);\n              }\n              if(mfd){\n                /* F2B-FO-CTOR: build FO via natural guest ctor chain\n                 * (func_0031F1A4 + func_00307774) so +0x34 path hash and\n                 * string object at +0x30 match real file_new. Hand-crafted\n                 * FO left freelist in 0x840000xx after R_Perm open.\n                 *\n                 * Movie re-open: REBIND natural FO only (no re-ctor). Re-running\n                 * 0031F1A4 on the first-open FO after MovieStop corrupts guest\n                 * pointers → GetPicture then path-as-code hang (0x005F6D6F). */\n                static uint32_t s_foff=0;\n                if(!s_foff) s_foff = 0x430185D8u;\n                uint32_t fea = 0;\n                int _is_mov = (strstr(_pt,"movies")||strstr(_pt,".m2v")||strstr(_pt,".M2V")\n                               ||strstr(_pt,".wav")||strstr(_pt,".WAV")) ? 1 : 0;\n                int _reuse = 0;\n                if (_is_mov && g_f2b_natural_movie_fo >= 0x10000u\n                    && g_f2b_natural_movie_fo < 0x4F000000u\n                    && vm_read32(g_f2b_natural_movie_fo+0x0u)==0x46494F53u) {\n                  fea = g_f2b_natural_movie_fo;\n                  _reuse = 1;\n                } else {\n                  fea = s_foff;\n                  s_foff += 0x200u;\n                }\n                uint32_t media=(uint32_t)ctx->gpr[26];\n                uint32_t parent=0;\n                if(media>=0x10000u && media<0x4F000000u){\n                  uint32_t head=vm_read32(media+0x234u);\n                  if(head>=0x10000u && head<0x4F000000u\n                     && vm_read32(head+0x0u)==0x46494F53u) parent=head;\n                }\n                if(fea+0x180u < 0x4F000000u && media>=0x10000u){\n                  if (_reuse) {\n                    /* Leave FO body/path intact; only rebind size + host mfd. */\n                    f2b_fo_mfd_put(fea, mfd, sz);\n                    vm_write32(fea+0x48u, 0u);\n                    vm_write32(fea+0x4Cu, sz);\n                    vm_write32(fea+0x50u, 1u);\n                    vm_write32(fea+0x8u, media);\n                    { uint32_t op=(uint32_t)ctx->gpr[27];\n                      if(op>=0x10000u && op<0x4F000000u){\n                        vm_write32(op+0x90u, 1u);\n                        vm_write32(op+0x44u, 0u);\n                        vm_write32(op+0xCCu, 0u);\n                        ps3_fios_sticky_publish(op);\n                      } }\n                    ctx->gpr[3]=fea;\n                    fprintf(stderr,\n                      "[FIOSOPEN] F2B-REBIND natural FO=0x%08X mfd=0x%X sz=%u path=\'%s\' (no re-ctor)\\n",\n                      fea, mfd, sz, _pt);\n                    fflush(stderr);\n                  } else {\n                  for(uint32_t i=0;i<0x58u;i+=4u) vm_write32(fea+i, 0u);\n                  uint32_t pbuf = fea + 0x80u;\n                  for(uint32_t i=0;i<0x100u;i+=4u) vm_write32(pbuf+i, 0u);\n                  { const char* s=_pt; for(int i=0;i<0xFF && s[i];i++)\n                      vm_write8(pbuf+(uint32_t)i, (unsigned char)s[i]);\n                    vm_write8(pbuf+0xFF, 0); }\n                  /* Save guest regs clobbered by ctor helpers (incl. r14-r31:\n                   * path lives in r25 for subsequent .wav open; measured\n                   * wall_after_f2b file_new #5 path=\'\' after F2B m2v). */\n                  uint64_t sv[32];\n                  for(int ri=0;ri<32;ri++) sv[ri]=ctx->gpr[ri];\n                  uint64_t sv_lr=ctx->lr;\n                  /* 0031F1A4(FO, path_end_or_path): natural fh/"FIOS" init */\n                  ctx->gpr[3]=fea;\n                  ctx->gpr[4]=pbuf;\n                  func_0031F1A4(ctx); DRAIN_TRAMPOLINE(ctx);\n                  /* media + flags like file_new tail */\n                  vm_write32(fea+0x8u, media);\n                  vm_write32(fea+0x50u, 1u);\n                  /* Normalize path into stack-like buffer at pbuf+0x100 then\n                   * 00307774(FO+0x30, path) → FO+0x30 ptr + FO+0x34 hash */\n                  uint32_t nbuf = pbuf; /* already has path */\n                  ctx->gpr[3]=fea+0x30u;\n                  ctx->gpr[4]=nbuf;\n                  func_00307774(ctx); DRAIN_TRAMPOLINE(ctx);\n                  /* If 00307774 did not install path ptr, force pbuf */\n                  if(vm_read32(fea+0x30u)==0u) vm_write32(fea+0x30u, pbuf);\n                  /* Media FO list (file_new loc_0030D460) */\n                  { uint32_t head=vm_read32(media+0x234u);\n                    vm_write32(fea+0xCu, head);\n                    vm_write32(media+0x234u, fea);\n                    uint32_t cnt=vm_read32(media+0x238u);\n                    vm_write32(media+0x238u, cnt+1u);\n                    if(vm_read32(media+0x240u) < cnt+1u)\n                      vm_write32(media+0x240u, cnt+1u);\n                    fprintf(stderr,"[FIOSOPEN] F2B-MEDIALINK media=0x%08X fo=0x%08X prev=0x%08X cnt=%u\\n",\n                      media, fea, head, cnt+1u);\n                  }\n                  /* Ensure FIOS magic if ctor skipped */\n                  if(vm_read32(fea+0x0u)!=0x46494F53u)\n                    vm_write32(fea+0x0u, 0x46494F53u);\n                  if(vm_read32(fea+0x4u)!=0x66682020u)\n                    vm_write32(fea+0x4u, 0x66682020u);\n                  /* Host-only mfd/size map (guest FO fields stay clean at +38) */\n                  f2b_fo_mfd_put(fea, mfd, sz);\n                  /* F2B-FO-SIZE: natural open fills FO+0x48 with file size (u64).\n                   * Success path func_002B42EC does:\n                   *   container+0x10 = (u32)vm_read64(FO+0x48)\n                   * Without this, success overwrites pump size with 0 →\n                   * WADLD rem=0 and no state-2 stream (measured G4 §24). */\n                  vm_write32(fea+0x48u, 0u);\n                  vm_write32(fea+0x4Cu, sz);\n                  { static int _n=0; if(_n++<8){\n                    fprintf(stderr,"[FIOSOPEN] F2B-FO-SIZE fo=0x%08X +48=+4C size=%u\\n",\n                      fea, sz);\n                    fflush(stderr); } }\n                  /* Optional TOC preload for small WAD (Lgl 3072) */\n                  if(sz>0u && sz<=65536u){\n                    uint32_t tbuf = fea + 0x180u;\n                    extern unsigned char* vm_base;\n                    if(vm_base && tbuf+sz < 0x4F000000u){\n                      unsigned got = movie_io_pread(mfd, vm_base+tbuf, sz, 0u);\n                      if(got==sz){\n                        fprintf(stderr,"[FIOSOPEN] F2B-PRELOAD fo=0x%08X tbuf=0x%08X n=%u\\n",\n                          fea, tbuf, got);\n                      }\n                    }\n                  }\n                  /* Restore regs (except r3 = FO return) */\n                  for(int ri=0;ri<32;ri++) if(ri!=3) ctx->gpr[ri]=sv[ri];\n                  ctx->lr=sv_lr;\n                  ctx->gpr[3]=fea;\n                  /* DONEFORCE open op */\n                  { uint32_t op=(uint32_t)ctx->gpr[27];\n                    if(op>=0x10000u && op<0x4F000000u){\n                      vm_write32(op+0x90u, 1u);\n                      vm_write32(op+0x44u, 0u);\n                      vm_write32(op+0xCCu, 0u);\n                      ps3_fios_sticky_publish(op);\n                      fprintf(stderr,"[FIOSOPEN] F2B-DONEFORCE op=0x%08X +44=0 +CC=0\\n", op);\n                    } }\n                  fprintf(stderr,"[FIOSOPEN] F2B-MOVIEIO path=\'%s\' mfd=0x%X sz=%u file=0x%08X media=0x%08X hash=+34=%08X\\n",\n                    _pt, mfd, sz, fea, media, vm_read32(fea+0x34u));\n                  fprintf(stderr,"[FIOSOPEN] F2B-FO-DUMP fo=0x%08X", fea);\n                  for(int _k=0;_k<0x58;_k+=4)\n                    fprintf(stderr," +%02X=%08X", _k, vm_read32(fea+_k));\n                  fprintf(stderr,"\\n");\n                  fflush(stderr);\n                  } /* end non-reuse F2B FO ctor */\n                } else {\n                  fprintf(stderr,"[FIOSOPEN] F2B-MOVIEIO path=\'%s\' mfd=0x%X but no guest FO\\n", _pt, mfd);\n                  fflush(stderr);\n                }\n              } else {\n                fprintf(stderr,"[FIOSOPEN] F2B-MOVIEIO path=\'%s\' cache MISS\\n", _pt);\n                fflush(stderr);\n              }\n            }\n          } }\n'

# Simbolos host de que o bloco depende (injeccoes de preambulo, static no chunk).
# (nome, agulha que prova que a DEFINICAO existe no lift). `vm_base` e
# `movie_io_open` sao declarados dentro do proprio bloco, logo nao entram aqui.
DEPS = (
    ("g_f2b_natural_movie_fo", "static uint32_t g_f2b_natural_movie_fo"),
    ("f2b_fo_mfd_put", "static void f2b_fo_mfd_put("),
    ("movie_io_pread", "movie_io_pread(unsigned"),
)


def region(text: str):
    i = text.find(SIG)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(SIG))
    return (i, len(text) if j < 0 else j)


def write(path: Path, text: str) -> None:
    try:
        path.write_text(text, encoding="utf-8", newline="\n")
    except TypeError:                                  # Python < 3.10
        path.write_text(text, encoding="utf-8")


def patch_one(path: Path) -> int:
    """0 aplicado | 1 ALREADY | -1 nao tem a funcao | -2 ancora inesperada."""
    t = path.read_text(encoding="utf-8", errors="replace")
    span = region(t)
    if span is None:
        return -1
    b0, b1 = span
    if MARKER in t[b0:b1]:
        print(f"  {path.name}: func_0030D5CC ALREADY")
        return 1
    if t.count(ANCHOR, b0, b1) != 1:
        return -2
    at = t.index(ANCHOR, b0, b1)
    write(path, t[:at] + BLOCK + t[at:])
    print(f"  {path.name}: func_0030D5CC APPLIED ({BLOCK.count(chr(10))} linhas)")
    return 0


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
             if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3

    host = None
    hits = []
    for p in paths:
        r = patch_one(p)
        hits.append(r)
        if r in (0, 1):
            host = p
    if any(r == -2 for r in hits):
        print("ERRO: func_0030D5CC existe mas a ancora gerada NAO e' a esperada\n"
              "  (0 ou >1 ocorrencias do trio 0x8001<<16 / r31=r3 / r0|=0x70A).\n"
              "  O lifter mudou de forma. Nao insiro as cegas: revalida o bloco\n"
              "  contra um lift de producao conhecido-bom e regenera este script.",
              file=sys.stderr)
        return 2
    if not any(r in (0, 1) for r in hits):
        print("ERRO: func_0030D5CC nao existe em nenhum dos ficheiros dados.",
              file=sys.stderr)
        return 1

    # A verificacao e' POR CHUNK: os simbolos sao `static`, logo tem de estar
    # definidos NESTE chunk. Um homonimo noutro chunk nao serve.
    t = host.read_text(encoding="utf-8", errors="replace")
    missing = [name for name, needle in DEPS if needle not in t]
    if missing:
        print(f"AVISO: {host.name} usa mas nao define -> " + ", ".join(missing))
        print("  (injeccao de preambulo com dono proprio; fonte versionada em "
              "../lift_baseline/injected_001.cpp)")
    print("[fios-f2b-fo-block] ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
