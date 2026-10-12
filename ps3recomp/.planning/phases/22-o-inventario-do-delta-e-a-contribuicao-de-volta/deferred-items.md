# Deferred items — Fase 22, Plano 22-01

## Falhas PRE-EXISTENTES em `scripts/test_python.sh` (fora do ambito deste plano)

Descobertas ao correr a suite completa na verificacao do Plano 22-01. **Nao foram
causadas por este plano** e por isso nao foram corrigidas (regra de fronteira de
ambito: so' se auto-corrige o que as alteracoes da task causaram).

| Teste | rc | Causa medida |
|---|---|---|
| `tools/test_ppu_lift.py` | 1 | `COMPILE FAILED` do harness de conformidade PPU |
| `runtime/ppu/tests/test_ppu_lifter_emit.py` | 1 | `include/ps3emu/endian.h:12:10: fatal error: 'string.h' file not found` |

**Prova de que sao pre-existentes** [nivel de evidencia: `offline-unit`]: ambos os
testes foram corridos num `git worktree` destacado em `65d6675^` (o commit
imediatamente ANTERIOR ao primeiro commit deste plano) e falharam com rc=1 e a
mesma causa. As alteracoes deste plano tocam em `tools/upstream_delta.py`,
`tools/test_upstream_delta.py`, `docs/UPSTREAM_DELTA.tsv` e uma linha em cada um
de `CMakeLists.txt` / `scripts/test_python.sh` -- nenhuma delas entra no caminho
de compilacao destes harnesses.

Diagnostico provavel (NAO verificado, portanto `nao-exercitado`): o SDK de linha
de comandos do host nao esta a expor os headers C de sistema ao compilador que os
harnesses invocam. E' ambiente de toolchain, nao codigo do repo.

Baseline desta corrida: 18 testes -- PASS 14, SKIP 2, FAIL 2.
`tools/test_upstream_delta.py` (novo neste plano) passa.

---

## Reconfirmado no Plano 22-02 (2026-08-12)

As mesmas duas falhas continuam, e continuam pre-existentes -- agora provado
contra o baseline DESTE plano, nao o do 22-01:

```
worktree destacado em f8f7568 (= 3746e1c^, o commit imediatamente anterior ao
primeiro commit do Plano 22-02)
  tools/test_ppu_lift.py                 rc=1
  runtime/ppu/tests/test_ppu_lifter_emit.py  rc=1
causa em ambos: include/ps3emu/endian.h:12:10:
  fatal error: 'string.h' file not found
```

As alteracoes do Plano 22-02 sao `tools/upstream_jt_snapshot.py`,
`tools/test_upstream_jt_contrib.py`, `docs/upstream-contrib/*`,
`docs/UPSTREAM_DELTA.tsv` e uma linha em cada um de `CMakeLists.txt` /
`scripts/test_python.sh`. Nenhuma entra no caminho de compilacao destes
harnesses. Nao corrigido: fronteira de ambito (problema de toolchain do host).

Corrida deste plano: 19 testes -- PASS 15, SKIP 2, FAIL 2.
`tools/test_upstream_jt_contrib.py` (novo neste plano) passa.
