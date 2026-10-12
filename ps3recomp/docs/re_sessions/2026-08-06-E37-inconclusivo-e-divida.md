# E37 — inconclusivo (flake), e uma dívida declarada

**Data:** 2026-08-06 · **Antecessor:** [E36](2026-08-06-E36-indice-sem-limite.md)

Sonda `IDXBOUND` instalada em `func_0039D3C4` para medir o índice de classe
(`*(chave+6)`) e o endereço lido. **A corrida foi um flake e o resultado não
conta.**

## Porque não conta

```
IDXBOUND=5   MGRLOOKUP=0
linhas=1086  st620=0  flips=132  FIOSOPEN=0
```

Ambas as sondas estão no binário (`strings` confirma as duas). O boot mal
arrancou — `st620=0`, zero I/O. Um `MGRLOOKUP=0` numa corrida saudável seria
uma descoberta enorme (o sítio da vcall nunca executar); aqui é só uma corrida
que morreu cedo.

**Não interpretado.** As 5 linhas de `IDXBOUND` que saíram mostram `idx=0` em
todas e nenhuma com a chave má — consistente com "só correu a fase inicial".

## A dívida

A sonda foi aplicada por **edição directa** de `recomp_macos_v2/ppu_recomp_001.cpp`,
não por script idempotente. Isso viola a regra do projecto: toda a alteração ao
lift tem de virar `patch_*.py` commitado, ou desaparece no próximo re-lift.

**Por fazer:** converter em `patch_39d3c4_idxbound_probe.py` com o contrato
tri-estado, agulha verificada única (linhas 274937–274946 de
`ppu_recomp_001.cpp`, 1 ocorrência).

## Estado da frente

O E36 deixou a hipótese precisa: o lookup não verifica limite, e um
`*(chave+6)` fora de alcance lê para lá do array. **Continua por medir.** A
sonda existe e está no binário; falta uma corrida sã.

O flake `FIOSOPEN=0`/`st620=0` é o mesmo da família medida no E33 (~1 em 6, não
explicado, e agora sabidamente independente da carga).
