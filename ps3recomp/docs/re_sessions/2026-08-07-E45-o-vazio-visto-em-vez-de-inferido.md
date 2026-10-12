# E45 — o vazio visto, em vez de inferido: 6 fábricas vazias, `cursor=-1`

> **ENQUADRAMENTO CORRIGIDO pelo [E52](2026-08-07-E52-dois-dos-seis-sao-por-desenho.md):**
> os **6** pedidos vazios nao sao 6 instancias do defeito -- 3 sao a cadeia, 1 e' rota
> latente, 2 sao **por desenho** (save/restore, valor nunca dereferenciado). Toda a
> medicao deste ledger sobrevive.

**Data:** 2026-08-07 · **Antecessor:** [E44](2026-08-07-E44-tres-niveis-guardam-o-consumidor-nao.md)

Paga a **dívida de instrumento** que o E44 declarou. A sonda `PRODUCT` vive
dentro do ramo não-negativo e não consegue observar o caso nulo; tudo o que o
E44 disse sobre ele era inferido de ausência, com controlo. Agora está visto.

---

## O que mudou no instrumento

`recomp_mid_v2/patch_39e5a8_empty_factory_probe.py` (novo, idempotente, na
ordem de reaplicação — 152 patches, era 151). Põe a sonda **antes** do ramo:

```c
ctx->gpr[9] = (int64_t)(int32_t)ctx->gpr[9];
/* EMPTYFAB-PROBE: imprime SEMPRE, os dois ramos */
if (((ctx->cr >> 0) & 8)) goto loc_0039E5D0;
```

Gate `PS3_TRACE_EMPTYFAB`, OFF por default, read-only.

O irmão `patch_39e5a8_product_probe.py` fica como está — e fica anotado que a
sonda dele é **cega ao ramo negativo** por construção. Não é para substituir:
os dois juntos dão a contagem cruzada abaixo.

## A predição, escrita antes da corrida

Está no cabeçalho do patch, commitada antes de existir medição:

> *«as chamadas vindas de `func_0024E3D0` para as fábricas `0x400C6210`,
> `0x403008E8` e `0x40300E80` devem sair `VAZIO`. Se saírem `ok`, o E44 cai.»*

## A medição

Corrida sã de 35s, `boot_gow2_e44`, `PS3_TRACE_EMPTYFAB` + `PRODUCT` + `B71`:

```
[EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO->devolve-0 lr=0x0024E424
[EMPTYFAB] fab=0x403008E8 cursor=-1 VAZIO->devolve-0 lr=0x0024E424
[EMPTYFAB] fab=0x40300E80 cursor=-1 VAZIO->devolve-0 lr=0x0024E25C
[EMPTYFAB] fab=0x400D6DA0 cursor=-1 VAZIO->devolve-0 lr=0x00237D38
[EMPTYFAB] fab=0x400C6210 cursor=-1 VAZIO->devolve-0 lr=0x002B2DE0
[EMPTYFAB] fab=0x401002F0 cursor=-1 VAZIO->devolve-0 lr=0x002A9A84
```

**259 chamadas = 253 `ok` + 6 `VAZIO`.** E a sonda `PRODUCT`, cega ao vazio,
imprimiu exactamente **253**. Os dois instrumentos fecham um no outro sem folga
— é a corroboração cruzada que faltava.

**A predição bate:** as três fábricas nomeadas pelo E44 saem `VAZIO`, com
`cursor=-1`, e as três primeiras têm `lr` no sítio consumidor.

## Controlo

- o sintoma está na mesma corrida: **16 escritas `UNCOMMITTED` em
  `func_00220284`** (mais 1 em `func_00263554`, 2 em `func_0041C9D0`);
- `FIOSOPEN=241`, dentro do baseline;
- tecto não esgotado (259 contra 100000).

## O E44 confirma-se, e alarga-se

O E44 disse *«3 em 3 chamadas do sítio devolvem nulo»*. Confirmado por
observação directa. Mas há **mais três** fábricas vazias, de **outros** sítios:

| fab | `lr` | fragmento |
|---|---|---|
| `0x400C6210` | `0x0024E424` | `func_0024E414 + 0x10` |
| `0x403008E8` | `0x0024E424` | `func_0024E414 + 0x10` |
| `0x40300E80` | `0x0024E25C` | `func_0024E208 + 0x54` |
| `0x400D6DA0` | `0x00237D38` | `func_00237CDC + 0x5C` |
| `0x400C6210` | `0x002B2DE0` | `func_002B2DD0 + 0x10` |
| `0x401002F0` | `0x002A9A84` | `func_002A98E8 + 0x19C` |

Duas leituras que isto força:

1. **`0x400C6210` sai vazia duas vezes, para dois chamadores diferentes.** Não é
   um chamador azarado; é a fábrica que está vazia quando lhe pedem.
2. **Quatro sítios distintos pedem produto a fábricas vazias na mesma corrida.**
   O E44 tratava isto como propriedade de `func_0024E3D0`. Não é: é uma
   propriedade do **estado das fábricas** neste ponto do boot.

Nota sobre os fragmentos: o lifter sobrepõe fragmentos, e `func_0024E414` cai
dentro do corpo liftado de `func_0024E3D0`. Os nomes acima são o fragmento que
contém o endereço de retorno guest, não necessariamente a função lógica.

## Honestidade sobre a corrida

**Das 2 corridas do binário novo, 1 flakou** — `st620` preso em `0 -> 0`, zero
`FIOSOPEN`, zero `UNCOMMITTED`, nenhuma sonda a disparar. É o flake conhecido
que o CLAUDE.md descreve. Refutado como regressão pela segunda corrida, que
reproduz o baseline inteiro. Registado porque a taxa importa: quem repetir isto
deve contar com ~1 em 2 e não concluir da primeira.

## Refutado por medição

1. **«o E44 inferiu mal a partir da ausência»** — a inferência bate com a
   observação directa, incluindo as três fábricas nomeadas e o `cursor=-1`.
2. **«só `func_0024E3D0` pede produto a fábrica vazia»** — são 4 sítios.
3. Tudo o que E11–E17 e E19–E44 já refutaram.

## Próximo

1. **porque é que `0x400C6210` está vazia às duas vezes que lhe pedem** — quem
   faz o push (as 176 do E42/E43) e onde caem esses pushes na linha do tempo
   relativamente aos `lr` acima;
2. o mesmo para as outras três fábricas — se todas partilham a causa, é um
   defeito de ordem de inicialização, não de um caminho;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
