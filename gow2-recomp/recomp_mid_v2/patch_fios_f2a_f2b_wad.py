#!/usr/bin/env python3
"""DOCUMENTAL-ONLY -- este script NAO aplica nenhum patch. NAO conta para o gate.

Historico
---------
Criado em 2026-07-21 para catalogar os fixes F2a (freelist FIOS)/F2b
(movie_io do WAD) medidos naquela sessao, mas o `main()` sempre foi so um
`print()` de instrucoes em prosa -- nunca escreveu um unico byte num
ppu_recomp_*.cpp. Isso violava a regra do CLAUDE.md ("todo fix nos
ppu_recomp_XXX.cpp vira script idempotente commitado para sobreviver a
re-lift"): o fix real do F2a (FIOS-FREELIST-REBUILD) so existia como edicao
manual no lift `recomp_macos_v2.pre_v4`, e o re-lift do marco v1.0 apagou-a
sem que nenhum script a repusesse -- causa raiz da regressao do marco v1.1
(2026-07-31).

O QUE FOI CONVERTIDO EM SCRIPT REAL (2026-07-31)
-------------------------------------------------
  F2a (FIOS-FREELIST-REBUILD) -> recomp_mid_v2/patch_fios_freelist_rebuild.py
      (novo, idempotente, aplica de verdade -- ver seu docstring).
  STOP-YIELD (pre-requisito de ordem do F2a) -> recomp_mid_v2/patch_fios_stop_yield.py
      (ja existia; a agulha estava desatualizada por deriva de forma do
      lifter -- corrigida na mesma sessao).

O QUE CONTINUA SEM SCRIPT (nao coberto por esta sessao)
--------------------------------------------------------
  FIOS-HOST-POP        -- guest CAS falha em popar mesmo apos o rebuild;
                           medido presente 1x no lift antigo, sem script
                           dedicado. Se necessario, extrair de
                           recomp_macos_v2.pre_v4 (grep "FIOS-HOST-POP") e
                           escrever patch_fios_host_pop.py seguindo o mesmo
                           padrao (ancora + marker + idempotente).
  FIOS-F2B-MOVIEIO      -- dearchiver do WAD via movie_cache + fake file
                           object + DONEFORCE. Nao extraido nesta sessao.
  FIOS-42B4-CANCEL-YIELD -- variante de cancel-yield em func_002B4274;
                           possivelmente ja coberta por
                           patch_fios_done_cancel_yield.py (marker
                           FIOS-DONE-CANCEL-YIELD) -- verificar antes de
                           duplicar.

Este ficheiro fica so como indice historico. NAO o remova sem atualizar
apply_all_patches.sh (ele aparece no catalogo `patch_*.py`, mas
is_probe()/classe_de() devem classifica-lo fora do gate -- ou removido do
PATCH_CATALOG.tsv -- porque `main()` nunca escreve nada e portanto nunca
muda hash nenhum: sera sempre reportado como SKIP inofensivo, nunca como
FAILED, ja que nao ha NEEDLE nenhuma para nao encontrar).

Usage: python3 recomp_mid_v2/patch_fios_f2a_f2b_wad.py [recomp_macos_v2]
"""
from pathlib import Path
import sys

ROOT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "recomp_macos_v2"

def main():
    print("DOCUMENTAL-ONLY: este script nao aplica nada. Ver docstring para o")
    print("mapeamento de qual patch real cobre cada fix historico (F2a/F2b).")
    print()
    print("Fixes convertidos em script real (2026-07-31):")
    print("  F2a (freelist)  -> patch_fios_freelist_rebuild.py")
    print("  STOP-YIELD      -> patch_fios_stop_yield.py (agulha corrigida)")
    print()
    print("Fixes ainda SEM script (ver docstring, secao 'sem script'):")
    print("  FIOS-HOST-POP, FIOS-F2B-MOVIEIO, FIOS-42B4-CANCEL-YIELD")
    print()
    print("ROOT seria", ROOT)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
