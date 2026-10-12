# SHGX expand → ICGLdr: veredito A1 (2026-07-22)

## Resumo

Com o pipeline WAD Mac (open ret=0, FO size, STREAM-FILL, SHGX T1SZ/VT28/GEND/FIN)
a trabalhar, o nested typemap **continua a zero no caminho natural**.

| Path | TYMAP-171 | LDRSH | CMP-ENTER |
|------|----------:|------:|----------:|
| Natural (WAD full + SHGX expand) | 0 | 0 | 0 |
| `PS3_GATE_FORCE=1` pós-rperma (diag) | 1 | 1 | 0 |

**Classe A1:** o caller `func_0032E200` nunca corre; o typemap **já está
instalado** (`vt=0x5130B8` em `0x4306B160` via component `0x4306ADF0`).

SHGX no TOC é **stub de 48 bytes** (catálogo). Expand/FIN não entregam
microcódigo EFCT; esse caminho é HOSTRES (já no runtime note).

Logs: `/tmp/vdec_t1sz.log` (natural), `/tmp/vdec_gate.log` (force diag).
G4 §27.

## Follow-up in-boot (mesma data) — cadeia acima de 0032E200

`notes/2026-07-22-a1-chain-inboot.md`: com probes A1 em
`00468C3C`/`00330D54`/`0032DF98` (patch `patch_a1_chain_probe.py`), **toda a
subárvore = 0** no boot natural pós-R_Perm full + SHGX expand. O "nunca
alcançado" sobe do walk até ao portão `00468C3C` (e micro-ctor `00329490`).
Gate estrutural `*r5=0` permanece válido offline; in-boot nem chega a
exercitá-lo.

