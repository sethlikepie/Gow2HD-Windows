#!/usr/bin/env python3
"""Idempotent: CE03C MovieStop before 2nd Play when st620!=0.

ESTADO 2026-07-25 -- VERIFICADOR PURO DE COMPORTAMENTO INEXISTENTE
------------------------------------------------------------------
Este ficheiro nao escreve nada: so' confirma o marcador "CE03C pre-Play
MovieStop". Ao contrario dos outros orfaos do CE03C, este NAO tem bloco para
repor -- o comportamento nunca existiu em lado nenhum. Medido a 2026-07-25:

  grep -c "CE03C pre-Play MovieStop"  recomp_macos_v2/ppu_recomp_*.cpp  -> 0
      (lift de PRODUCAO, 31 chunks -- a fonte de verdade dos blocos orfaos)
  grep -c "CE03C pre-Play MovieStop"  <lift limpo + TODOS os patches>   -> 0
  grep -rn "CE03C pre-Play" <repo, fora dos lifts>
      -> uma unica linha: o MARKER deste proprio ficheiro

E a funcao alvo em producao tambem nao faz isto: `func_000CE03C`
(recomp_macos_v2/ppu_recomp_001.cpp:581990, 143 linhas) NAO chama
`func_002BFF88` (MovieStop) nenhuma vez -- 0 ocorrencias no corpo. A estrategia
que ficou em producao e' a OPOSTA: esperar pelo MovieStop NATURAL do 1o filme
("Pump until natural MovieStop (st=0)") antes do 2o Play. Todas as strings
CE03C do lift de producao pertencem a esse bloco wait-idle, que ja' tem
escritor -- `patch_ce03c_introseq_block.py`:

  CE03C wait-idle 1st movie / wait tick / wait-idle exit / arm +0x714 /
  clear sticky EOS hook / Play aborted / post-abort freelist rebuild|SKIP

Logo nao ha' bloco orfao para instalar: nao existe codigo do qual copiar, e
escreve-lo de raiz seria inventar comportamento (regra 4 do CLAUDE.md), ainda
por cima a colidir com o wait-idle. O MISSING/rc=1 e' o resultado CORRECTO. O
check NAO foi enfraquecido -- nada abaixo desta docstring foi tocado.

Para o veredicto mudar e' preciso trabalho de bring-up (decidir e provar que um
MovieStop forcado antes do 2o Play e' fiel ao console), nao reparacao de agulha.
"""
from pathlib import Path
import sys
from lift_paths import resolve_lift_paths
MARKER = "CE03C pre-Play MovieStop"
def main():
    paths = resolve_lift_paths(sys.argv[1:], "recomp_macos_v2/ppu_recomp_001.cpp")
    rc = 0
    for p in paths:
        t = p.read_text(errors="replace") if p.exists() else ""
        ok = MARKER in t
        print(f"{'ok' if ok else 'MISSING'}: {p}")
        if not ok: rc = 1
    return rc
if __name__ == "__main__":
    raise SystemExit(main())
