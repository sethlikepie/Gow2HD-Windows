# E71 — é a fila: `0xBA10`, alinhada a 16 e não a 128

> **DIAGNOSTICO CORRIGIDO em 2026-08-08 pelo [E72](2026-08-08-E72-generalizei-de-duas-linhas-em-doze.md):**
> este ledger leu **duas** linhas de **doze** -- e as doze mostram tres valores
> distintos de `fila` (`0xBA10` x2, `0xFFFF` x4, `0x00010203` x6) e buffers fora do
> LS (`0xF6900003`). O cap de 12 era da propria sonda (`if (n < 12)`), copiado das
> vizinhas no dia seguinte a eu corrigir esse padrao no PPU.
> **SOBREVIVE:** a sonda funciona; nas amostras com `0xBA10` e' mesmo a fila que
> falha (`buffer & 15 = 0`); `0xBA10` e' literal do codigo com `& 127 = 16`; e o
> confundidor do `PS3_SPU_DBG` que este ledger ja' declarava.
> **CAI:** «e' a fila, defeito de colocacao em LS» como O diagnostico, e «ambos os
> valores estao dentro do LS».

**Data:** 2026-08-08 · **Antecessor:** [E70](2026-08-08-E70-o-erro-nao-e-nosso-vem-do-SPU-liftado.md)

O E70 localizou o erro no SPU liftado e deixou a pergunta: **qual dos dois
invariantes falha?** Sonda posta, e apanhada.

---

## A medição

```
[LFQALIGN-SPU] buffer=0x0003FFD0 fila=0x0000BA10
[LFQALIGN-SPU] buffer=0x0003FDD0 fila=0x0000BA10
```

| valor | teste | resultado |
|---|---|---|
| `buffer = 0x0003FFD0` | `& 15` = **0** | ✅ alinhado a 16 |
| `fila = 0x0000BA10` | `& 127` = **16** | ❌ **não alinhado a 128** |

**É a fila.** `0xBA10` é múltiplo de 16 mas não de 128 — falha por 16 bytes.

E `0x0000BA10` é exactamente o valor que o `[EDGEATT1]` já mostrava:

```
[EDGEATT1] a=0x0000BA10 b=0x80410910
```

Mesmo objecto, cadeia confirmada de ponta a ponta.

## São endereços de Local Store

Ambos os valores estão dentro dos 256 KB do LS do SPU (`0x00000`–`0x3FFFF`):
`0xBA10` a ~46 KB, `0x3FFD0` quase no topo. Não são EAs do guest.

Logo o defeito é de **colocação em LS**: a estrutura da LFQueue fica num
endereço só alinhado a 16, e a verificação do próprio jogo exige 128.

## A primeira convenção de patch para o `spu_lifted/`

`recomp_mid_v2/patch_spu1_9540_align_probe.py` — o `build_macos.sh` **não**
regenera essas árvores (compila os ficheiros que lá estão), logo uma edição à
mão sobrevive ao build mas não a um re-lift de SPU. O script existe para esse
caso e segue o contrato dos `patch_*.py` do PPU: idempotente, `rc=2` se a agulha
não casar.

## Um confundidor que declaro em vez de esconder

A caça teve uma anomalia. Com `PS3_SPU_DBG=1`:

| configuração | corridas | disparos |
|---|---|---|
| com `PS3_SPU_DBG=1` | 10 | **0** |
| sem a variável | 4 | **1** |

À taxa histórica (~36%, 4 em 11), zero em dez seria ~1%. Sem a variável voltou a
disparar à primeira leva.

**Isto sugere que a variável perturba o timing — mas não o prova.** Zero em dez
a uma taxa de 25% ainda tem ~5,6% de probabilidade, e a taxa histórica vem de
corridas intercaladas com builds, com carga de máquina diferente. Fica anotado
como confundidor por resolver, não como conclusão.

O que **não** é afectado: `UNCOMMITTED=19` e `st620=11` nas **catorze** corridas,
com e sem a variável, dispare ou não.

## Refutado por medição

1. **«o buffer está desalinhado»** — `0x0003FFD0 & 15 = 0`; quem falha é a fila.
2. **«são endereços do guest»** — `0xBA10` e `0x3FFD0` estão no LS do SPU.
3. Tudo o que E11–E17 e E19–E70 já refutaram.

## Próximo

1. **quem coloca a fila em `0xBA10`** — busca no SPU liftado por quem escreve
   esse ponteiro, ou o DMA que traz a estrutura para o LS;
2. porque é que o endereço varia entre corridas (é o que torna o defeito
   intermitente: às vezes calha múltiplo de 128);
3. resolver o confundidor do `PS3_SPU_DBG`;
4. promover o default do `PS3_GCM_CB`;
5. a parede dos E41–E61: fábricas por-tag não repovoadas, B71 no #60.
