# tools/testdata

Fixtures commitadas usadas pelos self-tests em `tools/test_*.py`.

## `midasm_golden_noconfig.c`

Golden de **não-regressão** do `tools/ppu_lifter.py` para a Fase 17 (XEN-01).

- **Como foi capturado:** `.venv/bin/python3 tools/test_midasm_emit.py --update-golden`
  corrido com o `ppu_lifter.py` **anterior** à emissão de mid-asm (git `4bf3bf8`,
  antes do commit que ligou `[[midasm_hook]]`). É por isso que ele prova alguma
  coisa: é o output de um lifter que não sabia o que era um mid-asm hook.
- **Fixture:** 4 palavras PPC sintéticas (`li r3,1` / `li r4,2` / `nop` / `blr`)
  em `0x1000`, uma função `[0x1000, 0x1010)`, lift `--raw --single-file --jobs 1`.
  Nenhum dado de jogo envolvido.
- **O que o teste faz com ele:** corre o lifter **actual sem `--config`** sobre a
  mesma fixture e faz `difflib.unified_diff` contra este ficheiro. O diff tem de
  ser vazio. Um `gow2_midasm_*` que apareça onde ninguém o declarou morre aqui.
- **Única normalização:** a linha `/* lifter-rev: <sha>[-dirty] */`, que é um
  carimbo de proveniência não-determinista por design (`_lifter_rev()` corre
  `git rev-parse` e marca `-dirty`). Todo o resto entra no diff literalmente.
- **Regenerar** só quando a mudança de output for **intencional** e explicada no
  commit — o golden existe precisamente para tornar isso um acto deliberado.
